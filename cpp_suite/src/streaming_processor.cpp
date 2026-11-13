#include "streaming_processor.h"
#include "simd_dsp.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <immintrin.h>
#include "kiss_fft.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace astro {

constexpr double PI = 3.14159265358979323846;

struct KissFftDeleter {
    void operator()(kiss_fft_cfg cfg) const {
        if (cfg) kiss_fft_free(cfg);
    }
};
using KissFftPtr = std::unique_ptr<kiss_fft_state, KissFftDeleter>;

namespace {

inline bool is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

#ifdef __AVX2__
inline void transpose8x8_ps(__m256 r[8]) {
    __m256 t0 = _mm256_unpacklo_ps(r[0], r[1]);
    __m256 t1 = _mm256_unpackhi_ps(r[0], r[1]);
    __m256 t2 = _mm256_unpacklo_ps(r[2], r[3]);
    __m256 t3 = _mm256_unpackhi_ps(r[2], r[3]);
    __m256 t4 = _mm256_unpacklo_ps(r[4], r[5]);
    __m256 t5 = _mm256_unpackhi_ps(r[4], r[5]);
    __m256 t6 = _mm256_unpacklo_ps(r[6], r[7]);
    __m256 t7 = _mm256_unpackhi_ps(r[6], r[7]);

    __m256 u0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 u1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 u2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 u3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 u4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 u5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 u6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 u7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

    r[0] = _mm256_permute2f128_ps(u0, u4, 0x20);
    r[1] = _mm256_permute2f128_ps(u1, u5, 0x20);
    r[2] = _mm256_permute2f128_ps(u2, u6, 0x20);
    r[3] = _mm256_permute2f128_ps(u3, u7, 0x20);
    r[4] = _mm256_permute2f128_ps(u0, u4, 0x31);
    r[5] = _mm256_permute2f128_ps(u1, u5, 0x31);
    r[6] = _mm256_permute2f128_ps(u2, u6, 0x31);
    r[7] = _mm256_permute2f128_ps(u3, u7, 0x31);
}

// Precomputed twiddle tables for radix-2 Stockham forward DFT (stages 0..log2(N)-1)
struct StockhamTwiddles {
    int nfft = 0;
    int stages = 0;
    std::vector<float> wr;
    std::vector<float> wi;
    std::vector<int> stage_offset;

    void init(int n) {
        nfft = n;
        stages = 0;
        for (int s = 1; s < n; s <<= 1) stages++;
        wr.resize(static_cast<size_t>(n));
        wi.resize(static_cast<size_t>(n));
        stage_offset.resize(stages);

        int off = 0;
        for (int st = 0; st < stages; ++st) {
            int half = 1 << st;
            int full = half << 1;
            stage_offset[st] = off;
            const double angle_step = -2.0 * PI / static_cast<double>(full);
            for (int j = 0; j < half; ++j) {
                double ang = angle_step * static_cast<double>(j);
                wr[off + j] = static_cast<float>(std::cos(ang));
                wi[off + j] = static_cast<float>(std::sin(ang));
            }
            off += half;
        }
    }
};

struct AlignedBuffer32 {
    std::vector<uint8_t> raw;
    float* ptr = nullptr;

