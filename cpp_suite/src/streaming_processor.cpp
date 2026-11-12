#include "streaming_processor.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <future>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "kiss_fft.h"

namespace astro {

constexpr double PI = 3.14159265358979323846;

struct KissFftDeleter {
    void operator()(kiss_fft_cfg cfg) const {
        if (cfg) kiss_fft_free(cfg);
    }
};
using KissFftPtr = std::unique_ptr<kiss_fft_state, KissFftDeleter>;

std::vector<float> StreamingProcessor::generate_window(WindowType type, int size) {
    std::vector<float> win(size);
    double n_minus_1 = static_cast<double>(size - 1);
    if (n_minus_1 <= 0.0) n_minus_1 = 1.0;

    for (int i = 0; i < size; ++i) {
        double x = static_cast<double>(i) / n_minus_1;
        switch (type) {
            case WindowType::Hann:
                win[i] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * PI * x)));
                break;
            case WindowType::BlackmanHarris:
                win[i] = static_cast<float>(
                    0.35875 
                    - 0.48829 * std::cos(2.0 * PI * x)
                    + 0.14128 * std::cos(4.0 * PI * x)
                    - 0.01168 * std::cos(6.0 * PI * x)
                );
                break;
            case WindowType::Blackman:
            default:
                win[i] = static_cast<float>(
                    0.42 
                    - 0.50 * std::cos(2.0 * PI * x)
                    + 0.08 * std::cos(4.0 * PI * x)
                );
                break;
        }
    }
    return win;
}

StreamingProcessor::StreamingProcessor(const std::string& filepath, const AstroConfig& config)
    : filepath_(filepath), config_(config) {
    if (!std::filesystem::exists(filepath)) {
        throw std::runtime_error("IQ file does not exist: " + filepath);
    }
    if (!std::filesystem::is_regular_file(filepath)) {
        throw std::runtime_error("IQ path is not a regular file: " + filepath);
    }

    uintmax_t raw_sz = std::filesystem::file_size(filepath);
    if (raw_sz == 0) {
        throw std::runtime_error("IQ file is empty (0 bytes): " + filepath);
    }
    if (raw_sz < 4) {
        throw std::runtime_error("IQ file is too small to contain a single CS16 sample (" +
                                 std::to_string(raw_sz) + " bytes): " + filepath);
    }

    // CS16: 4 bytes per complex sample (int16_t I + int16_t Q)
    uint64_t remainder = raw_sz % 4;
    if (remainder != 0) {
        std::cerr << "Warning: IQ file size (" << raw_sz << " bytes) is not a multiple of 4 (CS16 sample size). Ignoring "
                  << remainder << " trailing incomplete byte" << (remainder > 1 ? "s" : "") << ".\n";
        file_size_ = raw_sz - remainder;
    } else {
        file_size_ = raw_sz;
    }

    total_samples_ = file_size_ / 4;
    if (total_samples_ < static_cast<uint64_t>(config_.fft_size)) {
        throw std::runtime_error("IQ file contains " + std::to_string(total_samples_) +
                                 " samples, which is fewer than requested FFT size (" +
                                 std::to_string(config_.fft_size) + ") in " + filepath);
    }

    total_duration_s_ = static_cast<double>(total_samples_) / config_.sample_rate_hz;

    if (config_.max_duration_s > 0.0) {
        uint64_t target_samples = static_cast<uint64_t>(config_.max_duration_s * config_.sample_rate_hz);
        samples_to_process_ = std::min(total_samples_, target_samples);
    } else {
        samples_to_process_ = total_samples_;
    }

    // Align samples to complete FFT blocks to discard trailing incomplete block cleanly
    samples_to_process_ = (samples_to_process_ / static_cast<uint64_t>(config_.fft_size)) * static_cast<uint64_t>(config_.fft_size);

    bytes_to_process_ = samples_to_process_ * 4;
    duration_to_process_s_ = static_cast<double>(samples_to_process_) / config_.sample_rate_hz;
}

StreamingProcessor::~StreamingProcessor() = default;

