#include "test_framework.h"
#include "baseline_fitter.h"
#include <cmath>
#include <vector>
#include <numeric>

using namespace astro;

void test_pentadiagonal_solver_accuracy() {
    TEST_CASE("Pentadiagonal Solver Accuracy: A * x == rhs for SPD Pentadiagonal");

    // Construct a known symmetric positive-definite pentadiagonal matrix A of size N
    const int n = 128;
    std::vector<double> diag(n);
    std::vector<double> sub1(n - 1);
    std::vector<double> sub2(n - 2);

    // Strictly diagonally dominant to ensure positive-definiteness:
    // diag[i] > |sub1[i]| + |sub1[i-1]| + |sub2[i]| + |sub2[i-2]|
    for (int i = 0; i < n; ++i) diag[i] = 12.0 + std::sin(i * 0.1);
    for (int i = 0; i < n - 1; ++i) sub1[i] = -3.0 + 0.2 * std::cos(i * 0.2);
    for (int i = 0; i < n - 2; ++i) sub2[i] = 1.0 + 0.1 * std::sin(i * 0.3);

    // Known exact solution x_true
    std::vector<double> x_true(n);
    for (int i = 0; i < n; ++i) {
        x_true[i] = std::sin(2.0 * 3.14159265 * i / n) + 2.5;
    }

    // Multiply rhs = A * x_true
    std::vector<double> rhs(n, 0.0);
    for (int i = 0; i < n; ++i) {
        rhs[i] += diag[i] * x_true[i];
        if (i < n - 1) {
            rhs[i] += sub1[i] * x_true[i + 1];
            rhs[i + 1] += sub1[i] * x_true[i];
        }
        if (i < n - 2) {
            rhs[i] += sub2[i] * x_true[i + 2];
            rhs[i + 2] += sub2[i] * x_true[i];
        }
    }

    // Solve using fast O(N) LDL^T solver
    std::vector<double> x_computed(n, 0.0);
    solve_pentadiagonal_symmetric(n, diag.data(), sub1.data(), sub2.data(), rhs.data(), x_computed.data());

    // Verify ||x_computed - x_true||_inf < 1e-10
    double max_err = 0.0;
    for (int i = 0; i < n; ++i) {
        double err = std::abs(x_computed[i] - x_true[i]);
        if (err > max_err) max_err = err;
    }

    ASSERT_NEAR(max_err, 0.0, 1e-10);
    TEST_PASS();
}

void test_whittaker_als_smoothing() {
    TEST_CASE("Whittaker ALS Baseline Smoothing with Protected HI Window");

    const int n = 512;
    std::vector<double> freqs_mhz(n);
    std::vector<double> raw_power(n);
    std::vector<bool> passband_mask(n, true);
    std::vector<bool> hi_window(n, false);

    // Skirts
    for (int i = 0; i < 30; ++i) passband_mask[i] = false;
    for (int i = n - 30; i < n; ++i) passband_mask[i] = false;

    // Simulate curved instrumental baseline + HI emission peak at center
    int hi_center = n / 2;
    for (int i = 0; i < n; ++i) {
        freqs_mhz[i] = 1420.0 + (i - n / 2) * 0.001; // ~1419.74 to ~1420.25 MHz
        double baseline_curve = 100.0 + 20.0 * std::sin(3.14159 * i / n);
        raw_power[i] = baseline_curve;

        // HI emission peak (+15 K)
        if (std::abs(i - hi_center) <= 15) {
            hi_window[i] = true;
            double diff = (i - hi_center);
            raw_power[i] += 15.0 * std::exp(-0.5 * diff * diff / 9.0);
        }
    }

    AstroConfig cfg;
    cfg.baseline_method = BaselineMethod::ALS;
    cfg.als_lambda = 1000.0;
    cfg.als_p = 0.02;

    BaselineResults bl = BaselineFitter::fit(raw_power, freqs_mhz, cfg);

    ASSERT_EQ(bl.baseline_power.size(), static_cast<size_t>(n));

    // Baseline under the HI line must NOT track the peak!
    // The peak raw power is ~120 + 15 = 135
    // Baseline at hi_center must stay around ~120
    ASSERT_NEAR(bl.baseline_power[hi_center], 120.0, 3.0);
    ASSERT_TRUE(bl.baseline_power[hi_center] < raw_power[hi_center] - 10.0);

    TEST_PASS();
}

