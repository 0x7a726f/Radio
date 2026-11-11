#pragma once

#include "sdr_device.h"
#include <random>
#include <chrono>
#include <atomic>

class VirtualSdrDevice : public ISdrDevice {
public:
    VirtualSdrDevice();
    ~VirtualSdrDevice() override = default;

    bool open(uint32_t index) override;
    void close() override;
    bool is_open() const override { return is_open_; }
    bool is_simulated() const override { return true; }

    std::string get_name() const override { return "Virtual SDR (Simulation Mode)"; }
    std::string get_status_detail() const override { return "Virtual SDR Active (Multi-band RF simulation)"; }
    std::vector<float> get_valid_gains() override;

    bool set_sample_rate(uint32_t rate_hz) override;
    uint32_t get_sample_rate() const override { return sample_rate_hz_.load(); }

    bool set_frequency(uint64_t freq_hz) override;
    uint64_t get_frequency() const override { return center_freq_hz_.load(); }

    bool set_gain(float gain_db, bool auto_gain) override;
    float get_gain() const override { return gain_db_.load(); }
    bool is_auto_gain() const override { return auto_gain_.load(); }

    bool set_bias_tee(bool enabled) override;
    bool get_bias_tee() const override { return bias_tee_.load(); }

    bool set_ppm(int ppm) override;
    int get_ppm() const override { return ppm_.load(); }

    int read_samples(uint8_t* buffer, int length, int* bytes_read) override;
    void reset_buffer() override;

private:
    std::atomic<bool> is_open_{false};
    std::atomic<uint32_t> sample_rate_hz_{2400000};
    std::atomic<uint64_t> center_freq_hz_{1420500000};
    std::atomic<float> gain_db_{25.4f};
    std::atomic<bool> auto_gain_{false};
    std::atomic<bool> bias_tee_{false};
    std::atomic<int> ppm_{0};

    uint64_t sample_counter_ = 0;
    std::mt19937 rng_;
    std::normal_distribution<float> noise_dist_{0.0f, 14.0f};
    std::uniform_real_distribution<float> pulse_dist_{0.0f, 1.0f};

    // Phase accumulators for seamless phase continuity across tuning
    double hi_phase_ = 0.0;
    double ripple_phase_ = 0.0;
    double fm_phase_[8] = {0.0};
    double am_phase_ = 0.0;
    double ham_phase_ = 0.0;
    double ism_phase_ = 0.0;
    double adsb_phase_ = 0.0;
    double gen_phase_ = 0.0;

    std::chrono::high_resolution_clock::time_point next_chunk_time_;
};