    void resize_floats(size_t count) {
        raw.resize(count * sizeof(float) + 32);
        uintptr_t addr = reinterpret_cast<uintptr_t>(raw.data());
        uintptr_t aligned = (addr + 31u) & ~uintptr_t(31u);
        ptr = reinterpret_cast<float*>(aligned);
    }
};

// Executes 8 forward complex FFTs simultaneously using SoA AVX2 Stockham radix-2,
// writes unshifted linear power to out_blocks[b * nfft + k] (b=0..7) and
// accumulates double-precision bin totals into total_accum[0..nfft-1].
inline void batch8_fft_power_avx2(
    const int16_t* raw_base,
    int nfft,
    const float* win,
    const StockhamTwiddles& tw,
    __m256* a_re, __m256* a_im,
    __m256* b_re, __m256* b_im,
    float* out_blocks,
    double* total_accum
) {
    const int block_stride_i16 = nfft * 2;

    // 1. Load 8 CS16 blocks + multiply by window + transpose into SoA a_re/a_im
    for (int k = 0; k < nfft; k += 4) {
        const __m256 w = _mm256_set_ps(
            win[k + 3], win[k + 3], win[k + 2], win[k + 2],
            win[k + 1], win[k + 1], win[k + 0], win[k + 0]
        );
        __m256 r[8];
        for (int b = 0; b < 8; ++b) {
            const int16_t* src = raw_base + static_cast<size_t>(b) * block_stride_i16 + 2 * k;
            __m128i u = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
            r[b] = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(u)), w);
        }
        transpose8x8_ps(r);
        a_re[k + 0] = r[0]; a_im[k + 0] = r[1];
        a_re[k + 1] = r[2]; a_im[k + 1] = r[3];
        a_re[k + 2] = r[4]; a_im[k + 2] = r[5];
        a_re[k + 3] = r[6]; a_im[k + 3] = r[7];
    }

    // 2. Stockham radix-2 FFT stages across 8 blocks simultaneously (zero cross-lane shuffles)
    __m256* in_re = a_re;
    __m256* in_im = a_im;
    __m256* out_re = b_re;
    __m256* out_im = b_im;

    const int half_n = nfft >> 1;
    for (int st = 0; st < tw.stages; ++st) {
        const int half = 1 << st;
        const int full = half << 1;
        const int blocks = nfft >> (st + 1);
        const float* tw_r = tw.wr.data() + tw.stage_offset[st];
        const float* tw_i = tw.wi.data() + tw.stage_offset[st];

        for (int bk = 0; bk < blocks; ++bk) {
            const int in_off = bk * half;
            const int out_off = bk * full;
            const __m256* src_r0 = in_re + in_off;
            const __m256* src_i0 = in_im + in_off;
            const __m256* src_r1 = src_r0 + half_n;
            const __m256* src_i1 = src_i0 + half_n;
            __m256* dst_r0 = out_re + out_off;
            __m256* dst_i0 = out_im + out_off;
            __m256* dst_r1 = dst_r0 + half;
            __m256* dst_i1 = dst_i0 + half;

            for (int j = 0; j < half; ++j) {
                const __m256 wr = _mm256_set1_ps(tw_r[j]);
                const __m256 wi = _mm256_set1_ps(tw_i[j]);
                const __m256 ar = src_r0[j];
                const __m256 ai = src_i0[j];
                const __m256 br = src_r1[j];
                const __m256 bi = src_i1[j];

#if SIMD_HAS_FMA
                const __m256 tr = _mm256_fmsub_ps(wr, br, _mm256_mul_ps(wi, bi));
                const __m256 ti = _mm256_fmadd_ps(wr, bi, _mm256_mul_ps(wi, br));
#else
                const __m256 tr = _mm256_sub_ps(_mm256_mul_ps(wr, br), _mm256_mul_ps(wi, bi));
                const __m256 ti = _mm256_add_ps(_mm256_mul_ps(wr, bi), _mm256_mul_ps(wi, br));
#endif
                dst_r0[j] = _mm256_add_ps(ar, tr);
                dst_i0[j] = _mm256_add_ps(ai, ti);
                dst_r1[j] = _mm256_sub_ps(ar, tr);
                dst_i1[j] = _mm256_sub_ps(ai, ti);
            }
        }
        std::swap(in_re, out_re);
        std::swap(in_im, out_im);
    }

    // 3. Magnitude squared + 8x8 transpose to AoS power + vector total accumulation
    for (int k = 0; k < nfft; k += 8) {
        __m256 p[8];
        for (int sub = 0; sub < 8; ++sub) {
            __m256 re = in_re[k + sub];
            __m256 im = in_im[k + sub];
#if SIMD_HAS_FMA
            p[sub] = _mm256_fmadd_ps(re, re, _mm256_mul_ps(im, im));
#else
            p[sub] = _mm256_add_ps(_mm256_mul_ps(re, re), _mm256_mul_ps(im, im));
#endif
        }
        transpose8x8_ps(p);

        __m256 s01 = _mm256_add_ps(p[0], p[1]);
        __m256 s23 = _mm256_add_ps(p[2], p[3]);
        __m256 s45 = _mm256_add_ps(p[4], p[5]);
        __m256 s67 = _mm256_add_ps(p[6], p[7]);
        __m256 s8 = _mm256_add_ps(_mm256_add_ps(s01, s23), _mm256_add_ps(s45, s67));

        __m256d lo_d = _mm256_cvtps_pd(_mm256_castps256_ps128(s8));
        __m256d hi_d = _mm256_cvtps_pd(_mm256_extractf128_ps(s8, 1));
        _mm256_storeu_pd(total_accum + k + 0, _mm256_add_pd(_mm256_loadu_pd(total_accum + k + 0), lo_d));
        _mm256_storeu_pd(total_accum + k + 4, _mm256_add_pd(_mm256_loadu_pd(total_accum + k + 4), hi_d));

        for (int b = 0; b < 8; ++b) {
            _mm256_storeu_ps(out_blocks + static_cast<size_t>(b) * nfft + k, p[b]);
        }
    }
}
#endif

