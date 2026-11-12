#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cmath>

#include "constants.h"
#include "config.h"
#include "cs16_writer.h"
#include "streaming_processor.h"
#include "dc_excision.h"
#include "rfi_filter.h"
#include "baseline_fitter.h"
#include "astrophysics.h"
#include "simd_dsp.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while(0)

#define ASSERT_TRUE(a, msg)  TEST_ASSERT((a), msg)
#define ASSERT_FALSE(a, msg) TEST_ASSERT(!(a), msg)

static double get_rss_mb() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
    return 0.0;
}

int main() {
    using namespace astro;

    std::cout << "======================================================================\n";
    std::cout << "  RUNNING BENCHMARK SUITE: test_performance_stress\n";
    std::cout << "  (Throughput, Disk Write Speed, SIMD Performance & Memory Audit)\n";
    std::cout << "======================================================================\n";

    std::string test_dir = "tests/test_perf_tmp";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::create_directories(test_dir, ec);

    std::string perf_cs16 = test_dir + "/perf_benchmark.cs16";

    double initial_rss = get_rss_mb();
    std::cout << "  Baseline Process RSS Memory: " << std::fixed << std::setprecision(2) << initial_rss << " MB\n";
    std::cout << "  Active SIMD Acceleration:    " << SimdDsp::get_simd_level_name() << "\n\n";

    // -----------------------------------------------------------------------
    // BENCHMARK 1: CS16 ASYNCHRONOUS DISK WRITING SPEED
    // -----------------------------------------------------------------------
    std::cout << "[Benchmark 1] Stress-Testing CS16 Async Disk Writer (50 MB raw capture)...\n";
    const size_t chunk_size = 262144; // 256 KB RTL-SDR standard block size
    const size_t num_chunks = 200;    // 50 MB total input data
    std::vector<uint8_t> synthetic_raw(chunk_size);
    for (size_t i = 0; i < chunk_size; ++i) {
        synthetic_raw[i] = static_cast<uint8_t>((i % 256) ^ 0x55);
    }

    Cs16Writer writer(test_dir);
    std::string actual_file = writer.start_recording(
        2400000, 1420500000, "Virtual Perf SDR", 25.4f, false, perf_cs16, 0.0
    );

    auto t_write_start = std::chrono::high_resolution_clock::now();
    for (size_t c = 0; c < num_chunks; ++c) {
        writer.feed_samples(synthetic_raw.data(), synthetic_raw.size());
        // Slight throttling (every 8 blocks = 2MB) to allow async disk worker to keep pace
        if (c % 8 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    writer.stop_recording();
    auto t_write_end = std::chrono::high_resolution_clock::now();

    double write_elapsed_s = std::chrono::duration<double>(t_write_end - t_write_start).count();
    uint64_t file_bytes = std::filesystem::file_size(actual_file);
    double write_mb = file_bytes / (1024.0 * 1024.0);
    double write_speed_mb_s = write_mb / write_elapsed_s;
    double write_msps = (file_bytes / (2 * sizeof(int16_t))) / (write_elapsed_s * 1e6);

    std::cout << "  Written: " << write_mb << " MB in " << write_elapsed_s << "s\n"
              << "  Sustained Write Throughput: " << std::fixed << std::setprecision(2)
              << write_speed_mb_s << " MB/s (" << write_msps << " MSPS)\n";

    auto stats = writer.get_stats();
    std::cout << "  Dropped Blocks: " << stats.dropped_blocks << "\n";

    ASSERT_TRUE(stats.dropped_blocks == 0, "Zero dropped blocks during 50 MB write burst");
    ASSERT_TRUE(write_speed_mb_s >= 20.0, "CS16 disk write speed >= 20.0 MB/s");

    double rss_after_write = get_rss_mb();
    std::cout << "  Process RSS after writing: " << rss_after_write << " MB\n\n";

    // -----------------------------------------------------------------------
    // BENCHMARK 2: STREAMING DSP ANALYSIS & ACCUMULATION SPEED
    // -----------------------------------------------------------------------
    std::cout << "[Benchmark 2] Benchmarking SIMD-Accelerated Streaming FFT Accumulator...\n";
    AstroConfig config;
    config.fft_size = 8192;
    config.system_temp_k = 100.0;
    config.output_dir = test_dir;
    config.baseline_method = BaselineMethod::RippleFilter;

    StreamingProcessor processor(actual_file, config);

    auto t_stream_start = std::chrono::high_resolution_clock::now();
    StreamingResults stream_res = processor.process(nullptr);
    auto t_stream_end = std::chrono::high_resolution_clock::now();

    double stream_elapsed_s = std::chrono::duration<double>(t_stream_end - t_stream_start).count();
    double stream_mb_s = (stream_res.file_size_bytes / (1024.0 * 1024.0)) / stream_elapsed_s;
    double stream_msps = (stream_res.total_samples / 1e6) / stream_elapsed_s;

    std::cout << "  Processed: " << stream_res.total_samples << " complex samples in "
              << stream_elapsed_s << "s\n"
              << "  Streaming Analysis Throughput: " << std::fixed << std::setprecision(2)
              << stream_mb_s << " MB/s (" << stream_msps << " MSPS)\n";

    ASSERT_TRUE(stream_mb_s >= 40.0, "Streaming analysis speed >= 40.0 MB/s (Real-time capability)");

    double rss_after_stream = get_rss_mb();
    std::cout << "  Process RSS during/after streaming: " << rss_after_stream << " MB\n\n";

    // -----------------------------------------------------------------------
    // BENCHMARK 3: ASTROPHYSICS & BASELINE FITTING STRESS / MEMORY AUDIT
    // -----------------------------------------------------------------------
    std::cout << "[Benchmark 3] Scientific Pipeline Stress & Memory Leak Audit (50 passes)...\n";
    DCExcisionInfo dc_info;
    std::vector<double> dc_clean = DCExcision::excise(stream_res.averaged_power, config, dc_info);
    int num_rfi = 0;
    std::vector<double> rfi_clean = RFIFilter::mitigate(dc_clean, stream_res.freq_mhz, config, num_rfi);

    auto t_pipe_start = std::chrono::high_resolution_clock::now();
    const int stress_iterations = 50;
    for (int iter = 0; iter < stress_iterations; ++iter) {
        BaselineResults baseline = BaselineFitter::fit(rfi_clean, stream_res.freq_mhz, config);

        size_t nfft = stream_res.freq_mhz.size();
        std::vector<double> cal_k(nfft);
        std::vector<double> cal_db(nfft);
        std::vector<double> v_kms = Astrophysics::compute_doppler_velocity(stream_res.freq_hz);

        for (size_t i = 0; i < nfft; ++i) {
            double base_safe = std::max(1e-12, baseline.baseline_power[i]);
            cal_k[i] = config.system_temp_k * (rfi_clean[i] - baseline.baseline_power[i]) / base_safe;
            cal_db[i] = 10.0 * std::log10(std::max(1e-6, rfi_clean[i] / base_safe));
        }

        double slope = 0.0, intercept = 0.0;
        BaselineFitter::refine_local(v_kms, cal_k, cal_db, config, slope, intercept);

        AstrophysicalMetrics metrics = Astrophysics::calculate_metrics(
            stream_res.freq_mhz, v_kms, cal_k, cal_db, config
        );
        (void)metrics;
    }
    auto t_pipe_end = std::chrono::high_resolution_clock::now();
    double pipe_elapsed_ms = std::chrono::duration<double, std::milli>(t_pipe_end - t_pipe_start).count();
    double avg_pipe_ms = pipe_elapsed_ms / stress_iterations;

    std::cout << "  Executed " << stress_iterations << " complete astrophysical passes in "
              << pipe_elapsed_ms << " ms (" << avg_pipe_ms << " ms/pass)\n";

    double peak_rss = get_rss_mb();
    std::cout << "  Audited Peak Working Set (RSS): " << std::fixed << std::setprecision(2) << peak_rss << " MB\n";

    // Crucial memory audit assertion: RSS must be strictly < 50.0 MB
    ASSERT_TRUE(peak_rss < 50.0, "Working set (RSS) memory footprint is strictly < 50 MB");

    // Memory growth check over 50 iterations: must not exceed 5 MB
    double growth = peak_rss - rss_after_stream;
    std::cout << "  Memory growth across stress loop: " << growth << " MB\n";
    ASSERT_TRUE(growth < 5.0, "Zero unmanaged memory leakage detected");

    // Cleanup
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "RESULTS: " << g_tests_passed << " / " << g_tests_run << " assertions PASSED.\n";
    if (g_tests_passed == g_tests_run) {
        std::cout << "[SUCCESS] test_performance_stress PASSED (100%)\n";
        return 0;
    } else {
        std::cerr << "[FAILURE] " << (g_tests_run - g_tests_passed) << " assertions FAILED!\n";
        return 1;
    }
}
