#include "test_framework.h"
#include "sdr_controller.h"
#include "constants.h"
#include <vector>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>

TEST_CASE(RapidFrequencyTuningCoalescingWhileStreaming) {
    SdrController controller;
    bool init_ok = controller.initialize(true); // force simulated virtual SDR
    ASSERT_TRUE(init_ok);
    ASSERT_TRUE(controller.is_simulated());

    bool stream_ok = controller.start_streaming();
    ASSERT_TRUE(stream_ok);
    ASSERT_TRUE(controller.is_streaming());

    // Allow worker thread to spin up
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Simulate 100 rapid frequency adjustments (e.g., furious mouse-wheel spin from 1420 MHz to 1421 MHz)
    const int NUM_COMMANDS = 100;
    std::vector<std::future<bool>> futures;
    futures.reserve(NUM_COMMANDS);

    const uint64_t start_freq = 1420000000ULL;
    const uint64_t step_freq = 10000ULL; // 10 kHz step

    for (int i = 0; i < NUM_COMMANDS; ++i) {
        uint64_t target_freq = start_freq + i * step_freq;
        futures.push_back(controller.request_frequency(target_freq));
    }

    uint64_t expected_final_freq = start_freq + (NUM_COMMANDS - 1) * step_freq;

    // Verify all 100 futures resolve properly without hanging or deadlocking
    for (int i = 0; i < NUM_COMMANDS; ++i) {
        auto status = futures[i].wait_for(std::chrono::seconds(2));
        ASSERT_TRUE(status == std::future_status::ready);
        bool result = futures[i].get();
        ASSERT_TRUE(result); // Both superseded and executed commands must resolve true
    }

    // Wait a brief moment for the worker to finalize the state
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    uint64_t final_freq = controller.get_current_frequency();
    ASSERT_EQ(final_freq, expected_final_freq);

    controller.stop_streaming();
    ASSERT_FALSE(controller.is_streaming());
    controller.shutdown();
}

TEST_CASE(RapidMultiParameterCoalescing) {
    SdrController controller;
    bool init_ok = controller.initialize(true);
    ASSERT_TRUE(init_ok);

    bool stream_ok = controller.start_streaming();
    ASSERT_TRUE(stream_ok);

    std::vector<std::future<bool>> gain_futs;
    std::vector<std::future<bool>> ppm_futs;
    std::vector<std::future<bool>> bias_futs;

    const int N = 50;
    for (int i = 0; i < N; ++i) {
        gain_futs.push_back(controller.request_gain(10.0f + (float)i * 0.5f, false));
        ppm_futs.push_back(controller.request_ppm(i - 25));
        bias_futs.push_back(controller.request_bias_tee(i % 2 == 1));
    }

    // Wait for all futures
    for (auto& f : gain_futs) {
        ASSERT_TRUE(f.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        ASSERT_TRUE(f.get());
    }
    for (auto& f : ppm_futs) {
        ASSERT_TRUE(f.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        ASSERT_TRUE(f.get());
    }
    for (auto& f : bias_futs) {
        ASSERT_TRUE(f.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        ASSERT_TRUE(f.get());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_EQ(controller.get_current_ppm(), N - 1 - 25);
    ASSERT_EQ(controller.is_bias_tee_enabled(), ((N - 1) % 2 == 1));

    controller.stop_streaming();
    controller.shutdown();
}

TEST_CASE(IdleControllerCommandProcessing) {
    SdrController controller;
    bool init_ok = controller.initialize(true);
    ASSERT_TRUE(init_ok);
    ASSERT_FALSE(controller.is_streaming());

    // Send frequency command while idle
    auto fut1 = controller.request_frequency(89200000ULL);
    ASSERT_TRUE(fut1.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    ASSERT_TRUE(fut1.get());
    ASSERT_EQ(controller.get_current_frequency(), 89200000ULL);

    // Send gain command while idle
    auto fut2 = controller.request_gain(38.6f, false);
    ASSERT_TRUE(fut2.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    ASSERT_TRUE(fut2.get());
    ASSERT_EQ(controller.get_current_gain(), 38.6f);

    // Send sample rate while idle
    auto fut3 = controller.request_sample_rate(2048000);
    ASSERT_TRUE(fut3.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    ASSERT_TRUE(fut3.get());
    ASSERT_EQ(controller.get_current_sample_rate(), 2048000u);

    controller.shutdown();
}

TEST_CASE(ConcurrentStressNoDeadlock) {
    SdrController controller;
    bool init_ok = controller.initialize(true);
    ASSERT_TRUE(init_ok);
    controller.start_streaming();

    std::atomic<bool> run_stress{true};
    std::atomic<int> completed_cmds{0};

    auto stress_worker = [&](int thread_id) {
        int count = 0;
        while (run_stress.load() && count < 200) {
            uint64_t f = 1420000000ULL + (thread_id * 1000000ULL) + (count * 1000);
            auto fut = controller.request_frequency(f);
            if (fut.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
                fut.get();
            }
            count++;
            completed_cmds++;
            std::this_thread::yield();
        }
    };

    std::thread t1(stress_worker, 1);
    std::thread t2(stress_worker, 2);
    std::thread t3(stress_worker, 3);
    std::thread t4(stress_worker, 4);

    // Run for 400 ms then stop
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    run_stress = false;

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    ASSERT_GT(completed_cmds.load(), 10);

    // Verify stop and shutdown without deadlocks
    controller.stop_streaming();
    controller.shutdown();
}

int main() {
    bool ok = TestFramework::TestRunner::instance().run_all("Tuning Coalescing & Thread Safety Tests");
    return ok ? 0 : 1;
}
