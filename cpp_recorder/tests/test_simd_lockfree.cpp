#include "test_framework.h"
#include "simd_dsp.h"
#include "lockfree_ringbuffer.h"
#include <vector>
#include <thread>
#include <atomic>
#include <cmath>
#include <limits>

TEST_CASE(SimdCpuDetectionAndAlignment) {
    auto level = SimdDsp::detect_cpu_simd();
    ASSERT_TRUE(level >= SimdDsp::CpuSimdLevel::SSE2);
    const char* name = SimdDsp::get_simd_level_name();
    ASSERT_TRUE(name != nullptr);

    // Test 64-byte aligned allocation
    void* ptr = SimdDsp::aligned_alloc_64(1024);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(ptr) % 64, 0ULL);
    SimdDsp::aligned_free_64(ptr);
}

TEST_CASE(SimdCs16ConversionBitExactness) {
    const size_t COUNT = 16384;
    std::vector<uint8_t> u8(COUNT);
    for (size_t i = 0; i < COUNT; ++i) {
        u8[i] = static_cast<uint8_t>(i % 256);
    }

    std::vector<int16_t> cs16_simd(COUNT, 0);
    std::vector<int16_t> cs16_scalar(COUNT, 0);

    SimdDsp::convert_u8_to_cs16_fast(u8.data(), cs16_simd.data(), COUNT);

    for (size_t i = 0; i < COUNT; ++i) {
        cs16_scalar[i] = static_cast<int16_t>((static_cast<int>(u8[i]) - 128) * 256);
    }

    for (size_t i = 0; i < COUNT; ++i) {
        ASSERT_EQ(cs16_simd[i], cs16_scalar[i]);
    }
}

TEST_CASE(SimdFastLog10Accuracy) {
    // Test across wide dynamic range from 1e-12 (-120 dBFS) to 1.0 (0 dBFS)
    float test_powers[] = {1e-12f, 1e-10f, 1e-8f, 1e-6f, 1e-4f, 1e-2f, 0.1f, 0.5f, 1.0f, 10.0f};

    for (float p : test_powers) {
        float exact = 10.0f * std::log10(p);
        float fast = SimdDsp::fast_10log10_scalar(p);
        ASSERT_NEAR(fast, exact, 0.05f); // Error must be less than 0.05 dB!
    }
}

TEST_CASE(SimdFastLog10EdgeCasesAndClamping) {
    // 1. Negative values, Zero, and Subnormals must clamp to -140.0 dBFS
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(-1.0f), -140.0f);
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(-1000.0f), -140.0f);
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(0.0f), -140.0f);
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(1e-35f), -140.0f);

    // 2. NaN and Inf must not crash or propagate NaN
    float qnan = std::numeric_limits<float>::quiet_NaN();
    float inf = std::numeric_limits<float>::infinity();
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(qnan), -140.0f);
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(inf), 40.0f);

    // 3. Normal values in (0.0001, 1000) evaluate accurately
    ASSERT_NEAR(SimdDsp::fast_10log10_scalar(100.0f), 20.0f, 0.05f);
    // Extreme runaway values (> +40 dBFS) must clamp to +40.0 dBFS
    ASSERT_EQ(SimdDsp::fast_10log10_scalar(1e6f), 40.0f);

#if defined(__AVX2__)
    // 4. Test AVX2 vectorized edge cases and clamping
    alignas(32) float in_avx[8] = {-5.0f, 0.0f, 1e-30f, qnan, 1e-12f, 1.0f, 10.0f, 1e6f};
    alignas(32) float out_avx[8] = {0};

    __m256 v_in = _mm256_loadu_ps(in_avx);
    __m256 v_out = SimdDsp::fast_10log10_avx2(v_in);
    _mm256_storeu_ps(out_avx, v_out);

    ASSERT_EQ(out_avx[0], -140.0f); // -5.0f -> clamped
    ASSERT_EQ(out_avx[1], -140.0f); // 0.0f -> clamped
    ASSERT_EQ(out_avx[2], -140.0f); // subnormal -> clamped
    ASSERT_EQ(out_avx[3], -140.0f); // NaN -> clamped
    ASSERT_NEAR(out_avx[4], -120.0f, 0.05f); // 1e-12
    ASSERT_NEAR(out_avx[5], 0.0f, 0.05f);    // 1.0
    ASSERT_NEAR(out_avx[6], 10.0f, 0.05f);   // 10.0
    ASSERT_NEAR(out_avx[7], 40.0f, 0.05f);   // 1e6f -> clamped to 40.0f
#endif
}

TEST_CASE(SimdArbitrarySampleCountsAndTailProcessing) {
    // Test tail processing for non-multiples of 32 and 16
    const size_t test_sizes[] = {1, 7, 15, 16, 17, 31, 32, 33, 47, 63, 65, 127};

    for (size_t count : test_sizes) {
        std::vector<uint8_t> u8(count);
        for (size_t i = 0; i < count; ++i) {
            u8[i] = static_cast<uint8_t>((i * 17 + 3) & 0xFF);
        }

        std::vector<int16_t> cs16_simd(count, 0);
        std::vector<int16_t> cs16_scalar(count, 0);

        SimdDsp::convert_u8_to_cs16_fast(u8.data(), cs16_simd.data(), count);

        for (size_t i = 0; i < count; ++i) {
            cs16_scalar[i] = static_cast<int16_t>((static_cast<int>(u8[i]) - 128) * 256);
            ASSERT_EQ(cs16_simd[i], cs16_scalar[i]);
        }
    }
}

TEST_CASE(LockFreeRingBufferSpscThroughputAndIntegrity) {
    LockFreeSpscRingBuffer<uint8_t> rb(65536); // 64K capacity
    ASSERT_EQ(rb.capacity(), 65536ULL);
    ASSERT_EQ(rb.available_read(), 0ULL);
    ASSERT_EQ(rb.available_write(), 65536ULL);

    const size_t TOTAL_ITEMS = 2048000; // 2000 chunks of 1024
    std::atomic<bool> start{false};

    // Producer thread
    std::thread producer([&]() {
        while (!start.load()) {}
        std::vector<uint8_t> chunk(1024);
        size_t written = 0;
        while (written < TOTAL_ITEMS) {
            size_t batch = std::min(chunk.size(), TOTAL_ITEMS - written);
            for (size_t i = 0; i < batch; ++i) {
                chunk[i] = static_cast<uint8_t>((written + i) & 0xFF);
            }
            size_t n = rb.write(chunk.data(), batch);
            written += n;
            if (n == 0) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    size_t total_received = 0;
    bool integrity_ok = true;
    std::thread consumer([&]() {
        while (!start.load()) {}
        std::vector<uint8_t> recv_chunk(1024);
        while (total_received < TOTAL_ITEMS) {
            size_t batch = std::min(recv_chunk.size(), TOTAL_ITEMS - total_received);
            size_t n = rb.read(recv_chunk.data(), batch);
            for (size_t i = 0; i < n; ++i) {
                uint8_t expected = static_cast<uint8_t>((total_received + i) & 0xFF);
                if (recv_chunk[i] != expected) {
                    integrity_ok = false;
                }
            }
            total_received += n;
            if (n == 0) {
                std::this_thread::yield();
            }
        }
    });

    start.store(true);
    producer.join();
    consumer.join();

    ASSERT_TRUE(integrity_ok);
    ASSERT_EQ(total_received, TOTAL_ITEMS);
    ASSERT_EQ(rb.dropped_count(), 0ULL);
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("SIMD & Lock-Free RingBuffer Tests");
    return ok ? 0 : 1;
}
