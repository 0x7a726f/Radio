#pragma once

#include <vector>
#include <string>
#include "config.h"

namespace astro {

struct GaussianFit {
    bool success = false;
    double amplitude_k = 0.0;
    double v_center_kms = 0.0;
    double sigma_kms = 0.0;
    double fwhm_kms = 0.0;
    std::vector<double> fitted_curve; // Evaluated over entire vel_kms axis
};

struct AstrophysicalMetrics {
    double peak_freq_mhz = 0.0;
    double peak_vel_kms = 0.0;
    double peak_temp_k = 0.0;
    double peak_power_db = 0.0;
    double noise_std_k = 0.0;
    double noise_std_db = 0.0;
    double snr = 0.0;
    double integrated_flux_k_kms = 0.0;
    double column_density_cm2 = 0.0;
    double fwhm_kms = 0.0;
    double sigma_kms = 0.0;
    GaussianFit gauss_fit;
    double lo_offset_khz = 0.0;
    int bin_separation = 0;
    int dc_bin = 0;
    int hi_bin = 0;
};

class Astrophysics {
public:
    // Convert frequency array (Hz) to Topocentric Doppler Radial Velocity in km/s:
    // v = c * (f_rest - f) / f_rest
    static std::vector<double> compute_doppler_velocity(const std::vector<double>& freqs_hz);

    // Fit 1D Gaussian profile to HI emission line using Levenberg-Marquardt
    static GaussianFit fit_gaussian(
        const std::vector<double>& vel_kms,
        const std::vector<double>& temp_k,
        const std::vector<bool>& hi_window
    );

    // Compute comprehensive astrophysical metrics
    static AstrophysicalMetrics calculate_metrics(
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& vel_kms,
        const std::vector<double>& calibrated_k,
        const std::vector<double>& calibrated_db,
        const AstroConfig& config
    );
};

} // namespace astro
