#include "dsp_pipeline.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

DspPipeline::DspPipeline() {
    rebuild_plans();
}

DspPipeline::~DspPipeline() {
    if (fft_cfg_) {
        free(fft_cfg_);
        fft_cfg_ = nullptr;
    }
}

void DspPipeline::set_fft_size(int size) {
    if (size != fft_size_) {
        std::lock_guard<std::mutex> lock(sample_mutex_);
        fft_size_ = size;
        rebuild_plans();
        sample_buffer_.clear();
        has_new_samples_ = false;
        flush();
    }
}

void DspPipeline::set_averaging(float factor) {
    avg_factor_ = std::clamp(factor, 0.0f, 0.95f);
}

void DspPipeline::flush() {
    need_flush_.store(true);
}

void DspPipeline::rebuild_plans() {
    if (fft_cfg_) {
        free(fft_cfg_);
        fft_cfg_ = nullptr;
    }

    fft_cfg_ = kiss_fft_alloc(fft_size_, 0, nullptr, nullptr);
    fft_in_.resize(fft_size_);
    fft_out_.resize(fft_size_);
    power_raw_.resize(fft_size_);
    power_smooth_.assign(fft_size_, -120.0f);
    scratch_u8_.resize(static_cast<size_t>(fft_size_) * 2);
    has_smooth_ = false;

    update_window();
}

void DspPipeline::update_window() {
    window_scaled_.resize(fft_size_);
    const double inv_n_minus_1 = 2.0 * M_PI / static_cast<double>(fft_size_ - 1);
    const float inv_128 = 1.0f / 128.0f;

    for (int n = 0; n < fft_size_; ++n) {
        // Hann window: 0.5 * (1 - cos(2*pi*n / (N-1)))
        float w = 0.5f * (1.0f - static_cast<float>(std::cos(static_cast<double>(n) * inv_n_minus_1)));
        // Pre-scale by 1/128 for direct AVX multiplication with (u8 - 127.5)
        window_scaled_[n] = w * inv_128;
    }
}

void DspPipeline::feed_samples(const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(sample_mutex_);
    size_t needed_bytes = static_cast<size_t>(fft_size_) * 2;
    if (length >= needed_bytes) {
        sample_buffer_.assign(data, data + needed_bytes);
        has_new_samples_ = true;
    }
}

void DspPipeline::compute_telemetry(const std::vector<float>& power, float peak_db) {
    if (power.empty()) return;

    // Estimate noise floor by taking the 20th-percentile or median of bins away from center
    // We sample every 16th bin for sub-microsecond execution
    std::vector<float> samples;
    samples.reserve(power.size() / 16 + 1);
    for (size_t i = 0; i < power.size(); i += 16) {
        samples.push_back(power[i]);
    }
    std::nth_element(samples.begin(), samples.begin() + samples.size() / 4, samples.end());
    float noise_floor = samples[samples.size() / 4];

    noise_floor_db_.store(noise_floor, std::memory_order_relaxed);
    float snr = std::max(0.0f, peak_db - noise_floor);
    snr_db_.store(snr, std::memory_order_relaxed);
}

bool DspPipeline::get_power_spectrum(std::vector<float>& out_power, float& out_peak_freq, float& out_peak_db,
                                    uint64_t center_freq_hz, uint32_t sample_rate_hz) {
    const size_t needed_bytes = static_cast<size_t>(fft_size_) * 2;
    {
        std::lock_guard<std::mutex> lock(sample_mutex_);
        if (!has_new_samples_ || sample_buffer_.size() < needed_bytes) {
            return false;
        }
        if (scratch_u8_.size() < needed_bytes) {
            scratch_u8_.resize(needed_bytes);
        }
        std::memcpy(scratch_u8_.data(), sample_buffer_.data(), needed_bytes);
        has_new_samples_ = false;
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    const int n_pts = fft_size_;
    const float inv_n = 1.0f / static_cast<float>(n_pts);

    // 1. Vectorized Normalization & Hann Windowing (AVX2 / SSE2)
    SimdDsp::apply_hann_window_complex_fast(
        scratch_u8_.data(),
        window_scaled_.data(),
        reinterpret_cast<float*>(fft_in_.data()),
        n_pts
    );

    // 2. High-speed complex FFT (KissFFT)
    kiss_fft(fft_cfg_, fft_in_.data(), fft_out_.data());

    // 3. Vectorized FFT Shift & Fast dBFS (AVX2 + FMA / Fast Polynomial log10)
    SimdDsp::compute_power_dbfs_avx2(
        reinterpret_cast<const float*>(fft_out_.data()),
        power_raw_.data(),
        n_pts,
        inv_n
    );

    // 4. Vectorized Exponential Smoothing
    bool do_flush = need_flush_.exchange(false);
    if (do_flush || !has_smooth_) {
        std::memcpy(power_smooth_.data(), power_raw_.data(), n_pts * sizeof(float));
    } else {
        SimdDsp::apply_smoothing_avx2(power_smooth_.data(), power_raw_.data(), n_pts, avg_factor_);
    }
    has_smooth_ = true;

    // 5. Peak Detection
    int peak_idx = 0;
    float max_pwr = -999.0f;
    for (int k = 0; k < n_pts; ++k) {
        if (power_smooth_[k] > max_pwr) {
            max_pwr = power_smooth_[k];
            peak_idx = k;
        }
    }

    out_power = power_smooth_;
    out_peak_db = max_pwr;

    double start_f = static_cast<double>(center_freq_hz) - (static_cast<double>(sample_rate_hz) / 2.0);
    double step_f = static_cast<double>(sample_rate_hz) / static_cast<double>(n_pts);
    out_peak_freq = static_cast<float>(start_f + static_cast<double>(peak_idx) * step_f);

    // Compute Telemetry (SNR, Noise floor)
    compute_telemetry(power_smooth_, max_pwr);

    // Measure latency & throughput
    auto t_end = std::chrono::high_resolution_clock::now();
    double latency_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    last_latency_us_.store(latency_us, std::memory_order_relaxed);

    if (latency_us > 0.0) {
        double msps = (static_cast<double>(n_pts) / (latency_us * 1e-6)) / 1e6;
        dsp_throughput_msps_.store(msps, std::memory_order_relaxed);
    }

    return true;
}