inline void accumulate_double_from_float(double* __restrict dst, const float* __restrict src, int n) {
    int k = 0;
#ifdef __AVX2__
    for (; k + 8 <= n; k += 8) {
        __m256 s = _mm256_loadu_ps(src + k);
        __m256d lo = _mm256_cvtps_pd(_mm256_castps256_ps128(s));
        __m256d hi = _mm256_cvtps_pd(_mm256_extractf128_ps(s, 1));
        _mm256_storeu_pd(dst + k + 0, _mm256_add_pd(_mm256_loadu_pd(dst + k + 0), lo));
        _mm256_storeu_pd(dst + k + 4, _mm256_add_pd(_mm256_loadu_pd(dst + k + 4), hi));
    }
#endif
    for (; k < n; ++k) {
        dst[k] += static_cast<double>(src[k]);
    }
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring res(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &res[0], size);
    return res;
}
#endif

} // namespace

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
    const int shift_first = nfft - half_nfft;
    std::vector<float> window = generate_window(config_.window, nfft);

    // Pre-interleave window [w0, w0, w1, w1, ...] for AVX2 non-batched path
    std::vector<float> window_interleaved(static_cast<size_t>(nfft) * 2);
    for (int i = 0; i < nfft; ++i) {
        window_interleaved[2 * i] = window[i];
        window_interleaved[2 * i + 1] = window[i];
    }

    // Prepare frequency axes
    res.freq_hz.resize(nfft);
    res.freq_mhz.resize(nfft);
    const double bin_res = config_.sample_rate_hz / static_cast<double>(nfft);
    for (int i = 0; i < nfft; ++i) {
        double freq_rel = (i - half_nfft) * bin_res;
        double f_abs = freq_rel + config_.center_freq_hz;
        res.freq_hz[i] = f_abs;
        res.freq_mhz[i] = f_abs / 1e6;
    }

    // Number of blocks per streaming batch (~16 MB RAM buffer max)
    const int chunk_blocks = std::max(64, config_.chunk_blocks);
    const int chunk_samples = chunk_blocks * nfft;
    const size_t block_bytes = static_cast<size_t>(nfft) * 4;
    const size_t raw_buffer_bytes = static_cast<size_t>(chunk_samples) * 4;

    // Waterfall decimation parameters
    double waterfall_dt = std::max(0.05, config_.waterfall_dt_s);
    int blocks_per_waterfall_row = std::max(1, static_cast<int>(waterfall_dt * config_.sample_rate_hz / nfft));

    std::vector<double> current_row_accum(nfft, 0.0);
    int current_row_blocks = 0;

    size_t est_wf_rows = (bytes_to_process_ / 4) / (static_cast<size_t>(blocks_per_waterfall_row) * nfft) + 16;
    std::vector<float> flat_waterfall;
    flat_waterfall.reserve(est_wf_rows * static_cast<size_t>(nfft));
    std::vector<double> waterfall_timestamps;
    waterfall_timestamps.reserve(est_wf_rows);

    // Worker pool configuration
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    num_threads = std::min(num_threads, 8u);

    const bool can_use_avx2_fft =
#ifdef __AVX2__
        (SimdDsp::detect_cpu_simd() == SimdDsp::CpuSimdLevel::AVX2) && is_power_of_two(nfft) && nfft >= 16;
#else
        false;
#endif

#ifdef __AVX2__
    StockhamTwiddles twiddles;
    if (can_use_avx2_fft) {
        twiddles.init(nfft);
    }
