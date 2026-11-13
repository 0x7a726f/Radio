#include "cs16_writer.h"
#include "simd_dsp.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <filesystem>
#include <ctime>
#include <cstring>

namespace fs = std::filesystem;

static std::wstring to_wide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

Cs16Writer::Cs16Writer(const std::string& output_dir)
    : output_dir_(output_dir) {
    std::error_code ec;
    fs::create_directories(output_dir_, ec);
    free_pool_.reserve(128);
}

Cs16Writer::~Cs16Writer() {
    stop_recording();
}

static std::string format_iso_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
    return std::string(buf);
}

static std::string format_time_slug_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_now);
    return std::string(buf);
}

std::string Cs16Writer::start_recording(
    uint32_t sample_rate_hz,
    uint64_t center_freq_hz,
    const std::string& device_name,
    float gain_db,
    bool bias_tee,
    const std::string& custom_filepath,
    double max_duration_s
) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (is_recording_.load()) {
        return get_current_filepath();
    }
    // Join any finished auto-stopped worker thread before restarting
    if (writer_thread_.joinable()) {
        stop_worker_ = true;
        queue_cv_.notify_all();
        writer_thread_.join();
    }

    sample_rate_hz_ = sample_rate_hz;
    center_freq_hz_ = center_freq_hz;
    device_name_ = device_name;
    gain_db_ = gain_db;
    bias_tee_ = bias_tee;
    max_duration_s_ = max_duration_s;
    iso_start_timestamp_ = format_iso_now();

    std::string path_to_use;
    if (!custom_filepath.empty()) {
        path_to_use = custom_filepath;
    } else {
        std::ostringstream ss;
        ss << output_dir_ << "/hi_capture_" << format_time_slug_now() << "_"
           << std::fixed << std::setprecision(3) << (center_freq_hz / 1e6) << "MHz_"
           << std::setprecision(2) << (sample_rate_hz / 1e6) << "MSPS.cs16";
        path_to_use = ss.str();
    }

    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        current_filepath_ = path_to_use;
    }

    std::error_code ec;
    fs::create_directories(fs::path(path_to_use).parent_path(), ec);

#ifdef _WIN32
    file_handle_ = CreateFileW(
        to_wide(path_to_use).c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return "";
    }
#endif

    bytes_written_ = 0;
    samples_written_ = 0;
    dropped_blocks_ = 0;
    sidecars_written_.store(false);
    start_time_ = std::chrono::high_resolution_clock::now();
    stop_worker_ = false;
    is_recording_ = true;

    // Recycle any leftover queued buffers back into free_pool_
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!write_queue_.empty()) {
            free_pool_.push_back(std::move(write_queue_.front()));
            write_queue_.pop_front();
        }
    }

    writer_thread_ = std::thread(&Cs16Writer::writer_worker, this);
    return path_to_use;
}

bool Cs16Writer::stop_recording() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    bool was_recording = is_recording_.exchange(false);
    bool has_thread = writer_thread_.joinable();
    if (!was_recording && !has_thread) {
        return false;
    }

    stop_worker_ = true;
    queue_cv_.notify_all();

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    auto now = std::chrono::high_resolution_clock::now();
    double duration_s = std::chrono::duration<double>(now - start_time_).count();

    std::string cur_path = get_current_filepath();
    if (!cur_path.empty() && fs::exists(cur_path)) {
        {
            std::lock_guard<std::mutex> lock(path_mutex_);
            last_recorded_filepath_ = cur_path;
        }
        if (!sidecars_written_.exchange(true)) {
            fs::path p(cur_path);
            fs::path base = p.parent_path() / p.stem();
            write_accompanied_txt(base.string() + ".txt", duration_s);
            write_sidecar_json(base.string() + ".json", duration_s);
        }
    }

    return was_recording || has_thread;
}

std::string Cs16Writer::get_current_filepath() const {
    std::lock_guard<std::mutex> lock(path_mutex_);
    return current_filepath_;
}

std::string Cs16Writer::get_last_recorded_filepath() const {
    std::lock_guard<std::mutex> lock(path_mutex_);
    return last_recorded_filepath_;
}

void Cs16Writer::feed_samples(const uint8_t* data, size_t length) {
    if (!is_recording_.load(std::memory_order_acquire) || !data || length == 0) return;

    if (max_duration_s_ > 0.0) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time_).count();
        if (elapsed >= max_duration_s_) {
            // Never join() writer_thread_ from the producer thread; signal stop cleanly
            is_recording_.store(false);
            stop_worker_.store(true);
            queue_cv_.notify_all();
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (write_queue_.size() < 128) {
            std::vector<uint8_t> block;
            if (!free_pool_.empty()) {
                block = std::move(free_pool_.back());
                free_pool_.pop_back();
            }
            block.resize(length);
            std::memcpy(block.data(), data, length);
            write_queue_.push_back(std::move(block));
            queue_cv_.notify_one();
        } else {
            dropped_blocks_++;
        }
    }
}