StreamingResults StreamingProcessor::process(ProgressCallback progress_cb) {
    StreamingResults res;
    res.file_size_bytes = file_size_;
    res.total_samples = samples_to_process_;
    res.total_duration_s = duration_to_process_s_;

    const int nfft = config_.fft_size;
    const int half_nfft = nfft / 2;
    std::vector<float> window = generate_window(config_.window, nfft);

    // Prepare frequency axes
    res.freq_hz.resize(nfft);
    res.freq_mhz.resize(nfft);
    double bin_res = config_.sample_rate_hz / static_cast<double>(nfft);
    for (int i = 0; i < nfft; ++i) {
        double freq_rel = (i - half_nfft) * bin_res;
        double f_abs = freq_rel + config_.center_freq_hz;
        res.freq_hz[i] = f_abs;
        res.freq_mhz[i] = f_abs / 1e6;
    }

    // Number of blocks per streaming batch (~16 MB RAM buffer max)
    const int chunk_blocks = std::max(64, config_.chunk_blocks);
    const int chunk_samples = chunk_blocks * nfft;
    const size_t raw_buffer_bytes = static_cast<size_t>(chunk_samples) * 4;

    std::vector<int16_t> raw_buffer(chunk_samples * 2);

    // Waterfall decimation parameters
    double waterfall_dt = std::max(0.05, config_.waterfall_dt_s);
    int blocks_per_waterfall_row = std::max(1, static_cast<int>(waterfall_dt * config_.sample_rate_hz / nfft));

    std::vector<double> power_accumulator(nfft, 0.0);
    std::vector<double> current_row_accum(nfft, 0.0);
    int current_row_blocks = 0;

    std::vector<std::vector<float>> waterfall_rows;
    std::vector<double> waterfall_timestamps;
    size_t est_wf_rows = (bytes_to_process_ / 4) / (static_cast<size_t>(blocks_per_waterfall_row) * nfft) + 16;
    waterfall_rows.reserve(est_wf_rows);
    waterfall_timestamps.reserve(est_wf_rows);

    // KissFFT thread pool setup with guaranteed RAII cleanup
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    num_threads = std::min(num_threads, 8u);

    std::vector<KissFftPtr> fft_configs(num_threads);
    for (unsigned int t = 0; t < num_threads; ++t) {
        fft_configs[t].reset(kiss_fft_alloc(nfft, 0, nullptr, nullptr));
        if (!fft_configs[t]) {
            throw std::runtime_error("Failed to allocate KissFFT configuration for thread " + std::to_string(t));
        }
    }

    // Preallocated thread workspace buffers: ZERO heap allocations in hot path
    std::vector<std::vector<kiss_fft_cpx>> thread_cx_in(num_threads, std::vector<kiss_fft_cpx>(nfft));
    std::vector<std::vector<kiss_fft_cpx>> thread_cx_out(num_threads, std::vector<kiss_fft_cpx>(nfft));
    std::vector<std::future<void>> futures;
    futures.reserve(num_threads);

    std::ifstream file(filepath_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open IQ file for reading: " + filepath_);
    }

    uint64_t bytes_read_so_far = 0;
    uint64_t total_blocks_accumulated = 0;

    auto t_start = std::chrono::high_resolution_clock::now();
    auto last_progress_time = t_start;

    std::vector<float> thread_power_blocks(chunk_blocks * nfft);

    while (bytes_read_so_far < bytes_to_process_ && !is_cancelled()) {
        uint64_t bytes_left = bytes_to_process_ - bytes_read_so_far;
        size_t bytes_to_read = static_cast<size_t>(std::min<uint64_t>(raw_buffer_bytes, bytes_left));
        bytes_to_read = (bytes_to_read / 4) * 4; // Sample-aligned
        if (bytes_to_read == 0) break;

        file.read(reinterpret_cast<char*>(raw_buffer.data()), bytes_to_read);
        std::streamsize n_read = file.gcount();
        if (n_read <= 0) break;

        // Ensure sample and block alignment
        std::streamsize valid_sample_bytes = (n_read / 4) * 4;
        int samples_read = static_cast<int>(valid_sample_bytes / 4);
        int blocks = samples_read / nfft;

        if (blocks <= 0) {
            bytes_read_so_far += n_read;
            break;
        }

        // If leftover samples or partial bytes were read, rewind stream to preserve continuity
        int used_samples = blocks * nfft;
        int leftover_samples = samples_read - used_samples;
        std::streamoff rewind_bytes = (leftover_samples * 4) + (n_read - valid_sample_bytes);
        if (rewind_bytes > 0 && !file.eof()) {
            file.clear();
            file.seekg(-rewind_bytes, std::ios::cur);
            n_read -= rewind_bytes;
        }

        bytes_read_so_far += n_read;

        // Multithreaded FFT power computation: ZERO dynamic allocations inside tasks
        int blocks_per_thread = (blocks + num_threads - 1) / num_threads;
        futures.clear();

        for (unsigned int t = 0; t < num_threads; ++t) {
            int start_b = t * blocks_per_thread;
            int end_b = std::min(blocks, start_b + blocks_per_thread);
            if (start_b >= end_b) continue;

            futures.push_back(std::async(std::launch::async, [&, t, start_b, end_b]() {
                kiss_fft_cfg cfg = fft_configs[t].get();
                kiss_fft_cpx* p_cx_in = thread_cx_in[t].data();
                kiss_fft_cpx* p_cx_out = thread_cx_out[t].data();

                for (int b = start_b; b < end_b; ++b) {
                    const int16_t* p_raw = &raw_buffer[b * nfft * 2];
                    for (int k = 0; k < nfft; ++k) {
                        float w = window[k];
                        p_cx_in[k].r = static_cast<float>(p_raw[2 * k]) * w;
                        p_cx_in[k].i = static_cast<float>(p_raw[2 * k + 1]) * w;
                    }

                    kiss_fft(cfg, p_cx_in, p_cx_out);

                    float* p_out = &thread_power_blocks[b * nfft];
                    // FFT shift directly into output periodogram
                    for (int k = 0; k < half_nfft; ++k) {
                        float p1 = p_cx_out[k].r * p_cx_out[k].r + p_cx_out[k].i * p_cx_out[k].i;
                        p_out[k + half_nfft] = p1;
                    }
                    for (int k = half_nfft; k < nfft; ++k) {
                        float p2 = p_cx_out[k].r * p_cx_out[k].r + p_cx_out[k].i * p_cx_out[k].i;
                        p_out[k - half_nfft] = p2;
                    }
                }
            }));
        }

        for (auto& f : futures) {
            f.get();
        }

        // Accumulate periodograms and waterfall rows
        for (int b = 0; b < blocks; ++b) {
            const float* p_blk = &thread_power_blocks[b * nfft];
            for (int k = 0; k < nfft; ++k) {
                power_accumulator[k] += p_blk[k];
                current_row_accum[k] += p_blk[k];
            }
            total_blocks_accumulated++;
            current_row_blocks++;

            if (current_row_blocks >= blocks_per_waterfall_row) {
                std::vector<float> row(nfft);
                float inv_b = 1.0f / static_cast<float>(current_row_blocks);
                for (int k = 0; k < nfft; ++k) {
                    row[k] = static_cast<float>(current_row_accum[k] * inv_b);
                }
                waterfall_rows.push_back(std::move(row));
                waterfall_timestamps.push_back(
                    waterfall_rows.size() * (blocks_per_waterfall_row * nfft / config_.sample_rate_hz)
                );
                std::fill(current_row_accum.begin(), current_row_accum.end(), 0.0);
                current_row_blocks = 0;
            }
        }

        // Progress notification
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = now - last_progress_time;
        if (diff.count() >= 0.2 || bytes_read_so_far >= bytes_to_process_) {
            last_progress_time = now;
            std::chrono::duration<double> total_el = now - t_start;
            double elapsed_s = total_el.count();
            double pct = std::min(100.0, (static_cast<double>(bytes_read_so_far) / bytes_to_process_) * 100.0);
            double speed_mb = (bytes_read_so_far / (1024.0 * 1024.0)) / std::max(0.001, elapsed_s);
            double eta_s = (bytes_to_process_ - bytes_read_so_far) / std::max(1.0, speed_mb * 1024.0 * 1024.0);

            if (progress_cb) {
                progress_cb(pct, speed_mb, eta_s);
            }
        }
    }

    // Flush final partial waterfall row if needed
    if (current_row_blocks > 0) {
        std::vector<float> row(nfft);
        float inv_b = 1.0f / static_cast<float>(current_row_blocks);
        for (int k = 0; k < nfft; ++k) {
            row[k] = static_cast<float>(current_row_accum[k] * inv_b);
        }
        waterfall_rows.push_back(std::move(row));
        waterfall_timestamps.push_back(duration_to_process_s_);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_elapsed = t_end - t_start;
    res.elapsed_processing_time_s = std::max(0.0001, total_elapsed.count());
    res.throughput_mb_s = (bytes_read_so_far / (1024.0 * 1024.0)) / res.elapsed_processing_time_s;
    res.total_blocks = total_blocks_accumulated;

    if (total_blocks_accumulated == 0) {
        if (is_cancelled()) {
            res.averaged_power.assign(nfft, 0.0);
            return res;
        }
        throw std::runtime_error("No complete FFT blocks could be accumulated from the file.");
    }

    // Normalize accumulated power spectrum
    res.averaged_power.resize(nfft);
    double inv_total = 1.0 / static_cast<double>(total_blocks_accumulated);
    for (int k = 0; k < nfft; ++k) {
        res.averaged_power[k] = power_accumulator[k] * inv_total;
    }

    // Flatten waterfall rows into contiguous row-major buffer
    res.waterfall_rows = static_cast<int>(waterfall_rows.size());
    res.waterfall_cols = nfft;
    res.waterfall_times = std::move(waterfall_timestamps);

    if (res.waterfall_rows > 0) {
        res.waterfall_data.resize(static_cast<size_t>(res.waterfall_rows) * nfft);
        for (int r = 0; r < res.waterfall_rows; ++r) {
            std::memcpy(&res.waterfall_data[static_cast<size_t>(r) * nfft], waterfall_rows[r].data(), nfft * sizeof(float));
        }
    } else {
        // Fallback: 1 row using averaged power
        res.waterfall_rows = 1;
        res.waterfall_data.resize(nfft);
        for (int k = 0; k < nfft; ++k) {
            res.waterfall_data[k] = static_cast<float>(res.averaged_power[k]);
        }
        res.waterfall_times.push_back(duration_to_process_s_);
    }

    return res;
}

} // namespace astro