#endif

    std::vector<KissFftPtr> fft_configs(num_threads);
    for (unsigned int t = 0; t < num_threads; ++t) {
        fft_configs[t].reset(kiss_fft_alloc(nfft, 0, nullptr, nullptr));
        if (!fft_configs[t]) {
            throw std::runtime_error("Failed to allocate KissFFT configuration for thread " + std::to_string(t));
        }
    }

    // Per-thread workspaces (allocated once)
    std::vector<std::vector<kiss_fft_cpx>> thread_cx_in(num_threads, std::vector<kiss_fft_cpx>(nfft));
    std::vector<std::vector<kiss_fft_cpx>> thread_cx_out(num_threads, std::vector<kiss_fft_cpx>(nfft));
    std::vector<std::vector<double>> worker_power_sum(num_threads, std::vector<double>(nfft, 0.0));
#ifdef __AVX2__
    std::vector<AlignedBuffer32> worker_soa(num_threads);
    if (can_use_avx2_fft) {
        // Need 4 arrays of size nfft * 8 floats: a_re, a_im, b_re, b_im
        for (unsigned int t = 0; t < num_threads; ++t) {
            worker_soa[t].resize_floats(static_cast<size_t>(nfft) * 8 * 4);
        }
    }
#endif

    // Validate file opens before spawning background reader
