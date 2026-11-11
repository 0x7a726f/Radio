#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>
#include <iomanip>

namespace TestFramework {

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner r;
        return r;
    }

    struct TestCase {
        std::string name;
        std::function<void()> func;
    };

    void add_test(const std::string& name, std::function<void()> func) {
        tests_.push_back({name, func});
    }

    bool run_all(const std::string& suite_name) {
        std::cout << "\n======================================================================\n";
        std::cout << "  RUNNING TEST SUITE: " << suite_name << "\n";
        std::cout << "======================================================================\n";

        int passed = 0;
        int failed = 0;
        auto suite_start = std::chrono::high_resolution_clock::now();

        for (const auto& test : tests_) {
            current_test_failed_ = false;
            current_failure_msg_.clear();

            std::cout << "  [RUN ] " << test.name << " ... " << std::flush;
            auto t0 = std::chrono::high_resolution_clock::now();
            try {
                test.func();
            } catch (const std::exception& e) {
                current_test_failed_ = true;
                current_failure_msg_ = std::string("Unhandled exception: ") + e.what();
            } catch (...) {
                current_test_failed_ = true;
                current_failure_msg_ = "Unhandled unknown exception";
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (!current_test_failed_) {
                passed++;
                std::cout << " [PASS] (" << std::fixed << std::setprecision(2) << ms << " ms)\n";
            } else {
                failed++;
                std::cout << " [FAIL] (" << std::fixed << std::setprecision(2) << ms << " ms)\n";
                if (!current_failure_msg_.empty()) {
                    std::cout << "         " << current_failure_msg_ << "\n";
                }
            }
        }

        auto suite_end = std::chrono::high_resolution_clock::now();
        double total_s = std::chrono::duration<double>(suite_end - suite_start).count();

        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "  RESULTS: " << passed << " passed, " << failed << " failed (Total: "
                  << tests_.size() << " tests in " << std::fixed << std::setprecision(3) << total_s << " s)\n";
        if (failed == 0) {
            std::cout << "  ALL TESTS PASSED SUCCESSFULLY!\n";
        } else {
            std::cout << "  TEST SUITE COMPLETED WITH FAILURES!\n";
        }
        std::cout << "======================================================================\n\n";

        return (failed == 0);
    }

    void fail(const std::string& msg, const char* file, int line) {
        current_test_failed_ = true;
        std::ostringstream ss;
        ss << file << ":" << line << " -> Assertion failed: " << msg;
        current_failure_msg_ = ss.str();
        throw std::runtime_error(current_failure_msg_);
    }

private:
    std::vector<TestCase> tests_;
    bool current_test_failed_ = false;
    std::string current_failure_msg_;
};

struct TestRegistrar {
    TestRegistrar(const std::string& name, std::function<void()> func) {
        TestRunner::instance().add_test(name, func);
    }
};

} // namespace TestFramework

#define TEST_CASE(name) \
    static void test_func_##name(); \
    static TestFramework::TestRegistrar registrar_##name(#name, test_func_##name); \
    static void test_func_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            TestFramework::TestRunner::instance().fail(#cond " is FALSE", __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            TestFramework::TestRunner::instance().fail(#cond " is TRUE", __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::ostringstream _ss; \
            _ss << #a " (" << (a) << ") != " #b " (" << (b) << ")"; \
            TestFramework::TestRunner::instance().fail(_ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::ostringstream _ss; \
            _ss << "|" #a " (" << (a) << ") - " #b " (" << (b) << ")| = " \
                << std::abs((a) - (b)) << " > " #eps " (" << (eps) << ")"; \
            TestFramework::TestRunner::instance().fail(_ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_GT(a, b) \
    do { \
        if (!((a) > (b))) { \
            std::ostringstream _ss; \
            _ss << #a " (" << (a) << ") <= " #b " (" << (b) << ")"; \
            TestFramework::TestRunner::instance().fail(_ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_LT(a, b) \
    do { \
        if (!((a) < (b))) { \
            std::ostringstream _ss; \
            _ss << #a " (" << (a) << ") >= " #b " (" << (b) << ")"; \
            TestFramework::TestRunner::instance().fail(_ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)
