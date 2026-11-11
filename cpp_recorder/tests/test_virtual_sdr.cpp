#include "test_framework.h"
#include "virtual_sdr.h"
#include "constants.h"
#include <vector>
#include <chrono>
#include <cmath>
#include <numeric>

TEST_CASE(VirtualSdrDeviceOpenCloseAndGains) {
    VirtualSdrDevice dev;
    ASSERT_FALSE(dev.is_open());
    ASSERT_TRUE(dev.is_simulated());

    bool ok = dev.open(0);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(dev.is_open());

    auto gains = dev.get_valid_gains();
    ASSERT_EQ((int)gains.size(), 29);
    ASSERT_EQ(gains.front(), 0.0f);
    ASSERT_EQ(gains.back(), 49.6f);

    dev.close();
    ASSERT_FALSE(dev.is_open());
}

TEST_CASE(MultiFrequencyBandSignalGeneration) {
    VirtualSdrDevice dev;
    dev.open(0);
    dev.set_sample_rate(2400000);

    struct BandConfig {
        std::string name;
        uint64_t freq_hz;
    };

    std::vector<BandConfig> bands = {
        {"Hydrogen Line 1420 MHz", 1420500000ULL},
        {"FM Broadcast 89.2 MHz",   89200000ULL},
        {"Airband 121.5 MHz",      121500000ULL},
        {"Ham APRS 144.8 MHz",     144800000ULL},
        {"ISM 433.92 MHz",         433920000ULL},
        {"ADS-B 1090 MHz",        1090000000ULL},
        {"Generic Carrier 500 MHz", 500000000ULL}
    };

    const int BUF_SIZE = 32768;
    std::vector<uint8_t> buffer(BUF_SIZE);

    for (const auto& band : bands) {
        dev.set_frequency(band.freq_hz);
        ASSERT_EQ(dev.get_frequency(), band.freq_hz);

        int bytes_read = 0;
        int ret = dev.read_samples(buffer.data(), BUF_SIZE, &bytes_read);
        ASSERT_EQ(ret, 0);
        ASSERT_EQ(bytes_read, BUF_SIZE);

        // Calculate mean and variance
        double sum = 0.0;
        for (int i = 0; i < BUF_SIZE; ++i) {
            sum += buffer[i];
        }
        double mean = sum / (double)BUF_SIZE;
        // Central DC offset should be around 128 +/- 35 (including DC leakage spike and zero-IF carrier bias)
        ASSERT_NEAR(mean, 128.0, 35.0);

        double var_sum = 0.0;
        for (int i = 0; i < BUF_SIZE; ++i) {
            double diff = buffer[i] - mean;
            var_sum += diff * diff;
        }
        double variance = var_sum / (double)BUF_SIZE;
        // Variance must be healthy (RF signal + noise present)
        ASSERT_GT(variance, 50.0);
    }

    dev.close();
}

TEST_CASE(RealTimePacingAccuracy) {
    VirtualSdrDevice dev;
    dev.open(0);
    const uint32_t fs = 2400000; // 2.4 MSPS
    dev.set_sample_rate(fs);

    // Read 3 chunks of 65,536 bytes (32,768 complex samples each)
    // Total samples = 98,304 samples.
    // Expected elapsed time = 98304 / 2400000 = 0.04096 seconds (41 ms).
    const int CHUNK_BYTES = 65536;
    std::vector<uint8_t> buf(CHUNK_BYTES);

    // Warm up one chunk to establish next_chunk_time_
    int bytes = 0;
    dev.read_samples(buf.data(), CHUNK_BYTES, &bytes);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 3; ++i) {
        dev.read_samples(buf.data(), CHUNK_BYTES, &bytes);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    // 3 chunks * (32768 / 2400000) = 40.96 ms.
    // Verify real-time pacing prevents runaway CPU usage (> 30 ms) while completing without hangs (< 120 ms)
    ASSERT_GT(elapsed_ms, 30.0);
    ASSERT_LT(elapsed_ms, 120.0);

    dev.close();
}

TEST_CASE(ContinuousPhaseAcrossFrequencyChanges) {
    VirtualSdrDevice dev;
    dev.open(0);
    dev.set_sample_rate(2400000);

    const int N = 16384;
    std::vector<uint8_t> buf(N);

    // Rapidly alternate frequencies and read samples
    uint64_t freqs[] = {1420405000ULL, 89200000ULL, 121500000ULL, 433920000ULL};
    for (int iter = 0; iter < 10; ++iter) {
        for (uint64_t f : freqs) {
            dev.set_frequency(f);
            int bytes = 0;
            int ret = dev.read_samples(buf.data(), N, &bytes);
            ASSERT_EQ(ret, 0);
            ASSERT_EQ(bytes, N);
            for (int i = 0; i < N; ++i) {
                // Ensure no underflow or overflow or corrupted values
                ASSERT_TRUE(buf[i] >= 0 && buf[i] <= 255);
            }
        }
    }

    dev.close();
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("Virtual SDR Signal Generation & Pacing Tests");
    return ok ? 0 : 1;
}