void Cs16Writer::writer_worker() {
#ifdef _WIN32
    // Elevate thread priority to HIGHEST to prevent Windows IO thread stalls
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
#endif

#ifndef _WIN32
    std::ofstream out(get_current_filepath(), std::ios::binary);
    if (!out.is_open()) {
        is_recording_ = false;
        return;
    }
    std::vector<char> file_buf(2097152);
    out.rdbuf()->pubsetbuf(file_buf.data(), file_buf.size());
#endif

    std::vector<std::vector<uint8_t>> batch;
    batch.reserve(32);
    std::vector<int16_t> cs16_staging;
    cs16_staging.reserve(1024 * 1024); // 2 MB initial CS16 batch staging

    while (true) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !write_queue_.empty() || stop_worker_.load();
            });
            if (write_queue_.empty()) {
                if (stop_worker_.load()) break;
                continue;
            }
            while (!write_queue_.empty()) {
                batch.push_back(std::move(write_queue_.front()));
                write_queue_.pop_front();
            }
        }

        size_t total_u8 = 0;
        for (const auto& blk : batch) total_u8 += blk.size();
        if (cs16_staging.size() < total_u8) {
            cs16_staging.resize(total_u8);
        }

        size_t offset = 0;
        for (const auto& blk : batch) {
            SimdDsp::convert_u8_to_cs16_fast(blk.data(), cs16_staging.data() + offset, blk.size());
            offset += blk.size();
        }

        const size_t total_bytes = total_u8 * sizeof(int16_t);
        bool write_ok = true;
#ifdef _WIN32
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(cs16_staging.data());
        size_t rem = total_bytes;
        while (rem > 0) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(rem, 32ULL * 1024ULL * 1024ULL));
            DWORD written = 0;
            if (!WriteFile(file_handle_, ptr, chunk, &written, nullptr) || written != chunk) {
                write_ok = false;
                break;
            }
            ptr += written;
            rem -= written;
        }
#else
        out.write(reinterpret_cast<const char*>(cs16_staging.data()), static_cast<std::streamsize>(total_bytes));
        write_ok = out.good();
#endif

        if (write_ok) {
            bytes_written_ += total_bytes;
            samples_written_ += (total_bytes / 4);
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            for (auto& blk : batch) {
                if (free_pool_.size() < 128) {
                    free_pool_.push_back(std::move(blk));
                }
            }
        }
        batch.clear();
    }

#ifdef _WIN32
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(file_handle_);
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    out.flush();
    out.close();
#endif

    // Ensure sidecar metadata is emitted promptly even on non-blocking auto-stop
    if (!is_recording_.load() && !sidecars_written_.exchange(true)) {
        std::string cur_path = get_current_filepath();
        if (!cur_path.empty() && fs::exists(cur_path)) {
            auto now = std::chrono::high_resolution_clock::now();
            double duration_s = std::chrono::duration<double>(now - start_time_).count();
            {
                std::lock_guard<std::mutex> lock(path_mutex_);
                last_recorded_filepath_ = cur_path;
            }
            fs::path p(cur_path);
            fs::path base = p.parent_path() / p.stem();
            write_accompanied_txt(base.string() + ".txt", duration_s);
            write_sidecar_json(base.string() + ".json", duration_s);
        }
    }
}