#ifdef _WIN32
    HANDLE h_file = CreateFileW(
        utf8_to_wide(filepath_).c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    if (h_file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open IQ file for reading: " + filepath_);
    }
#else
    std::ifstream file(filepath_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open IQ file for reading: " + filepath_);
    }
#endif

    // Double-buffered async reader slot state
    struct ReadSlot {
        std::vector<int16_t> buf;
        int blocks = 0;
        uint64_t bytes_consumed = 0;
        bool ready = false;
        bool eof = false;
        std::string error_msg;
    };
    ReadSlot slots[2];
    slots[0].buf.resize(static_cast<size_t>(chunk_samples) * 2);
    slots[1].buf.resize(static_cast<size_t>(chunk_samples) * 2);

    std::mutex io_mutex;
    std::condition_variable io_cv;
    bool io_stop = false;

    std::thread reader_thread([&]() {
        int w_idx = 0;
        uint64_t bytes_fed = 0;
        std::vector<uint8_t> carryover;
        carryover.reserve(block_bytes);

        while (!io_stop && !is_cancelled()) {
            {
                std::unique_lock<std::mutex> lock(io_mutex);
                io_cv.wait(lock, [&]() { return !slots[w_idx].ready || io_stop || is_cancelled(); });
                if (io_stop || is_cancelled()) break;
            }

            ReadSlot& s = slots[w_idx];
            if (bytes_fed >= bytes_to_process_) {
                std::lock_guard<std::mutex> lock(io_mutex);
                s.blocks = 0;
                s.bytes_consumed = 0;
                s.eof = true;
                s.ready = true;
                io_cv.notify_all();
                break;
            }

            uint8_t* dst = reinterpret_cast<uint8_t*>(s.buf.data());
            size_t carry_sz = carryover.size();
            if (carry_sz > 0) {
                std::memcpy(dst, carryover.data(), carry_sz);
                carryover.clear();
            }

            uint64_t rem_target = bytes_to_process_ - bytes_fed;
            size_t space = raw_buffer_bytes - carry_sz;
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(space, rem_target));
            size_t got = 0;

#ifdef _WIN32
            while (got < to_read) {
                DWORD chunk = static_cast<DWORD>(std::min<size_t>(to_read - got, 64ULL * 1024ULL * 1024ULL));
                DWORD br = 0;
                if (!ReadFile(h_file, dst + carry_sz + got, chunk, &br, nullptr) || br == 0) {
                    break;
                }
                got += br;
            }
#else
            file.read(reinterpret_cast<char*>(dst + carry_sz), static_cast<std::streamsize>(to_read));
            std::streamsize gr = file.gcount();
            if (gr > 0) got = static_cast<size_t>(gr);
#endif
            bytes_fed += got;
            size_t total_avail = carry_sz + got;
            int blocks = static_cast<int>(total_avail / block_bytes);
            size_t valid_bytes = static_cast<size_t>(blocks) * block_bytes;
            size_t rem_bytes = total_avail - valid_bytes;
            if (rem_bytes > 0) {
                carryover.assign(dst + valid_bytes, dst + total_avail);
            }

            std::lock_guard<std::mutex> lock(io_mutex);
            s.blocks = blocks;
            s.bytes_consumed = valid_bytes;
            s.eof = (got == 0 && blocks == 0);
            s.ready = true;
            io_cv.notify_all();

            if (s.eof) break;
            w_idx ^= 1;
        }
    });

    // Persistent worker pool with RAII teardown
    std::vector<float> thread_power_blocks(static_cast<size_t>(chunk_blocks) * nfft);
    const int16_t* active_raw_buf = nullptr;
    int active_blocks = 0;

    std::mutex pool_mutex;
    std::condition_variable pool_cv_work;
    std::condition_variable pool_cv_done;
    uint64_t job_gen = 0;
    unsigned int workers_active = 0;
    bool pool_stop = false;

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (unsigned int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            uint64_t seen_gen = 0;
            while (true) {
                int start_b = 0;
                int end_b = 0;
                {
                    std::unique_lock<std::mutex> lock(pool_mutex);
                    pool_cv_work.wait(lock, [&]() { return job_gen != seen_gen || pool_stop; });
                    if (pool_stop) break;
                    seen_gen = job_gen;
                    int blocks_per_thread = (active_blocks + static_cast<int>(num_threads) - 1) / static_cast<int>(num_threads);
                    start_b = static_cast<int>(t) * blocks_per_thread;
                    end_b = std::min(active_blocks, start_b + blocks_per_thread);
                }

                if (start_b < end_b) {
                    int b = start_b;
                    double* p_sum = worker_power_sum[t].data();

#ifdef __AVX2__
                    if (can_use_avx2_fft) {
                        __m256* a_re = reinterpret_cast<__m256*>(worker_soa[t].ptr);
                        __m256* a_im = a_re + nfft;
                        __m256* b_re = a_im + nfft;
                        __m256* b_im = b_re + nfft;
                        for (; b + 8 <= end_b; b += 8) {
                            batch8_fft_power_avx2(
                                &active_raw_buf[static_cast<size_t>(b) * nfft * 2],
                                nfft,
                                window.data(),
                                twiddles,
                                a_re, a_im, b_re, b_im,
                                &thread_power_blocks[static_cast<size_t>(b) * nfft],
                                p_sum
                            );
                        }
                    }
#endif

                    kiss_fft_cfg cfg = fft_configs[t].get();
                    kiss_fft_cpx* p_cx_in = thread_cx_in[t].data();
                    kiss_fft_cpx* p_cx_out = thread_cx_out[t].data();
                    float* p_cx_in_f = reinterpret_cast<float*>(p_cx_in);

                    for (; b < end_b; ++b) {
                        const int16_t* p_raw = &active_raw_buf[static_cast<size_t>(b) * nfft * 2];
                        SimdDsp::convert_cs16_window_float_fast(p_raw, window_interleaved.data(), p_cx_in_f, nfft);
                        kiss_fft(cfg, p_cx_in, p_cx_out);

                        float* p_out = &thread_power_blocks[static_cast<size_t>(b) * nfft];
                        for (int k = 0; k < nfft; ++k) {
                            float p = p_cx_out[k].r * p_cx_out[k].r + p_cx_out[k].i * p_cx_out[k].i;
                            p_out[k] = p;
                        }
                        accumulate_double_from_float(p_sum, p_out, nfft);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(pool_mutex);
                    if (--workers_active == 0) {
                        pool_cv_done.notify_one();
                    }
                }
            }
        });
    }

    struct CleanupGuard {
        HANDLE* win_file;
        std::ifstream* cpp_file;
        std::mutex& io_m;
        std::condition_variable& io_cv;
        bool& io_stop;
        std::thread& reader;
        std::mutex& p_m;
        std::condition_variable& p_cv;
        bool& p_stop;
        std::vector<std::thread>& workers;

        ~CleanupGuard() {
            {
                std::lock_guard<std::mutex> lock(io_m);
                io_stop = true;
            }
            io_cv.notify_all();
            if (reader.joinable()) reader.join();

            {
                std::lock_guard<std::mutex> lock(p_m);
                p_stop = true;
            }
            p_cv.notify_all();
            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }
#ifdef _WIN32
            if (win_file && *win_file != INVALID_HANDLE_VALUE) {
                CloseHandle(*win_file);
                *win_file = INVALID_HANDLE_VALUE;
            }
#else
            if (cpp_file && cpp_file->is_open()) cpp_file->close();
#endif
        }
    } cleanup{
#ifdef _WIN32
        &h_file, nullptr,
#else
        nullptr, &file,
#endif
        io_mutex, io_cv, io_stop, reader_thread,
        pool_mutex, pool_cv_work, pool_stop, workers
    };

    auto append_shifted_waterfall_row = [&](int row_blocks, double timestamp_s) {
        size_t old_sz = flat_waterfall.size();
        flat_waterfall.resize(old_sz + static_cast<size_t>(nfft));
        float* dst = flat_waterfall.data() + old_sz;
        const float inv_b = 1.0f / static_cast<float>(row_blocks);
        for (int k = 0; k < shift_first; ++k) {
            dst[k] = static_cast<float>(current_row_accum[k + half_nfft]) * inv_b;
        }
        for (int k = 0; k < half_nfft; ++k) {
            dst[shift_first + k] = static_cast<float>(current_row_accum[k]) * inv_b;
        }
        waterfall_timestamps.push_back(timestamp_s);
    };

    uint64_t bytes_read_so_far = 0;
    uint64_t total_blocks_accumulated = 0;
    int r_idx = 0;

    auto t_start = std::chrono::high_resolution_clock::now();
    auto last_progress_time = t_start;

    while (!is_cancelled()) {
        int blocks = 0;
        uint64_t slot_bytes = 0;
        bool slot_eof = false;
        {
            std::unique_lock<std::mutex> lock(io_mutex);
            io_cv.wait(lock, [&]() { return slots[r_idx].ready || is_cancelled(); });
            if (is_cancelled()) break;
            blocks = slots[r_idx].blocks;
            slot_bytes = slots[r_idx].bytes_consumed;
            slot_eof = slots[r_idx].eof;
        }

        if (slot_eof || blocks <= 0) {
            std::lock_guard<std::mutex> lock(io_mutex);
            slots[r_idx].ready = false;
            io_cv.notify_all();
            break;
        }

        bytes_read_so_far += slot_bytes;

        // Dispatch persistent workers on current buffer
        {
            std::unique_lock<std::mutex> lock(pool_mutex);
            active_raw_buf = slots[r_idx].buf.data();
            active_blocks = blocks;
            workers_active = num_threads;
            ++job_gen;
            pool_cv_work.notify_all();
            pool_cv_done.wait(lock, [&]() { return workers_active == 0; });
        }

        // Release slot back to background reader immediately
        {
            std::lock_guard<std::mutex> lock(io_mutex);
            slots[r_idx].ready = false;
            io_cv.notify_all();
        }
        r_idx ^= 1;

        // Accumulate waterfall rows in AVX2 (total power already accumulated per worker thread)
        for (int b = 0; b < blocks; ++b) {
            const float* p_blk = &thread_power_blocks[static_cast<size_t>(b) * nfft];
            accumulate_double_from_float(current_row_accum.data(), p_blk, nfft);
            total_blocks_accumulated++;
            current_row_blocks++;

            if (current_row_blocks >= blocks_per_waterfall_row) {
                double ts = static_cast<double>(waterfall_timestamps.size() + 1)
                          * (static_cast<double>(blocks_per_waterfall_row) * static_cast<double>(nfft) / config_.sample_rate_hz);
                append_shifted_waterfall_row(current_row_blocks, ts);
                std::fill(current_row_accum.begin(), current_row_accum.end(), 0.0);
                current_row_blocks = 0;
            }
        }

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
        append_shifted_waterfall_row(current_row_blocks, duration_to_process_s_);
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

    // Reduce worker thread accumulators & perform single deferred FFT-shift
    std::vector<double> unshifted_total(nfft, 0.0);
    for (unsigned int t = 0; t < num_threads; ++t) {
        const double* wt = worker_power_sum[t].data();
        for (int k = 0; k < nfft; ++k) {
            unshifted_total[k] += wt[k];
        }
    }

    res.averaged_power.resize(nfft);
    const double inv_total = 1.0 / static_cast<double>(total_blocks_accumulated);
    for (int k = 0; k < shift_first; ++k) {
        res.averaged_power[k] = unshifted_total[k + half_nfft] * inv_total;
    }
    for (int k = 0; k < half_nfft; ++k) {
        res.averaged_power[shift_first + k] = unshifted_total[k] * inv_total;
    }

    res.waterfall_rows = static_cast<int>(waterfall_timestamps.size());
    res.waterfall_cols = nfft;
    res.waterfall_times = std::move(waterfall_timestamps);

    if (res.waterfall_rows > 0) {
        res.waterfall_data = std::move(flat_waterfall);
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
