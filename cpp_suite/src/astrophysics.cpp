#include "astrophysics.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

namespace astro {

std::vector<double> Astrophysics::compute_doppler_velocity(const std::vector<double>& freqs_hz) {
    size_t n = freqs_hz.size();
    std::vector<double> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = SPEED_OF_LIGHT_KMS * ((HYDROGEN_REST_FREQ_HZ - freqs_hz[i]) / HYDROGEN_REST_FREQ_HZ);
    }
    return v;
}

// Invert 3x3 symmetric matrix A * inv = I
static bool invert_3x3(const double A[3][3], double inv[3][3]) {
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
               - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);

    if (std::abs(det) < 1e-15) return false;
    double inv_det = 1.0 / det;

    inv[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) * inv_det;
    inv[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * inv_det;
    inv[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * inv_det;

    inv[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) * inv_det;
    inv[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * inv_det;
    inv[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * inv_det;

    inv[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) * inv_det;
    inv[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * inv_det;
    inv[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * inv_det;

    return true;
}

GaussianFit Astrophysics::fit_gaussian(
    const std::vector<double>& vel_kms,
    const std::vector<double>& temp_k,
    const std::vector<bool>& hi_window
) {
    GaussianFit res;
    size_t n_all = vel_kms.size();
    res.fitted_curve.assign(n_all, 0.0);

    // Extract subset inside HI window
    std::vector<double> v_sub, t_sub;
    for (size_t i = 0; i < n_all; ++i) {
        if (hi_window[i]) {
            v_sub.push_back(vel_kms[i]);
            t_sub.push_back(temp_k[i]);
        }
    }

    if (v_sub.size() < 5) return res;

    // Initial non-parametric estimate
    size_t max_idx = 0;
    double max_t = t_sub[0];
    for (size_t i = 1; i < t_sub.size(); ++i) {
        if (t_sub[i] > max_t) {
            max_t = t_sub[i];
            max_idx = i;
        }
    }

    // Handle weak/negative peak or no emission: fallback gracefully
    if (max_t <= 0.0) {
        res.success = false;
        res.amplitude_k = 0.0;
        res.v_center_kms = v_sub[max_idx];
        res.fwhm_kms = 25.0;
        res.sigma_kms = 25.0 / GAUSSIAN_SIGMA_TO_FWHM;
        return res;
    }

    double a_init = max_t;
    double v0_init = v_sub[max_idx];
    double half_max = a_init * 0.5;

    double v_min_half = 1e9, v_max_half = -1e9;
    int count_half = 0;
    for (size_t i = 0; i < t_sub.size(); ++i) {
        if (t_sub[i] >= half_max) {
            v_min_half = std::min(v_min_half, v_sub[i]);
            v_max_half = std::max(v_max_half, v_sub[i]);
            count_half++;
        }
    }

    double fwhm_init = (count_half >= 2) ? (v_max_half - v_min_half) : 25.0;
    double sigma_init = std::max(2.0, fwhm_init / GAUSSIAN_SIGMA_TO_FWHM);

    double a = a_init;
    double v0 = v0_init;
    double sigma = sigma_init;

    // Levenberg-Marquardt optimizer for 3 parameters: p = [a, v0, sigma]
    double lambda = 1.0;
    size_t m = v_sub.size();

    auto eval_chi2 = [&](double cur_a, double cur_v0, double cur_sig) {
        double sum_sq = 0.0;
        for (size_t i = 0; i < m; ++i) {
            double diff = v_sub[i] - cur_v0;
            double model = cur_a * std::exp(-0.5 * (diff * diff) / (cur_sig * cur_sig));
            double r = t_sub[i] - model;
            sum_sq += r * r;
        }
        return sum_sq;
    };

    double initial_chi2 = eval_chi2(a, v0, sigma);
    double current_chi2 = initial_chi2;
    const double max_a_bound = std::max(10.0, a_init * 2.5 + 5.0);

    bool converged = false;
    int successful_steps = 0;

    for (int iter = 0; iter < 100; ++iter) {
        double JTJ[3][3] = {{0}};
        double JTr[3] = {0};

        for (size_t i = 0; i < m; ++i) {
            double diff = v_sub[i] - v0;
            double sig2 = sigma * sigma;
            double exp_val = std::exp(-0.5 * (diff * diff) / sig2);
            double model = a * exp_val;
            double r = t_sub[i] - model;

            // Partial derivatives
            double d_a = exp_val;
            double d_v0 = a * (diff / sig2) * exp_val;
            double d_sig = a * (diff * diff) / (sig2 * sigma) * exp_val;

            double J[3] = {d_a, d_v0, d_sig};
            for (int r_idx = 0; r_idx < 3; ++r_idx) {
                JTr[r_idx] += J[r_idx] * r;
                for (int c_idx = 0; c_idx < 3; ++c_idx) {
                    JTJ[r_idx][c_idx] += J[r_idx] * J[c_idx];
                }
            }
        }

        // Apply damping: JTJ + lambda * diag(JTJ)
        double A_damped[3][3];
        for (int r_idx = 0; r_idx < 3; ++r_idx) {
            for (int c_idx = 0; c_idx < 3; ++c_idx) {
                A_damped[r_idx][c_idx] = JTJ[r_idx][c_idx];
            }
            A_damped[r_idx][r_idx] += lambda * (JTJ[r_idx][r_idx] + 1e-3);
        }

        double inv_A[3][3];
        if (!invert_3x3(A_damped, inv_A)) {
            lambda *= 4.0;
            continue;
        }

        double delta[3] = {0};
        for (int r_idx = 0; r_idx < 3; ++r_idx) {
            for (int c_idx = 0; c_idx < 3; ++c_idx) {
                delta[r_idx] += inv_A[r_idx][c_idx] * JTr[c_idx];
            }
        }

        // Trust-region step limiter to maintain numerical stability across disparate parameter scales
        double step_scale = 1.0;
        if (std::abs(delta[0]) > 0.5 * a) step_scale = std::min(step_scale, (0.5 * a) / std::abs(delta[0]));
        if (std::abs(delta[1]) > 5.0) step_scale = std::min(step_scale, 5.0 / std::abs(delta[1]));
        if (std::abs(delta[2]) > 0.5 * sigma) step_scale = std::min(step_scale, (0.5 * sigma) / std::abs(delta[2]));

        delta[0] *= step_scale;
        delta[1] *= step_scale;
        delta[2] *= step_scale;

        double new_a = std::clamp(a + delta[0], 0.1, max_a_bound);
        double new_v0 = std::clamp(v0 + delta[1], -250.0, 250.0);
        double new_sig = std::clamp(sigma + delta[2], 0.5, 100.0);

        double new_chi2 = eval_chi2(new_a, new_v0, new_sig);
        if (new_chi2 < current_chi2) {
            current_chi2 = new_chi2;
            a = new_a;
            v0 = new_v0;
            sigma = new_sig;
            successful_steps++;
            lambda = std::max(1e-7, lambda * 0.5);
            if (std::abs(delta[0]) < 1e-4 && std::abs(delta[1]) < 1e-4 && std::abs(delta[2]) < 1e-4) {
                converged = true;
                break; // Converged
            }
        } else {
            lambda = std::min(1e5, lambda * 3.0);
        }
    }

    // Determine fit validity: must improve chi2 and stay off degenerate boundaries
    bool fit_valid = (successful_steps > 0 || converged) &&
                     (current_chi2 <= initial_chi2) &&
                     (a > 0.12 && a < max_a_bound * 0.99) &&
                     (sigma > 1.05 && sigma < 98.0);

    if (fit_valid) {
        res.success = true;
        res.amplitude_k = a;
        res.v_center_kms = v0;
        res.sigma_kms = std::abs(sigma);
        res.fwhm_kms = GAUSSIAN_SIGMA_TO_FWHM * res.sigma_kms;

        // Evaluate full curve
        for (size_t i = 0; i < n_all; ++i) {
            double diff = vel_kms[i] - v0;
            res.fitted_curve[i] = a * std::exp(-0.5 * (diff * diff) / (sigma * sigma));
        }
    } else {
        // Graceful non-parametric fallback
        res.success = false;
        res.amplitude_k = a_init;
        res.v_center_kms = v0_init;
        res.sigma_kms = sigma_init;
        res.fwhm_kms = fwhm_init;
        std::fill(res.fitted_curve.begin(), res.fitted_curve.end(), 0.0);
    }

    return res;

    return res;
}

AstrophysicalMetrics Astrophysics::calculate_metrics(
    const std::vector<double>& freqs_mhz,
    const std::vector<double>& vel_kms,
    const std::vector<double>& calibrated_k,
    const std::vector<double>& calibrated_db,
    const AstroConfig& config
) {
    AstrophysicalMetrics m;
    int nfft = static_cast<int>(freqs_mhz.size());
    if (nfft == 0) return m;

    double hi_half_mhz = config.hi_protection_half_width_khz / 1e3;
    std::vector<bool> hi_window(nfft, false);
    for (int i = 0; i < nfft; ++i) {
        if (freqs_mhz[i] >= (HYDROGEN_REST_FREQ_MHZ - hi_half_mhz) &&
            freqs_mhz[i] <= (HYDROGEN_REST_FREQ_MHZ + hi_half_mhz)) {
            hi_window[i] = true;
        }
    }

    // Line-free noise floor estimation: |v| in [line_free_velocity_kms, 200]
    std::vector<double> free_k, free_db;
    for (int i = 0; i < nfft; ++i) {
        double v = vel_kms[i];
        if (v >= -200.0 && v <= 200.0 && std::abs(v) >= config.line_free_velocity_kms) {
            free_k.push_back(calibrated_k[i]);
            free_db.push_back(calibrated_db[i]);
        }
    }

    if (free_k.size() < 10) {
        // Fallback: use passband excluding HI
        int edge_bins = static_cast<int>(nfft * config.passband_edge_fraction);
        for (int i = edge_bins; i < nfft - edge_bins; ++i) {
            if (!hi_window[i]) {
                free_k.push_back(calibrated_k[i]);
                free_db.push_back(calibrated_db[i]);
            }
        }
    }

    auto calc_std = [](const std::vector<double>& v) {
        if (v.size() < 2) return 0.05;
        double sum = 0.0;
        for (double val : v) sum += val;
        double mean = sum / v.size();
        double sum_sq = 0.0;
        for (double val : v) sum_sq += (val - mean) * (val - mean);
        return std::sqrt(sum_sq / (v.size() - 1));
    };

    m.noise_std_k = calc_std(free_k);
    m.noise_std_db = calc_std(free_db);

    // Peak detection in HI window
    int peak_idx = nfft / 2;
    double max_k = -1e9;
    bool found_peak = false;
    for (int i = 0; i < nfft; ++i) {
        if (hi_window[i] && calibrated_k[i] > max_k) {
            max_k = calibrated_k[i];
            peak_idx = i;
            found_peak = true;
        }
    }

    if (found_peak) {
        m.peak_freq_mhz = freqs_mhz[peak_idx];
        m.peak_vel_kms = vel_kms[peak_idx];
        m.peak_temp_k = calibrated_k[peak_idx];
        m.peak_power_db = calibrated_db[peak_idx];
    } else {
        m.peak_freq_mhz = HYDROGEN_REST_FREQ_MHZ;
        m.peak_vel_kms = 0.0;
        m.peak_temp_k = 0.0;
        m.peak_power_db = 0.0;
    }

    m.snr = m.peak_temp_k / std::max(1e-6, m.noise_std_k);

    // Integrated line flux: W = sum(max(0, T_A)) * dv [K * km/s]
    double dv = (nfft > 1) ? std::abs(vel_kms[1] - vel_kms[0]) : 1.0;
    double integral_w = 0.0;
    for (int i = 0; i < nfft; ++i) {
        if (hi_window[i] && calibrated_k[i] > 0.0) {
            integral_w += calibrated_k[i] * dv;
        }
    }
    m.integrated_flux_k_kms = integral_w;
    m.column_density_cm2 = HI_COLUMN_DENSITY_FACTOR * integral_w;

    // Gaussian Line Fit
    m.gauss_fit = fit_gaussian(vel_kms, calibrated_k, hi_window);
    m.fwhm_kms = m.gauss_fit.fwhm_kms;
    m.sigma_kms = m.gauss_fit.sigma_kms;

    // SDR LO Offset and DC notch analysis
    m.lo_offset_khz = (config.center_freq_hz - HYDROGEN_REST_FREQ_HZ) / 1e3;
    m.dc_bin = nfft / 2;

    int closest_hi_bin = 0;
    double min_f_diff = 1e12;
    for (int i = 0; i < nfft; ++i) {
        double diff = std::abs(freqs_mhz[i] - HYDROGEN_REST_FREQ_MHZ);
        if (diff < min_f_diff) {
            min_f_diff = diff;
            closest_hi_bin = i;
        }
    }
    m.hi_bin = closest_hi_bin;
    m.bin_separation = std::abs(m.dc_bin - m.hi_bin);

    return m;
}

} // namespace astro