void Cs16Writer::write_accompanied_txt(const std::string& txt_path, double duration_s) {
    double fc_hz = (double)center_freq_hz_;
    double fc_mhz = fc_hz / 1e6;
    double fs_hz = (double)sample_rate_hz_;
    double fs_msps = fs_hz / 1e6;
    double offset_hz = fc_hz - SdrConstants::HI_REST_FREQ_HZ;
    double offset_khz = offset_hz / 1e3;
    double dur_min = duration_s / 60.0;
    double size_mb = (double)bytes_written_ / (1024.0 * 1024.0);
    double size_gb = (double)bytes_written_ / (1024.0 * 1024.0 * 1024.0);
    std::string cs16_filename = fs::path(current_filepath_).filename().string();
    std::string full_path = fs::absolute(current_filepath_).string();

    double passband_min_mhz = (fc_hz - fs_hz / 2.0) / 1e6;
    double passband_max_mhz = (fc_hz + fs_hz / 2.0) / 1e6;

    std::string offset_desc;
    if (std::abs(offset_khz) < 1.0) {
        offset_desc = "DIRECT CENTERED (LO placed exactly on HI line; residual DC spike may affect line peak)";
    } else if (offset_hz > 0) {
        std::ostringstream ss;
        ss << "+" << std::fixed << std::setprecision(2) << offset_khz
           << " kHz ABOVE HI line (EXCELLENT: LO DC spike is safe and clear of the HI line)";
        offset_desc = ss.str();
    } else {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << offset_khz
           << " kHz BELOW HI line (EXCELLENT: LO DC spike is safe and clear of the HI line)";
        offset_desc = ss.str();
    }

    std::ofstream f(txt_path);
    if (!f.is_open()) return;

    f << "================================================================================\n"
      << "  HYDROGEN LINE (21cm) RADIO ASTRONOMY OBSERVATION PARAMETERS\n"
      << "================================================================================\n"
      << "Recording File:             " << cs16_filename << "\n"
      << "Full Path:                  " << full_path << "\n"
      << "Capture Timestamp:          " << iso_start_timestamp_ << "\n"
      << "Receiver Hardware:          " << device_name_ << "\n\n"
      << "1. OBSERVATIONAL FREQUENCIES & LO OFFSET\n"
      << "--------------------------------------------------------------------------------\n"
      << "Celestial Target Line:      Neutral Atomic Hydrogen (H I, 21 cm Spin-Flip)\n"
      << "HI Rest Frequency:          1420.405752 MHz (1420405751.768 Hz)\n"
      << std::fixed << std::setprecision(6)
      << "Tuned SDR Center LO:        " << fc_mhz << " MHz (" << (int64_t)fc_hz << " Hz)\n"
      << std::showpos << std::setprecision(3)
      << "LO Frequency Offset:        " << offset_khz << " kHz (" << (int64_t)offset_hz << " Hz)\n"
      << std::noshowpos
      << "Offset Strategy:            " << offset_desc << "\n"
      << std::setprecision(6)
      << "DC Spike Excision Window:   Excise +/- 5.0 kHz around " << fc_mhz << " MHz\n\n"
      << "2. SDR HARDWARE & RF CONFIGURATION\n"
      << "--------------------------------------------------------------------------------\n"
      << std::setprecision(3)
      << "Sampling Rate (Fs):         " << fs_msps << " MSPS (" << (int64_t)fs_hz << " Hz)\n"
      << "Receiver Nyquist Bandwidth: " << fs_msps << " MHz (Coverage: " << passband_min_mhz << " to " << passband_max_mhz << " MHz)\n"
      << std::setprecision(1)
      << "Tuner LNA Gain:             " << gain_db_ << " dB\n"
      << "Bias-Tee (4.5V LNA Power):  " << (bias_tee_ ? "Enabled (Powers external Sawbird+ H1 LNA)" : "Disabled") << "\n"
      << "Data Format:                CS16 (Signed 16-bit Complex Integer, Little-Endian)\n"
      << "Bytes Per Complex Sample:   4 bytes (2 bytes I + 2 bytes Q)\n"
      << "Sample Interleaving:        [I0, Q0, I1, Q1, I2, Q2, ...]\n\n"
      << "3. ACQUISITION RUNTIME METRICS\n"
      << "--------------------------------------------------------------------------------\n"
      << std::setprecision(2)
      << "Observation Duration:       " << duration_s << " seconds (" << dur_min << " minutes)\n"
      << "Total Complex Samples:      " << samples_written_.load() << " samples\n"
      << "Total File Size:            " << bytes_written_.load() << " bytes (" << size_mb << " MB / "
      << std::setprecision(3) << size_gb << " GB)\n"
      << std::setprecision(2)
      << "Average Data Rate:          " << ((fs_hz * 4.0) / (1024.0 * 1024.0)) << " MB/s\n\n"
      << "4. OFFLINE ANALYSIS INSTRUCTIONS\n"
      << "--------------------------------------------------------------------------------\n"
      << "The Radio Astronomy Suite GUI & CLI automatically read this sidecar, or you can\n"
      << "run offline analysis directly with the observed parameters:\n\n"
      << "  [CLI Full Pipeline & 5-Panel PNG Export]\n"
      << std::fixed << std::setprecision(4)
      << "  radio_suite_cli.exe analyze \"" << cs16_filename << "\" --fc " << fc_mhz << "M --fs " << fs_msps << "M --out output\n\n"
      << "  [Interactive GUI Analyzer]\n"
      << "  radio_suite.exe (Switch to Offline HI Analysis mode and select recording)\n"
      << "================================================================================\n";
    f.flush();
    f.close();
}

