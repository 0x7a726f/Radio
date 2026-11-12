#include "virtual_sdr.h"
#include "constants.h"
#include <cmath>
#include <algorithm>
#include <thread>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

VirtualSdrDevice::VirtualSdrDevice()
    : rng_(std::random_device{}()) {
}

bool VirtualSdrDevice::open(uint32_t /*index*/) {
    is_open_ = true;
    sample_counter_ = 0;
    next_chunk_time_ = std::chrono::high_resolution_clock::now();
    return true;
}

void VirtualSdrDevice::close() {
    is_open_ = false;
}

std::vector<float> VirtualSdrDevice::get_valid_gains() {
    return {0.0f, 0.9f, 1.4f, 2.7f, 3.7f, 7.7f, 8.7f, 12.5f, 14.4f, 15.7f, 16.6f,
            19.7f, 20.7f, 22.9f, 25.4f, 28.0f, 29.7f, 32.8f, 33.8f, 36.4f, 37.2f,
            38.6f, 40.2f, 42.1f, 43.4f, 43.9f, 44.5f, 48.0f, 49.6f};
}

bool VirtualSdrDevice::set_sample_rate(uint32_t rate_hz) {
    sample_rate_hz_.store(rate_hz);
    return true;
}

bool VirtualSdrDevice::set_frequency(uint64_t freq_hz) {
    center_freq_hz_.store(freq_hz);
    return true;
}

bool VirtualSdrDevice::set_gain(float gain_db, bool auto_gain) {
    gain_db_.store(gain_db);
    auto_gain_.store(auto_gain);
    return true;
}

bool VirtualSdrDevice::set_bias_tee(bool enabled) {
    bias_tee_.store(enabled);
    return true;
}

bool VirtualSdrDevice::set_ppm(int ppm) {
    ppm_.store(ppm);
    return true;
}

void VirtualSdrDevice::reset_buffer() {
    // No-op for virtual SDR
}

