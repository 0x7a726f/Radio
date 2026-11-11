#include "test_framework.h"
#include "dsp_pipeline.h"
#include "cs16_writer.h"
#include "sdr_controller.h"
#include "constants.h"
#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace fs = std::filesystem;

static size_t get_current_process_ram_bytes() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
}

TEST_CASE(DspPipelineThroughputBenchmark) {
    DspPipeline dsp;
    const int fft_size = 4096;
    dsp.set_fft_size(fft_size);
    dsp.set_averaging(0.85f);

    const int NUM_FRAMES = 2500;
    const size_t total_samples = (size_t)NUM_FRAMES * fft_size;

    // Pre-generate input buffer
    std::vector<uint8_t> frame(fft_size * 2);
    for (size_t i = 0; i < frame.size(); ++i) {
        frame[i] = (uint8_t)(128 + (int)(60.0 * std::sin(2.0 * 3.14159 * (double)i / 32.0)));
    }

    std::vector<float> pwr;
    float pf = 0.0f, pdb = 0.0f;

    // Warm-up run
    for (int i = 0; i < 50; ++i) {
        dsp.feed_samples(frame.data(), frame.size());
        dsp.get_power_spectrum(pwr, pf, pdb, 1420405000ULL, 2400000);
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_FRAMES; ++i) {
        dsp.feed_samples(frame.data(), frame.size());
        dsp.get_power_spectrum(pwr, pf, pdb, 1420405000ULL, 2400000);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    double msps = ((double)total_samples / elapsed_s) / 1e6;
    double ffts_per_sec = (double)NUM_FRAMES / elapsed_s;
    double latency_us = (elapsed_s / (double)NUM_FRAMES) * 1e6;

    std::cout << "\n    ========================================================\n";
    std::cout << "    [BENCHMARK] DSP PIPELINE (KissFFT 4096-pt + Hann + dBFS)\n";
    std::cout << "    ========================================================\n";
    std::cout << "    Processed Frames:     " << NUM_FRAMES << " FFTs\n";
    std::cout << "    Total Samples:        " << total_samples << " complex samples\n";
    std::cout << "    Elapsed Time:         " << std::fixed << std::setprecision(4) << elapsed_s << " s\n";
    std::cout << "    Sustained Throughput: " << std::fixed << std::setprecision(2) << msps << " MSPS\n";
    std::cout << "    Transform Rate:       " << std::fixed << std::setprecision(1) << ffts_per_sec << " FFTs/sec\n";
    std::cout << "    Latency per 4096-FFT: " << std::fixed << std::setprecision(2) << latency_us << " microseconds\n";
    std::cout << "    Real-time Margin:     " << std::fixed << std::setprecision(1) << (msps / 2.4) << "x faster than 2.4 MSPS\n";
    std::cout << "    ========================================================\n";

    // Standard laptop CPU should easily exceed 20 MSPS (nearly 10x real-time)
    ASSERT_GT(msps, 15.0);
}

TEST_CASE(Cs16WriterSustainedDiskThroughputBenchmark) {
    const std::string out_dir = "tests_output/bench_cs16";
    fs::create_directories(out_dir);
    const std::string cs16_file = out_dir + "/bench_stream.cs16";
    if (fs::exists(cs16_file)) fs::remove(cs16_file);

    Cs16Writer writer(out_dir);
    writer.start_recording(2400000, 1420405000ULL, "Bench-SDR", 28.0f, false, cs16_file);

    // 64 MB of raw uint8 -> 128 MB of int16 CS16
    const size_t CHUNK_SIZE = 131072; // 128 KB
    const int NUM_CHUNKS = 512;        // 512 * 128 KB = 64 MB input -> 128 MB output
    const size_t total_input_bytes = (size_t)NUM_CHUNKS * CHUNK_SIZE;
    const size_t total_output_bytes = total_input_bytes * 2;

    std::vector<uint8_t> block(CHUNK_SIZE);
    for (size_t i = 0; i < CHUNK_SIZE; ++i) block[i] = (uint8_t)(i & 0xFF);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_CHUNKS; ++i) {
        writer.feed_samples(block.data(), block.size());
        // Tiny sleep to avoid overwhelming queue limit
        if (i % 30 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    writer.stop_recording();
    auto t1 = std::chrono::high_resolution_clock::now();

    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    uint64_t actual_bytes = fs::exists(cs16_file) ? fs::file_size(cs16_file) : 0;
    double mb_written = (double)actual_bytes / (1024.0 * 1024.0);
    double write_speed_mb_s = mb_written / elapsed_s;

    std::cout << "\n    ========================================================\n";
    std::cout << "    [BENCHMARK] CS16 WRITER & DISK THROUGHPUT\n";
    std::cout << "    ========================================================\n";
    std::cout << "    Total CS16 Written:   " << std::fixed << std::setprecision(2) << mb_written << " MB\n";
    std::cout << "    Elapsed Time:         " << std::fixed << std::setprecision(4) << elapsed_s << " s\n";
    std::cout << "    Sustained Write Rate: " << std::fixed << std::setprecision(2) << write_speed_mb_s << " MB/s\n";
    std::cout << "    Target Rate @ 2.4M:   9.16 MB/s (2.4 MSPS * 4 B/sample)\n";
    std::cout << "    Throughput Headroom:  " << std::fixed << std::setprecision(1) << (write_speed_mb_s / 9.16) << "x\n";
    std::cout << "    ========================================================\n";

    ASSERT_GT(write_speed_mb_s, 10.0); // Must comfortably exceed real-time 9.16 MB/s
}

TEST_CASE(ZeroMemoryLeakAndMinimalRamFootprint) {
    size_t ram_initial = get_current_process_ram_bytes();

    {
        // Cycle DspPipeline, Cs16Writer, and SdrController 50 times
        for (int cycle = 0; cycle < 50; ++cycle) {
            {
                DspPipeline dsp;
                dsp.set_fft_size(4096);
                std::vector<uint8_t> dummy(8192, 128);
                dsp.feed_samples(dummy.data(), dummy.size());
                std::vector<float> pwr;
                float pf = 0, pdb = 0;
                dsp.get_power_spectrum(pwr, pf, pdb, 1420405000ULL, 2400000);
            }
            {
                std::string path = "tests_output/bench_cs16/cycle_" + std::to_string(cycle) + ".cs16";
                Cs16Writer writer("tests_output/bench_cs16");
                writer.start_recording(2400000, 1420405000ULL, "Test", 20.0f, false, path);
                std::vector<uint8_t> dummy(16384, 128);
                writer.feed_samples(dummy.data(), dummy.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                writer.stop_recording();
                if (fs::exists(path)) fs::remove(path);
                std::string txt = "tests_output/bench_cs16/cycle_" + std::to_string(cycle) + ".txt";
                std::string jsn = "tests_output/bench_cs16/cycle_" + std::to_string(cycle) + ".json";
                if (fs::exists(txt)) fs::remove(txt);
                if (fs::exists(jsn)) fs::remove(jsn);
            }
        }
    }

    size_t ram_final = get_current_process_ram_bytes();
    double ram_initial_mb = (double)ram_initial / (1024.0 * 1024.0);
    double ram_final_mb = (double)ram_final / (1024.0 * 1024.0);
    double ram_diff_mb = ram_final_mb - ram_initial_mb;

    std::cout << "\n    ========================================================\n";
    std::cout << "    [BENCHMARK] RAM CONSUMPTION & MEMORY LEAK AUDIT\n";
    std::cout << "    ========================================================\n";
    std::cout << "    Initial Working Set:  " << std::fixed << std::setprecision(2) << ram_initial_mb << " MB\n";
    std::cout << "    Final Working Set:    " << std::fixed << std::setprecision(2) << ram_final_mb << " MB\n";
    std::cout << "    Net RAM Differential: " << std::showpos << ram_diff_mb << " MB\n" << std::noshowpos;
    std::cout << "    ========================================================\n";

    // Working set should remain lightweight (< 50 MB total process footprint)
    ASSERT_LT(ram_final_mb, 50.0);
    // Differential across 50 full cycles should be negligible (< 10 MB)
    ASSERT_LT(ram_diff_mb, 10.0);
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("Performance Benchmarks & Stress Tests");
    return ok ? 0 : 1;
}
