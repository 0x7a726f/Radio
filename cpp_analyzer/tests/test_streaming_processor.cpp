#include "test_framework.h"
#include "streaming_processor.h"
#include "kiss_fft.h"
#include <fstream>
#include <cmath>
#include <vector>
#include <filesystem>

using namespace astro;

static constexpr double PI = 3.14159265358979323846;

void test_window_generators() {
    TEST_CASE("Window Generators (Blackman, Hann, Blackman-Harris)");

    int n = 1024;
    auto w_hann = StreamingProcessor::generate_window(WindowType::Hann, n);
    auto w_blackman = StreamingProcessor::generate_window(WindowType::Blackman, n);
    auto w_bh = StreamingProcessor::generate_window(WindowType::BlackmanHarris, n);

    ASSERT_EQ(w_hann.size(), static_cast<size_t>(n));
    ASSERT_EQ(w_blackman.size(), static_cast<size_t>(n));
    ASSERT_EQ(w_bh.size(), static_cast<size_t>(n));

    // Endpoints for Hann must be 0.0
    ASSERT_NEAR(w_hann[0], 0.0f, 1e-5);
    ASSERT_NEAR(w_hann[n - 1], 0.0f, 1e-5);

    // Center for Hann must be 1.0 (at n/2)
    ASSERT_NEAR(w_hann[n / 2], 1.0f, 1e-3);

    // Symmetry check: w[i] == w[n - 1 - i]
    for (int i = 0; i < n / 2; ++i) {
        ASSERT_NEAR(w_hann[i], w_hann[n - 1 - i], 1e-5);
        ASSERT_NEAR(w_blackman[i], w_blackman[n - 1 - i], 1e-5);
        ASSERT_NEAR(w_bh[i], w_bh[n - 1 - i], 1e-5);
    }

    // Blackman peak at center is ~1.0
    ASSERT_NEAR(w_blackman[n / 2], 1.0f, 1e-3);

    TEST_PASS();
}

void test_kiss_fft_tone_accuracy() {
    TEST_CASE("KissFFT Transform Accuracy on Pure Complex Sinusoid");

    int nfft = 1024;
    kiss_fft_cfg cfg = kiss_fft_alloc(nfft, 0, nullptr, nullptr);
    ASSERT_TRUE(cfg != nullptr);

    std::vector<kiss_fft_cpx> in(nfft);
    std::vector<kiss_fft_cpx> out(nfft);

    // Inject complex tone at exactly bin k_target = 75
    int k_target = 75;
    for (int i = 0; i < nfft; ++i) {
        double phase = 2.0 * PI * k_target * i / nfft;
        in[i].r = static_cast<float>(std::cos(phase));
        in[i].i = static_cast<float>(std::sin(phase));
    }

    kiss_fft(cfg, in.data(), out.data());
    kiss_fft_free(cfg);

    // Check power spectrum: bin k_target should contain virtually 100% of the spectral power
    double max_power = -1.0;
    int peak_bin = -1;
    double off_power_sum = 0.0;

    for (int k = 0; k < nfft; ++k) {
        double p = out[k].r * out[k].r + out[k].i * out[k].i;
        if (p > max_power) {
            max_power = p;
            peak_bin = k;
        }
        if (k != k_target) {
            off_power_sum += p;
        }
    }

    ASSERT_EQ(peak_bin, k_target);
    // Dynamic range > 80 dB (off-bin leakage should be negligible for integer bin tone)
    ASSERT_TRUE(max_power / (off_power_sum + 1e-12) > 1e8);

    TEST_PASS();
}

void test_streaming_chunking_and_alignment() {
    TEST_CASE("Streaming Chunking & CS16 Sample Alignment");

    std::string test_file = "test_synthetic_stream.cs16";
    const int sample_rate = 2400000;
    const int fft_size = 512;
    const int num_blocks = 20; // 20 * 512 = 10,240 complex samples
    const int total_samples = num_blocks * fft_size;

    {
        std::ofstream out(test_file, std::ios::binary);
        std::vector<int16_t> raw(total_samples * 2);
        // Generate test tone at +100 kHz offset
        double f_tone = 100000.0;
        for (int i = 0; i < total_samples; ++i) {
            double phase = 2.0 * PI * f_tone * i / sample_rate;
            raw[2 * i] = static_cast<int16_t>(10000.0 * std::cos(phase));
            raw[2 * i + 1] = static_cast<int16_t>(10000.0 * std::sin(phase));
        }
        out.write(reinterpret_cast<const char*>(raw.data()), raw.size() * sizeof(int16_t));
    }

    AstroConfig cfg;
    cfg.sample_rate_hz = sample_rate;
    cfg.center_freq_hz = 1420.5e6;
    cfg.fft_size = fft_size;
    cfg.chunk_blocks = 8; // Chunk size smaller than total blocks to test multiple streaming chunk reads

    StreamingProcessor proc(test_file, cfg);
    StreamingResults res = proc.process();

    ASSERT_EQ(res.total_blocks, static_cast<uint64_t>(num_blocks));
    ASSERT_EQ(res.total_samples, static_cast<uint64_t>(total_samples));
    ASSERT_EQ(res.averaged_power.size(), static_cast<size_t>(fft_size));

    // Frequency check: tone at +100 kHz should peak at center + 100 kHz
    double bin_res = static_cast<double>(sample_rate) / fft_size;
    int expected_bin_offset = static_cast<int>(std::round(100000.0 / bin_res));
    int expected_peak_bin = (fft_size / 2) + expected_bin_offset;

    int actual_peak_bin = 0;
    double max_p = -1.0;
    for (int i = 0; i < fft_size; ++i) {
        if (res.averaged_power[i] > max_p) {
            max_p = res.averaged_power[i];
            actual_peak_bin = i;
        }
    }

    ASSERT_EQ(actual_peak_bin, expected_peak_bin);

    std::filesystem::remove(test_file);
    TEST_PASS();
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " RUNNING STREAMING PROCESSOR TEST SUITE\n";
    std::cout << "=======================================================\n";

    test_window_generators();
    test_kiss_fft_tone_accuracy();
    test_streaming_chunking_and_alignment();

    test::report_summary("Streaming Processor");
    return test::exit_code();
}
