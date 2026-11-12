#include "test_framework.h"
#include "astrophysics.h"
#include "constants.h"
#include <cmath>
#include <vector>
#include <random>

using namespace astro;

void test_doppler_velocity() {
    TEST_CASE("Doppler Velocity Calculation (Topocentric)");

    std::vector<double> freqs_hz = {
        HYDROGEN_REST_FREQ_HZ,                     // Exactly at rest -> v = 0 km/s
        HYDROGEN_REST_FREQ_HZ - 4737.954,          // Redshifted (-1 kHz) -> v > 0 km/s (~+1.0 km/s)
        HYDROGEN_REST_FREQ_HZ + 4737.954           // Blueshifted (+1 kHz) -> v < 0 km/s (~-1.0 km/s)
    };

    auto vels = Astrophysics::compute_doppler_velocity(freqs_hz);

    ASSERT_EQ(vels.size(), 3u);
    ASSERT_NEAR(vels[0], 0.0, 1e-6);
    ASSERT_NEAR(vels[1], 1.0, 1e-3);
    ASSERT_NEAR(vels[2], -1.0, 1e-3);

    TEST_PASS();
}

void test_gaussian_fit_convergence() {
    TEST_CASE("Gaussian Profile Curve Fitting: Successful Convergence");

    const int n = 150;
    std::vector<double> vel_kms(n);
    std::vector<double> temp_k(n);
    std::vector<bool> hi_window(n, true);

    const double true_a = 8.5;
    const double true_v0 = 12.3;
    const double true_sigma = 3.2;
    const double true_fwhm = GAUSSIAN_SIGMA_TO_FWHM * true_sigma;

    for (int i = 0; i < n; ++i) {
        vel_kms[i] = -30.0 + i * 0.6; // -30 to +60 km/s
        double diff = vel_kms[i] - true_v0;
        temp_k[i] = true_a * std::exp(-0.5 * (diff * diff) / (true_sigma * true_sigma));
    }

    GaussianFit fit = Astrophysics::fit_gaussian(vel_kms, temp_k, hi_window);

    ASSERT_TRUE(fit.success);
    ASSERT_NEAR(fit.amplitude_k, true_a, 1e-2);
    ASSERT_NEAR(fit.v_center_kms, true_v0, 1e-2);
    ASSERT_NEAR(fit.sigma_kms, true_sigma, 1e-2);
    ASSERT_NEAR(fit.fwhm_kms, true_fwhm, 1e-2);

    TEST_PASS();
}

void test_gaussian_fit_noise_fallback() {
    TEST_CASE("Gaussian Profile Curve Fitting: Graceful Fallback on Pure Noise");

    const int n = 150;
    std::vector<double> vel_kms(n);
    std::vector<double> temp_k(n);
    std::vector<bool> hi_window(n, true);

    // Pure zero/negative noise: no emission peak
    for (int i = 0; i < n; ++i) {
        vel_kms[i] = -30.0 + i * 0.6;
        temp_k[i] = -0.5 + 0.1 * std::sin(i * 0.5);
    }

    GaussianFit fit = Astrophysics::fit_gaussian(vel_kms, temp_k, hi_window);

    ASSERT_FALSE(fit.success);
    ASSERT_EQ(fit.amplitude_k, 0.0);
    ASSERT_NEAR(fit.fwhm_kms, 25.0, 1e-3); // Default Milky Way line fallback

    TEST_PASS();
}

void test_integrated_flux_and_column_density() {
    TEST_CASE("Integrated Flux W & Neutral Hydrogen Column Density N_HI");

    const int n = 500;
    std::vector<double> freqs_mhz(n);
    std::vector<double> vel_kms(n);
    std::vector<double> cal_k(n, 0.0);
    std::vector<double> cal_db(n, 0.0);

    const double true_a = 10.0;
    const double true_sigma = 4.0;
    const double dv = 0.2; // 0.2 km/s per bin

    // Analytical integral of Gaussian: sqrt(2 * pi) * a * sigma
    const double expected_flux_analytical = std::sqrt(2.0 * 3.14159265358979323846) * true_a * true_sigma;
    const double expected_nhi_analytical = HI_COLUMN_DENSITY_FACTOR * expected_flux_analytical;

    for (int i = 0; i < n; ++i) {
        vel_kms[i] = -50.0 + i * dv;
        // Map back to frequency around 1420.405752 MHz
        freqs_mhz[i] = HYDROGEN_REST_FREQ_MHZ * (1.0 - vel_kms[i] / SPEED_OF_LIGHT_KMS);

        double diff = vel_kms[i] - 0.0;
        double val = true_a * std::exp(-0.5 * (diff * diff) / (true_sigma * true_sigma));
        cal_k[i] = val;
        cal_db[i] = 10.0 * std::log10(1.0 + val / 100.0);
    }

    AstroConfig cfg;
    cfg.hi_protection_half_width_khz = 250.0; // Wide enough to cover -50 to +50 km/s
    cfg.line_free_velocity_kms = 30.0;

    AstrophysicalMetrics m = Astrophysics::calculate_metrics(freqs_mhz, vel_kms, cal_k, cal_db, cfg);

    // Discrete numerical integration sum(T_A * dv) matches analytical Gaussian integral within ~0.5%
    ASSERT_NEAR(m.integrated_flux_k_kms, expected_flux_analytical, 0.5);
    ASSERT_NEAR(m.column_density_cm2, expected_nhi_analytical, 1e17);
    ASSERT_NEAR(m.peak_temp_k, true_a, 0.1);

    TEST_PASS();
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " RUNNING ASTROPHYSICS TEST SUITE\n";
    std::cout << "=======================================================\n";

    test_doppler_velocity();
    test_gaussian_fit_convergence();
    test_gaussian_fit_noise_fallback();
    test_integrated_flux_and_column_density();

    test::report_summary("Astrophysics");
    return test::exit_code();
}
