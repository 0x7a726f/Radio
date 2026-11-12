#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "app_state.h"

// Test harness macros
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

#define ASSERT_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define ASSERT_TRUE(a, msg)  TEST_ASSERT((a), msg)
#define ASSERT_FALSE(a, msg) TEST_ASSERT(!(a), msg)

void test_initial_state() {
    std::cout << "[Test 1] Testing AppState initial default values...\n";
    AppState state;
    ASSERT_EQ(state.get_section(), AppSection::Record, "Initial section must be Record");
    ASSERT_FALSE(state.is_recording_active(), "Initial recording active must be false");
    ASSERT_EQ(state.get_active_recording_filepath(), "", "Initial active filepath must be empty");
    ASSERT_EQ(state.get_last_recorded_file(), "", "Initial last recorded file must be empty");
    std::string target;
    ASSERT_FALSE(state.check_and_clear_auto_analyze(target), "Initial auto_analyze request must be false");
}

void test_mode_transitions() {
    std::cout << "[Test 2] Testing AppState manual section transitions...\n";
    AppState state;
    
    state.set_section(AppSection::Analyze);
    ASSERT_EQ(state.get_section(), AppSection::Analyze, "Mode transition to Analyze");

    state.switch_to_record();
    ASSERT_EQ(state.get_section(), AppSection::Record, "switch_to_record should return to Record");

    state.set_section(AppSection::Record);
    ASSERT_EQ(state.get_section(), AppSection::Record, "set_section(Record) should remain Record");
}

void test_active_recording_registration() {
    std::cout << "[Test 3] Testing Active Recording File Registration...\n";
    AppState state;

    std::string test_file = "recordings/active_test.cs16";
    state.set_recording_active(true, test_file);
    ASSERT_TRUE(state.is_recording_active(), "is_recording_active must be true when set");
    ASSERT_EQ(state.get_active_recording_filepath(), test_file, "active_recording_filepath must match");

    state.set_recording_active(false);
    ASSERT_FALSE(state.is_recording_active(), "is_recording_active must be false after clear");
    ASSERT_EQ(state.get_active_recording_filepath(), "", "active_recording_filepath must be cleared");
}

void test_collision_prevention() {
    std::cout << "[Test 4] Testing In-Flight File Collision Prevention...\n";
    AppState state;

    // Create a temporary file on disk to test std::filesystem::equivalent path matching
    std::filesystem::create_directories("test_tmp");
    std::string test_file = "test_tmp/collision_test.cs16";
    {
        std::ofstream ofs(test_file, std::ios::binary);
        ofs << "test_cs16_data";
    }

    state.set_recording_active(true, test_file);

    // 1. Direct match
    ASSERT_TRUE(state.is_file_actively_recording(test_file),
                "Active recording file must be reported as actively recording");

    // 2. Relative vs absolute path equivalence test
    std::string abs_path = std::filesystem::absolute(test_file).string();
    ASSERT_TRUE(state.is_file_actively_recording(abs_path),
                "std::filesystem::equivalent should match absolute path of in-flight file");

    // 3. Different file must return false
    ASSERT_FALSE(state.is_file_actively_recording("test_tmp/other_file.cs16"),
                 "Other file must NOT be reported as in-flight recording");

    // 4. Empty path must return false
    ASSERT_FALSE(state.is_file_actively_recording(""),
                 "Empty path must return false");

    // 5. Stopping recording must release collision block
    state.set_recording_active(false);
    ASSERT_FALSE(state.is_file_actively_recording(test_file),
                 "Released file must NOT be blocked once recording stops");

    // 6. Simulated Analyze UI guard block check
    bool analysis_blocked = false;
    state.set_recording_active(true, test_file);
    if (state.is_file_actively_recording(test_file)) {
        analysis_blocked = true;
    }
    ASSERT_TRUE(analysis_blocked, "UI Analyze panel guard must block analysis of in-flight file");

    std::error_code ec;
    std::filesystem::remove_all("test_tmp", ec);
}

void test_record_to_analyze_bridge() {
    std::cout << "[Test 5] Testing [ ⚡ Analyze This Recording Now ] Bridge...\n";
    AppState state;

    std::string rec_path = "recordings/hi_survey_001.cs16";
    state.set_last_recorded_file(rec_path);
    ASSERT_EQ(state.get_last_recorded_file(), rec_path, "get_last_recorded_file should match set value");

    // Trigger Bridge with auto_run = true
    state.switch_to_analyze(rec_path, true);
    ASSERT_EQ(state.get_section(), AppSection::Analyze, "Bridge must switch current section to Analyze");

    // Check and clear auto-analyze request
    std::string target_file;
    bool request_fired = state.check_and_clear_auto_analyze(target_file);
    ASSERT_TRUE(request_fired, "check_and_clear_auto_analyze must return true on first call");
    ASSERT_EQ(target_file, rec_path, "Target file passed to Analyze must match recorded file");

    // Subsequent call must return false (consume flag atomically)
    std::string target_file2;
    bool second_call = state.check_and_clear_auto_analyze(target_file2);
    ASSERT_FALSE(second_call, "Second call to check_and_clear_auto_analyze must return false");

    // Test Bridge with auto_run = false
    state.switch_to_record();
    state.switch_to_analyze(rec_path, false);
    ASSERT_EQ(state.get_section(), AppSection::Analyze, "Bridge without auto_run must switch section");
    std::string target_file3;
    ASSERT_FALSE(state.check_and_clear_auto_analyze(target_file3),
                 "check_and_clear_auto_analyze must be false when auto_run is false");
}

void test_multithreaded_concurrency() {
    std::cout << "[Test 6] Testing AppState thread safety and concurrent mutations...\n";
    AppState state;
    const int num_threads = 8;
    const int iterations_per_thread = 500;
    std::atomic<bool> start_gate{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&state, &start_gate, t, iterations_per_thread]() {
            while (!start_gate.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < iterations_per_thread; ++i) {
                std::string path = "test_path_" + std::to_string(t) + "_" + std::to_string(i) + ".cs16";
                if (i % 4 == 0) {
                    state.set_recording_active(true, path);
                    state.is_file_actively_recording(path);
                } else if (i % 4 == 1) {
                    state.set_recording_active(false);
                } else if (i % 4 == 2) {
                    state.switch_to_analyze(path, true);
                    std::string target;
                    state.check_and_clear_auto_analyze(target);
                } else {
                    state.switch_to_record();
                }
            }
        });
    }

    start_gate.store(true);
    for (auto& th : workers) {
        th.join();
    }

    // State should remain consistent and not corrupted or deadlock
    ASSERT_TRUE(true, "Concurrent worker execution completed without deadlock or crash");
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << "  RUNNING TEST SUITE: test_suite_state (AppState & Bridge Validation)\n";
    std::cout << "======================================================================\n";

    test_initial_state();
    test_mode_transitions();
    test_active_recording_registration();
    test_collision_prevention();
    test_record_to_analyze_bridge();
    test_multithreaded_concurrency();

    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "RESULTS: " << g_tests_passed << " / " << g_tests_run << " assertions PASSED.\n";
    if (g_tests_passed == g_tests_run) {
        std::cout << "[SUCCESS] test_suite_state PASSED (100%)\n";
        return 0;
    } else {
        std::cerr << "[FAILURE] " << (g_tests_run - g_tests_passed) << " assertions FAILED!\n";
        return 1;
    }
}
