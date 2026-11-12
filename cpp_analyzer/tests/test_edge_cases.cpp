#include "test_framework.h"
#include "streaming_processor.h"
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>

using namespace astro;

void test_missing_file() {
    TEST_CASE("Missing / Non-Existent File Exception");

    AstroConfig cfg;
    bool caught = false;
    try {
        StreamingProcessor proc("non_existent_file_12345.cs16", cfg);
        proc.process();
    } catch (const std::exception& e) {
        caught = true;
    }

    ASSERT_TRUE(caught);
    TEST_PASS();
}

void test_zero_byte_file() {
    TEST_CASE("Zero-Byte Empty File Handling");

    std::string path = "test_empty_0byte.cs16";
    {
        std::ofstream out(path, std::ios::binary);
        // 0 bytes written
    }

    AstroConfig cfg;
    bool caught = false;
    try {
        StreamingProcessor proc(path, cfg);
        proc.process();
    } catch (const std::exception& e) {
        caught = true;
    }

    std::filesystem::remove(path);
    ASSERT_TRUE(caught);
    TEST_PASS();
}

void test_sub_fft_file() {
    TEST_CASE("Sub-FFT File Size Handling (Fewer Samples Than FFT Size)");

    std::string path = "test_sub_fft.cs16";
    {
        std::ofstream out(path, std::ios::binary);
        // Write 64 samples (256 bytes) when FFT size is 1024 (requires 4096 bytes)
        std::vector<int16_t> buf(128, 0);
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size() * sizeof(int16_t));
    }

    AstroConfig cfg;
    cfg.fft_size = 1024;

    bool caught = false;
    try {
        StreamingProcessor proc(path, cfg);
        proc.process();
    } catch (const std::exception& e) {
        caught = true;
    }

    std::filesystem::remove(path);
    ASSERT_TRUE(caught);
    TEST_PASS();
}

void test_non_multiple_of_4_size() {
    TEST_CASE("Non-Multiple-of-4 File Size (Trailing Byte Truncation)");

    std::string path = "test_non_multiple_4.cs16";
    const int valid_samples = 1024 * 4; // 4 blocks of 1024
    {
        std::ofstream out(path, std::ios::binary);
        std::vector<int16_t> buf(valid_samples * 2, 100);
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size() * sizeof(int16_t));
        // Append 3 trailing corrupted bytes (non-multiple of 4 CS16 complex sample)
        char trailing[3] = {0x01, 0x02, 0x03};
        out.write(trailing, 3);
    }

    AstroConfig cfg;
    cfg.fft_size = 1024;
    cfg.chunk_blocks = 2;

    StreamingProcessor proc(path, cfg);
    StreamingResults res = proc.process();

    ASSERT_EQ(res.total_blocks, 4u);
    ASSERT_EQ(res.total_samples, static_cast<uint64_t>(valid_samples));

    std::filesystem::remove(path);
    TEST_PASS();
}

void test_cancellation_token() {
    TEST_CASE("Cancellation Token Responsiveness (Immediate & Active)");

    std::string path = "test_large_cancel.cs16";
    const int sample_rate = 2400000;
    const int fft_size = 1024;
    const int total_blocks = 10000; // 10,000 blocks * 1024 * 4 bytes = ~40.9 MB
    {
        std::ofstream out(path, std::ios::binary);
        std::vector<int16_t> buf(fft_size * 2, 0);
        for (int b = 0; b < total_blocks; ++b) {
            out.write(reinterpret_cast<const char*>(buf.data()), buf.size() * sizeof(int16_t));
        }
    }

    AstroConfig cfg;
    cfg.sample_rate_hz = sample_rate;
    cfg.fft_size = fft_size;
    cfg.chunk_blocks = 32;

    // Test 1: Pre-set cancel token before process() starts
    {
        std::atomic<bool> cancel_flag{true};
        StreamingProcessor proc(path, cfg);
        proc.set_cancel_token(&cancel_flag);

        StreamingResults res = proc.process();
        ASSERT_EQ(res.total_blocks, 0u);
        ASSERT_TRUE(proc.is_cancelled());
    }

    // Test 2: Active cancellation during streaming
    {
        std::atomic<bool> cancel_flag{false};
        StreamingProcessor proc(path, cfg);
        proc.set_cancel_token(&cancel_flag);

        std::thread cancel_thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancel_flag.store(true);
        });

        StreamingResults res = proc.process();
        cancel_thread.join();

        ASSERT_TRUE(res.total_blocks < static_cast<uint64_t>(total_blocks));
        ASSERT_TRUE(proc.is_cancelled());
    }

    std::filesystem::remove(path);
    TEST_PASS();
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " RUNNING EDGE CASES TEST SUITE\n";
    std::cout << "=======================================================\n";

    test_missing_file();
    test_zero_byte_file();
    test_sub_fft_file();
    test_non_multiple_of_4_size();
    test_cancellation_token();

    test::report_summary("Edge Cases");
    return test::exit_code();
}