int VirtualSdrDevice::read_samples(uint8_t* buffer, int length, int* bytes_read) {
    if (!is_open_) {
        *bytes_read = 0;
        return -1;
    }

    const int chunk_samples = length / 2;
    const uint32_t fs = sample_rate_hz_.load();
    const uint64_t fc = center_freq_hz_.load();
    const double span = (double)fs;

    // Real-time pacing with drift compensation
    auto now = std::chrono::high_resolution_clock::now();
    if (next_chunk_time_ > now) {
        std::this_thread::sleep_until(next_chunk_time_);
        now = std::chrono::high_resolution_clock::now();
    }
    double chunk_duration_sec = (double)chunk_samples / (double)fs;
    auto chunk_duration = std::chrono::microseconds((int64_t)(chunk_duration_sec * 1000000.0));
    if (next_chunk_time_ + chunk_duration * 2 < now) {
        next_chunk_time_ = now + chunk_duration;
    } else {
        next_chunk_time_ += chunk_duration;
    }

    // Multi-frequency RF simulation with phase continuity
    const double dt = 1.0 / (double)fs;
    const double dphi_ripple = 2.0 * M_PI * 710000.0 * dt;
    const double hi_offset = SdrConstants::HI_REST_FREQ_HZ - (double)fc;
    const double dphi_hi = 2.0 * M_PI * hi_offset * dt;

    static const double fm_stations[] = {
        88.5e6, 89.2e6, 92.6e6, 96.9e6, 98.6e6, 100.3e6, 103.7e6, 105.8e6
    };

    sample_counter_ += chunk_samples;

    for (int n = 0; n < chunk_samples; ++n) {
        // 0. Thermal noise floor
        float ni = noise_dist_(rng_);
        float nq = noise_dist_(rng_);

        float sig_i = 0.0f;
        float sig_q = 0.0f;

        // 1. Hydrogen Line band (~1420 MHz)
        if (std::abs((double)fc - SdrConstants::HI_REST_FREQ_HZ) < span) {
            float h_ni = noise_dist_(rng_) * 1.1f;
            float h_nq = noise_dist_(rng_) * 1.1f;
            hi_phase_ += dphi_hi;
            if (hi_phase_ > M_PI) hi_phase_ -= 2.0 * M_PI;
            else if (hi_phase_ < -M_PI) hi_phase_ += 2.0 * M_PI;

            sig_i += (float)(h_ni * std::cos(hi_phase_) - h_nq * std::sin(hi_phase_));
            sig_q += (float)(h_ni * std::sin(hi_phase_) + h_nq * std::cos(hi_phase_));

            // Coaxial standing wave ripple (~710 kHz)
            ripple_phase_ += dphi_ripple;
            if (ripple_phase_ > M_PI) ripple_phase_ -= 2.0 * M_PI;
            else if (ripple_phase_ < -M_PI) ripple_phase_ += 2.0 * M_PI;

            sig_i += 3.5f * (float)std::sin(ripple_phase_);
            sig_q += 3.5f * (float)std::cos(ripple_phase_);
        }
        // 2. FM Broadcast Band (80 - 115 MHz)
        else if (fc >= 80000000ULL && fc <= 115000000ULL) {
            for (size_t s = 0; s < 8; ++s) {
                double offset = fm_stations[s] - (double)fc;
                if (std::abs(offset) < (span * 0.48)) {
                    double dphi = 2.0 * M_PI * offset * dt;
                    fm_phase_[s] += dphi;
                    if (fm_phase_[s] > M_PI) fm_phase_[s] -= 2.0 * M_PI;
                    else if (fm_phase_[s] < -M_PI) fm_phase_[s] += 2.0 * M_PI;

                    sig_i += 38.0f * (float)std::cos(fm_phase_[s]);
                    sig_q += 38.0f * (float)std::sin(fm_phase_[s]);
                }
            }
        }
        // 3. Aircraft Airband (118 - 137 MHz)
        else if (fc >= 118000000ULL && fc <= 137000000ULL) {
            double offset = 121.5e6 - (double)fc;
            if (std::abs(offset) < (span * 0.48)) {
                double dphi = 2.0 * M_PI * offset * dt;
                am_phase_ += dphi;
                if (am_phase_ > M_PI) am_phase_ -= 2.0 * M_PI;
                else if (am_phase_ < -M_PI) am_phase_ += 2.0 * M_PI;

                sig_i += 24.0f * (float)std::cos(am_phase_);
                sig_q += 24.0f * (float)std::sin(am_phase_);
            }
        }
        // 4. 2-Meter Amateur Band (144 - 148 MHz)
        else if (fc >= 144000000ULL && fc <= 148000000ULL) {
            double offset = 144.8e6 - (double)fc;
            if (std::abs(offset) < (span * 0.48)) {
                double dphi = 2.0 * M_PI * offset * dt;
                ham_phase_ += dphi;
                if (ham_phase_ > M_PI) ham_phase_ -= 2.0 * M_PI;
                else if (ham_phase_ < -M_PI) ham_phase_ += 2.0 * M_PI;

                sig_i += 28.0f * (float)std::cos(ham_phase_);
                sig_q += 28.0f * (float)std::sin(ham_phase_);
            }
        }
        // 5. 70cm / ISM 433 MHz Band
        else if (fc >= 430000000ULL && fc <= 440000000ULL) {
            double offset = 433.92e6 - (double)fc;
            if (std::abs(offset) < (span * 0.48)) {
                double dphi = 2.0 * M_PI * offset * dt;
                ism_phase_ += dphi;
                if (ism_phase_ > M_PI) ism_phase_ -= 2.0 * M_PI;
                else if (ism_phase_ < -M_PI) ism_phase_ += 2.0 * M_PI;

                sig_i += 30.0f * (float)std::cos(ism_phase_);
                sig_q += 30.0f * (float)std::sin(ism_phase_);
            }
        }
        // 6. ADS-B 1090 MHz
        else if (fc >= 1080000000ULL && fc <= 1100000000ULL) {
            double offset = 1090.0e6 - (double)fc;
            if (std::abs(offset) < (span * 0.48)) {
                float pulse = (pulse_dist_(rng_) < 0.03f) ? 45.0f : 0.0f;
                double dphi = 2.0 * M_PI * offset * dt;
                adsb_phase_ += dphi;
                if (adsb_phase_ > M_PI) adsb_phase_ -= 2.0 * M_PI;
                else if (adsb_phase_ < -M_PI) adsb_phase_ += 2.0 * M_PI;

                sig_i += pulse * (float)std::cos(adsb_phase_);
                sig_q += pulse * (float)std::sin(adsb_phase_);
            }
        }
        // 7. Generic test carrier
        else {
            double offset = span * 0.15;
            double dphi = 2.0 * M_PI * offset * dt;
            gen_phase_ += dphi;
            if (gen_phase_ > M_PI) gen_phase_ -= 2.0 * M_PI;
            else if (gen_phase_ < -M_PI) gen_phase_ += 2.0 * M_PI;

            sig_i += 22.0f * (float)std::cos(gen_phase_);
            sig_q += 22.0f * (float)std::sin(gen_phase_);
        }

        // Central DC LO leakage spike
        float dc_spike = 10.0f;

        float total_i = ni + sig_i + dc_spike + 128.0f;
        float total_q = nq + sig_q + 128.0f;

        buffer[2 * n] = (uint8_t)std::clamp((int)std::lround(total_i), 0, 255);
        buffer[2 * n + 1] = (uint8_t)std::clamp((int)std::lround(total_q), 0, 255);
    }

    *bytes_read = chunk_samples * 2;
    return 0;
}
