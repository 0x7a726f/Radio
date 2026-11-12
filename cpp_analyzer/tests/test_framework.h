#pragma once

#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>

namespace test {

inline int total_tests = 0;
inline int passed_tests = 0;
inline int failed_tests = 0;

inline void report_summary(const std::string& suite_name) {
    std::cout << "\n=======================================================\n";
    std::cout << " TEST SUITE: " << suite_name << "\n";
    std::cout << " Result: " << passed_tests << " Passed, " << failed_tests << " Failed (Total: " << total_tests << ")\n";
    if (failed_tests == 0) {
        std::cout << " STATUS: ALL TESTS PASSED (100%)\n";
    } else {
        std::cout << " STATUS: FAILED!\n";
    }
    std::cout << "=======================================================\n\n";
}

inline int exit_code() {
    return (failed_tests == 0) ? 0 : 1;
}

#define TEST_CASE(name) \
    std::cout << "\n[RUN] " << name << "..." << std::endl; \
    test::total_tests++;

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            test::failed_tests++; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            std::cerr << "  FAIL: NOT (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            test::failed_tests++; \
            return; \
        } \
    } while (0)

#define ASSERT_NEAR(a, b, tol) \
    do { \
        double diff = std::abs(static_cast<double>(a) - static_cast<double>(b)); \
        if (diff > (tol)) { \
            std::cerr << "  FAIL: |" << #a << " - " << #b << "| = " << diff \
                      << " > tolerance " << (tol) << " (" << (a) << " vs " << (b) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            test::failed_tests++; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "  FAIL: " << #a << " == " << #b << " (" << (a) << " != " << (b) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            test::failed_tests++; \
            return; \
        } \
    } while (0)

#define TEST_PASS() \
    do { \
        std::cout << "  PASS" << std::endl; \
        test::passed_tests++; \
    } while (0)

} // namespace test
