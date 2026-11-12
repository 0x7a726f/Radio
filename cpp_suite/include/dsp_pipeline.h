#pragma once

#include "constants.h"
#include "simd_dsp.h"
#include "kiss_fft.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <cmath>

class DspPipeline {
public:
    DspPipeline();
    ~DspPipeline();

    void set_fft_size(int size);
    int get_fft_size() const { return fft_size_; }

    void set_averaging(float factor);
    float get_averaging() const { return avg_factor_; }

    // Flush the smoothing buffer (called on frequency change to remove ghost peaks)
    void flush();

    // Ingest new raw uint8 I/Q data from SDR thread
    void feed_samples(const uint8_t* data, size_t length);

    // Compute FFT & smoothing and populate output buffer for GUI
    // Returns true if new spectrum was calculated
    bool get_power_spectrum(std::vector<float>& out_power, float& out_peak_freq, float& out_peak_db,
                            uint64_t center_freq_hz, uint32_t sample_rate_hz);

    // Live Telemetry Accessors
    float get_noise_floor_db() const { return noise_floor_db_.load(std::memory_order_relaxed); }
    float get_snr_db() const { return snr_db_.load(std::memory_order_relaxed); }
    double get_last_latency_us() const { return last_latency_us_.load(std::memory_order_relaxed); }
    double get_dsp_throughput_msps() const { return dsp_throughput_msps_.load(std::memory_order_relaxed); }

private:
    void rebuild_plans();
    void update_window();
    void compute_telemetry(const std::vector<float>& power, float peak_db);

    int fft_size_ = SdrConstants::DEFAULT_FFT_SIZE;
    float avg_factor_ = SdrConstants::DEFAULT_AVG_FACTOR;
    std::atomic<bool> need_flush_{true};

    // Aligned window coefficients (scaled by 1/128 for direct AVX multiplication)
    std::vector<float> window_scaled_;
    std::vector<kiss_fft_cpx> fft_in_;
    std::vector<kiss_fft_cpx> fft_out_;
    kiss_fft_cfg fft_cfg_ = nullptr;

    std::vector<float> power_raw_;
    std::vector<float> power_smooth_;
    bool has_smooth_ = false;

    // Telemetry state
    std::atomic<float> noise_floor_db_{-110.0f};
    std::atomic<float> snr_db_{0.0f};
    std::atomic<double> last_latency_us_{0.0};
    std::atomic<double> dsp_throughput_msps_{0.0};

    // Thread-safe sample ingestion
    std::mutex sample_mutex_;
    std::vector<uint8_t> sample_buffer_;
    std::vector<uint8_t> scratch_u8_;
    bool has_new_samples_ = false;
};