void Cs16Writer::write_sidecar_json(const std::string& json_path, double duration_s) {
    std::ofstream f(json_path);
    if (!f.is_open()) return;

    double offset_hz = (double)center_freq_hz_ - SdrConstants::HI_REST_FREQ_HZ;
    double offset_khz = offset_hz / 1e3;

    std::string clean_path;
    {
        std::lock_guard<std::mutex> lock(path_mutex_);
        clean_path = fs::absolute(current_filepath_).generic_string();
    }

    f << "{\n"
      << "  \"format\": \"cs16\",\n"
      << "  \"endianness\": \"little\",\n"
      << "  \"bytes_per_sample\": 4,\n"
      << "  \"sample_rate_hz\": " << sample_rate_hz_ << ",\n"
      << "  \"center_freq_hz\": " << center_freq_hz_ << ",\n"
      << "  \"hi_rest_freq_hz\": " << std::fixed << std::setprecision(3) << SdrConstants::HI_REST_FREQ_HZ << ",\n"
      << std::fixed << std::setprecision(3)
      << "  \"lo_offset_hz\": " << offset_hz << ",\n"
      << "  \"lo_offset_khz\": " << offset_khz << ",\n"
      << "  \"timestamp\": \"" << iso_start_timestamp_ << "\",\n"
      << std::setprecision(2)
      << "  \"duration_seconds\": " << duration_s << ",\n"
      << "  \"total_samples\": " << samples_written_.load() << ",\n"
      << "  \"file_size_bytes\": " << bytes_written_.load() << ",\n"
      << "  \"device\": \"" << device_name_ << "\",\n"
      << std::setprecision(1)
      << "  \"gain_db\": " << gain_db_ << ",\n"
      << "  \"bias_tee\": " << (bias_tee_ ? "true" : "false") << ",\n"
      << "  \"file_path\": \"" << clean_path << "\"\n"
      << "}\n";
    f.flush();
    f.close();
}

RecordingStats Cs16Writer::get_stats() {
    RecordingStats s;
    s.status = is_recording_ ? "RECORDING" : "IDLE";
    std::string cur_path = get_current_filepath();
    s.filename = cur_path.empty() ? "None" : fs::path(cur_path).filename().string();
    s.full_path = cur_path;
    s.bytes_written = bytes_written_.load();
    s.samples_written = samples_written_.load();
    s.dropped_blocks = dropped_blocks_.load();

    if (is_recording_) {
        auto now = std::chrono::high_resolution_clock::now();
        s.elapsed_seconds = std::chrono::duration<double>(now - start_time_).count();
    } else {
        s.elapsed_seconds = 0.0;
    }

    int total_s = (int)s.elapsed_seconds;
    int hrs = total_s / 3600;
    int mins = (total_s % 3600) / 60;
    int secs = total_s % 60;
    char el_buf[32];
    snprintf(el_buf, sizeof(el_buf), "%02d:%02d:%02d", hrs, mins, secs);
    s.elapsed_str = el_buf;

    // File size string
    double mb = (double)s.bytes_written / (1024.0 * 1024.0);
    char sz_buf[32];
    if (mb >= 1024.0) {
        snprintf(sz_buf, sizeof(sz_buf), "%.2f GB", mb / 1024.0);
    } else {
        snprintf(sz_buf, sizeof(sz_buf), "%.2f MB", mb);
    }
    s.size_str = sz_buf;

    // Free disk space via Windows API
    ULARGE_INTEGER free_bytes_avail, total_bytes, total_free_bytes;
    std::wstring dir_w = to_wide(output_dir_.empty() ? "." : output_dir_);
    if (GetDiskFreeSpaceExW(dir_w.c_str(), &free_bytes_avail, &total_bytes, &total_free_bytes)) {
        s.free_space_gb = (double)free_bytes_avail.QuadPart / (1024.0 * 1024.0 * 1024.0);
        char free_buf[32];
        snprintf(free_buf, sizeof(free_buf), "%.2f GB", s.free_space_gb);
        s.free_space_str = free_buf;

        uint64_t bytes_per_sec = (uint64_t)sample_rate_hz_ * 4ULL;
        if (bytes_per_sec > 0) {
            uint64_t time_left_s = free_bytes_avail.QuadPart / bytes_per_sec;
            int tl_days = (int)(time_left_s / 86400);
            int tl_hrs = (int)((time_left_s % 86400) / 3600);
            int tl_mins = (int)((time_left_s % 3600) / 60);
            int tl_secs = (int)(time_left_s % 60);
            char tl_buf[64];
            if (tl_days > 0) {
                snprintf(tl_buf, sizeof(tl_buf), "%02d:%02d:%02d:%02d", tl_days, tl_hrs, tl_mins, tl_secs);
            } else {
                snprintf(tl_buf, sizeof(tl_buf), "%02d:%02d:%02d", tl_hrs, tl_mins, tl_secs);
            }
            s.time_left_str = tl_buf;
        } else {
            s.time_left_str = "--:--:--";
        }
    } else {
        s.free_space_str = "Unknown";
        s.time_left_str = "--:--:--";
    }

    return s;
}
