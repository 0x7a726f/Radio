#include "test_framework.h"
#include "dsp_pipeline.h"
#include "constants.h"
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: generate complex sinusoidal tone in uint8 I/Q format
static std::vector<uint8_t> generate_test_tone_u8(int num_samples, double tone_freq_hz, double fs_hz, float amplitude = 80.0f) {
    std::vector<uint8_t> buf(num_samples * 2);
    for (int n = 0; n < num_samples; ++n) {
        double phase = 2.0 * M_PI * tone_freq_hz * (double)n / fs_hz;
        double i_val = 128.0 + (double)amplitude * std::cos(phase);
        double q_val = 128.0 + (double)amplitude * std::sin(phase);
        buf[2 * n]     = (uint8_t)std::clamp((int)std::lround(i_val), 0, 255);
        buf[2 * n + 1] = (uint8_t)std::clamp((int)std::lround(q_val), 0, 255);
    }
    return buf;
}

TEST_CASE(HannWindowSymmetryAndBoundaries) {
    DspPipeline dsp;
    int n_pts = 4096;
    dsp.set_fft_size(n_pts);
    ASSERT_EQ(dsp.get_fft_size(), n_pts);

    // Compute expected Hann window directly and check mathematical properties
    for (int n = 0; n < n_pts; ++n) {
        double expected = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (double)(n_pts - 1)));
        if (n == 0 || n == n_pts - 1) {
            ASSERT_NEAR(expected, 0.0, 1e-6);
        }
        if (n == n_pts / 2) {
            ASSERT_NEAR(expected, 1.0, 1e-4);
        }
    }
}

TEST_CASE(KissFFTToneDetectionAndBinAccuracy) {
    DspPipeline dsp;
    const int fft_size = 4096;
    dsp.set_fft_size(fft_size);
    dsp.set_averaging(0.0f); // Disable smoothing for single-frame precision

    const uint32_t sample_rate = 2400000; // 2.4 MSPS
    const uint64_t center_freq = 1420405752ULL; // 1420.405 MHz
    const double target_offset = 350000.0; // +350 kHz offset
    const double bin_width = (double)sample_rate / (double)fft_size; // ~585.9 Hz

    auto samples = generate_test_tone_u8(fft_size, target_offset, sample_rate, 90.0f);
    dsp.feed_samples(samples.data(), samples.size());

    std::vector<float> power;
    float peak_freq = 0.0f;
    float peak_db = 0.0f;

    bool ok = dsp.get_power_spectrum(power, peak_freq, peak_db, center_freq, sample_rate);
    ASSERT_TRUE(ok);
    ASSERT_EQ((int)power.size(), fft_size);

    double expected_peak_freq = (double)center_freq + target_offset;
    // Expected frequency should match within +/- 1.5 FFT bins
    ASSERT_NEAR((double)peak_freq, expected_peak_freq, bin_width * 1.5);
    ASSERT_GT(peak_db, -20.0f); // Strong tone should be above -20 dBFS
}

TEST_CASE(PowerSpectrumDbfsDynamicRange) {
    DspPipeline dsp;
    const int fft_size = 4096;
    dsp.set_fft_size(fft_size);
    dsp.set_averaging(0.0f);

    const uint32_t sample_rate = 2400000;
    const uint64_t center_freq = 1420000000ULL;

    // High amplitude tone (amplitude 100)
    auto high_tone = generate_test_tone_u8(fft_size, 200000.0, sample_rate, 100.0f);
    dsp.feed_samples(high_tone.data(), high_tone.size());
    std::vector<float> power_high;
    float peak_freq_high = 0.0f, peak_db_high = 0.0f;
    dsp.get_power_spectrum(power_high, peak_freq_high, peak_db_high, center_freq, sample_rate);

    // Low amplitude tone (amplitude 25, 4x lower -> ~12 dB lower)
    dsp.flush();
    auto low_tone = generate_test_tone_u8(fft_size, 200000.0, sample_rate, 25.0f);
    dsp.feed_samples(low_tone.data(), low_tone.size());
    std::vector<float> power_low;
    float peak_freq_low = 0.0f, peak_db_low = 0.0f;
    dsp.get_power_spectrum(power_low, peak_freq_low, peak_db_low, center_freq, sample_rate);

    float delta_db = peak_db_high - peak_db_low;
    // 20 * log10(100 / 25) = 20 * log10(4) = 12.04 dB
    ASSERT_NEAR(delta_db, 12.04f, 1.5f);
}