void test_polynomial_baseline_fitting() {
    TEST_CASE("Polynomial Baseline Fitting (Iterative Sigma-Clipped)");

    const int n = 512;
    std::vector<double> freqs_mhz(n);
    std::vector<double> raw_power(n);
    std::vector<bool> passband_mask(n, true);
    std::vector<bool> hi_window(n, false);

    for (int i = 0; i < 25; ++i) passband_mask[i] = false;
    for (int i = n - 25; i < n; ++i) passband_mask[i] = false;

    int hi_center = n / 2;
    for (int i = 0; i < n; ++i) {
        freqs_mhz[i] = 1420.0 + (i - n / 2) * 0.001;
        // True cubic baseline
        double x = (i - n / 2.0) / (n / 2.0);
        double baseline = 50.0 + 5.0 * x - 8.0 * x * x + 3.0 * x * x * x;
        raw_power[i] = baseline;

        if (std::abs(i - hi_center) <= 10) {
            hi_window[i] = true;
            raw_power[i] += 12.0; // Emission spike
        }
    }

    AstroConfig cfg;
    cfg.baseline_method = BaselineMethod::Poly;
    cfg.poly_degree = 4;

    BaselineResults bl = BaselineFitter::fit(raw_power, freqs_mhz, cfg);

    // Check passband fit accuracy outside the HI line
    double max_passband_err = 0.0;
    for (int i = 50; i < n - 50; ++i) {
        if (!hi_window[i]) {
            double err = std::abs(bl.baseline_power[i] - raw_power[i]);
            if (err > max_passband_err) max_passband_err = err;
        }
    }

    ASSERT_NEAR(max_passband_err, 0.0, 1.5);
    TEST_PASS();
}

void test_stage2_velocity_zeroing() {
    TEST_CASE("Stage 2 Local Baseline Refinement & Velocity Zeroing");

    const int n = 200;
    std::vector<double> vel_kms(n);
    std::vector<double> cal_k(n);
    std::vector<double> cal_db(n);

    // Velocity from -100 km/s to +100 km/s (1 km/s per bin)
    // Add linear slope + offset in dB
    double true_slope = 0.002;
    double true_intercept = 0.15;

    for (int i = 0; i < n; ++i) {
        vel_kms[i] = -100.0 + i * 1.0;
        double trend_db = true_slope * vel_kms[i] + true_intercept;
        cal_db[i] = trend_db;
        // Emission line in |v| < 20 km/s
        if (std::abs(vel_kms[i]) < 10.0) {
            cal_db[i] += 0.5 * std::exp(-0.5 * vel_kms[i] * vel_kms[i] / 9.0);
        }
        cal_k[i] = 100.0 * (std::pow(10.0, cal_db[i] / 10.0) - 1.0);
    }

    AstroConfig cfg;
    cfg.system_temp_k = 100.0;
    cfg.line_free_velocity_kms = 40.0;

    double slope = 0.0, intercept = 0.0;
    BaselineFitter::refine_local(vel_kms, cal_k, cal_db, cfg, slope, intercept);

    ASSERT_NEAR(slope, true_slope, 1e-4);
    ASSERT_NEAR(intercept, true_intercept, 1e-4);

    // Line-free channels (|v| > 40 km/s) must now average to ~0.000 K and ~0.000 dB
    double sum_free_k = 0.0;
    double sum_free_db = 0.0;
    int count_free = 0;
    for (int i = 0; i < n; ++i) {
        if (std::abs(vel_kms[i]) >= 40.0) {
            sum_free_k += cal_k[i];
            sum_free_db += cal_db[i];
            count_free++;
        }
    }

    double mean_free_k = sum_free_k / count_free;
    double mean_free_db = sum_free_db / count_free;

    ASSERT_NEAR(mean_free_k, 0.0, 1e-4);
    ASSERT_NEAR(mean_free_db, 0.0, 1e-4);

    TEST_PASS();
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " RUNNING BASELINE SOLVERS TEST SUITE\n";
    std::cout << "=======================================================\n";

    test_pentadiagonal_solver_accuracy();
    test_whittaker_als_smoothing();
    test_polynomial_baseline_fitting();
    test_stage2_velocity_zeroing();

    test::report_summary("Baseline Solvers");
    return test::exit_code();
}