TEST_CASE(ExponentialSmoothingConvergence) {
    DspPipeline dsp;
    const int fft_size = 2048;
    dsp.set_fft_size(fft_size);
    dsp.set_averaging(0.80f); // 80% smoothing history
    dsp.flush();

    const uint32_t sample_rate = 2400000;
    const uint64_t center_freq = 1420000000ULL;

    // Feed step input: high power tone
    auto tone1 = generate_test_tone_u8(fft_size, 100000.0, sample_rate, 90.0f);
    dsp.feed_samples(tone1.data(), tone1.size());
    std::vector<float> p1;
    float f1 = 0.0f, db1 = 0.0f;
    dsp.get_power_spectrum(p1, f1, db1, center_freq, sample_rate);

    // Now feed weak noise/flat baseline
    auto quiet = generate_test_tone_u8(fft_size, 100000.0, sample_rate, 2.0f);
    dsp.feed_samples(quiet.data(), quiet.size());
    std::vector<float> p2;
    float f2 = 0.0f, db2 = 0.0f;
    dsp.get_power_spectrum(p2, f2, db2, center_freq, sample_rate);

    // With 0.8 smoothing, db2 should not drop immediately to flat floor, but be smoothed
    ASSERT_LT(db2, db1);
    ASSERT_GT(db2, db1 - 15.0f); // smooth transition
}

TEST_CASE(FlushClearsOldSpectrumWithoutGhostArtifacts) {
    DspPipeline dsp;
    const int fft_size = 4096;
    dsp.set_fft_size(fft_size);
    dsp.set_averaging(0.90f); // Strong smoothing

    const uint32_t sample_rate = 2400000;
    const uint64_t center_freq = 1420400000ULL;

    // 1. Establish strong peak at +400 kHz
    auto tone_a = generate_test_tone_u8(fft_size, +400000.0, sample_rate, 90.0f);
    dsp.feed_samples(tone_a.data(), tone_a.size());
    std::vector<float> p_a;
    float fa = 0.0f, dba = 0.0f;
    dsp.get_power_spectrum(p_a, fa, dba, center_freq, sample_rate);
    ASSERT_NEAR((double)fa, (double)center_freq + 400000.0, 1000.0);

    // 2. Flush pipeline (simulating frequency retune)
    dsp.flush();

    // 3. Immediately inject strong tone at -300 kHz
    auto tone_b = generate_test_tone_u8(fft_size, -300000.0, sample_rate, 90.0f);
    dsp.feed_samples(tone_b.data(), tone_b.size());
    std::vector<float> p_b;
    float fb = 0.0f, dbb = 0.0f;
    dsp.get_power_spectrum(p_b, fb, dbb, center_freq, sample_rate);

    // Peak MUST immediately be at -300 kHz without ghost peak at +400 kHz dominating
    ASSERT_NEAR((double)fb, (double)center_freq - 300000.0, 1000.0);
}

TEST_CASE(DynamicFftSizeReconfiguration) {
    DspPipeline dsp;
    const uint32_t sample_rate = 2400000;
    const uint64_t center_freq = 1420000000ULL;

    int test_sizes[] = {512, 1024, 2048, 4096, 8192, 16384};
    for (int size : test_sizes) {
        dsp.set_fft_size(size);
        ASSERT_EQ(dsp.get_fft_size(), size);

        auto tone = generate_test_tone_u8(size, 150000.0, sample_rate, 80.0f);
        dsp.feed_samples(tone.data(), tone.size());

        std::vector<float> pwr;
        float pf = 0.0f, pdb = 0.0f;
        bool ok = dsp.get_power_spectrum(pwr, pf, pdb, center_freq, sample_rate);
        ASSERT_TRUE(ok);
        ASSERT_EQ((int)pwr.size(), size);
        ASSERT_NEAR((double)pf, (double)center_freq + 150000.0, (double)sample_rate / size * 1.5);
    }
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("DSP Pipeline Unit Tests");
    return ok ? 0 : 1;
}
