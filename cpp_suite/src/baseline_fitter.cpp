#include "baseline_fitter.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

namespace astro {

// Fast, stable, O(N) symmetric positive-definite pentadiagonal solver: A * x = rhs
// Factors A = L * D * L^T where L is unit lower triangular with bandwidth 2.
// Fully in-place solve in vector x, with persistent thread_local factorization buffers
// guaranteeing ZERO heap allocations in hot path.
void solve_pentadiagonal_symmetric(
    int n,
    const double* diag,   // A[i, i]
    const double* sub1,   // A[i+1, i]
    const double* sub2,   // A[i+2, i]
    const double* rhs,
    double* x
) {
    if (n <= 0) return;
    if (n == 1) {
        double d0 = (std::abs(diag[0]) < 1e-15) ? 1e-15 : diag[0];
        x[0] = rhs[0] / d0;
        return;
    }

    // Reusable thread-local factorization buffers: zero heap allocations in hot path
    thread_local std::vector<double> tl_D;
    thread_local std::vector<double> tl_L1;
    thread_local std::vector<double> tl_L2;

    if (static_cast<int>(tl_D.size()) < n) tl_D.resize(n);
    if (static_cast<int>(tl_L1.size()) < n - 1) tl_L1.resize(n - 1);
    if (static_cast<int>(tl_L2.size()) < n - 2) tl_L2.resize(n - 2);

    double* D = tl_D.data();
    double* L1 = tl_L1.data();
    double* L2 = tl_L2.data();

    // Factorization: A = L * D * L^T
    for (int i = 0; i < n; ++i) {
        double d_val = diag[i];
        if (i >= 1) {
            d_val -= L1[i - 1] * L1[i - 1] * D[i - 1];
        }
        if (i >= 2) {
            d_val -= L2[i - 2] * L2[i - 2] * D[i - 2];
        }
        // Strict positive-definiteness & numerical pivot stability
        if (d_val < 1e-15) d_val = 1e-15;
        D[i] = d_val;

        if (i < n - 1) {
            double num1 = sub1[i];
            if (i >= 1) {
                num1 -= L2[i - 1] * L1[i - 1] * D[i - 1];
            }
            L1[i] = num1 / d_val;
        }

        if (i < n - 2) {
            L2[i] = sub2[i] / d_val;
        }
    }

    // Forward substitution in-place: L * y = rhs -> store y in x
    for (int i = 0; i < n; ++i) {
        double val = rhs[i];
        if (i >= 1) {
            val -= L1[i - 1] * x[i - 1];
        }
        if (i >= 2) {
            val -= L2[i - 2] * x[i - 2];
        }
        x[i] = val;
    }

    // Diagonal solve in-place: D * z = y -> store z in x
    for (int i = 0; i < n; ++i) {
        x[i] /= D[i];
    }

    // Back substitution in-place: L^T * x = z -> update x backwards
    for (int i = n - 1; i >= 0; --i) {
        double val = x[i];
        if (i < n - 1) {
            val -= L1[i] * x[i + 1];
        }
        if (i < n - 2) {
            val -= L2[i] * x[i + 2];
        }
        x[i] = val;
    }
}

static double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    size_t n = v.size();
    size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    if (n % 2 != 0) {
        return v[mid];
    } else {
        std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
        return (v[mid - 1] + v[mid]) * 0.5;
    }
}

// 1D Gaussian smoothing
static std::vector<double> gaussian_smooth_1d(const std::vector<double>& input, double sigma) {
    if (sigma <= 0.0 || input.size() < 3) return input;
    int n = static_cast<int>(input.size());
    std::vector<double> output(n);

    int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<double> kernel(2 * radius + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        double g = std::exp(-0.5 * (i * i) / (sigma * sigma));
        kernel[i + radius] = g;
        sum += g;
    }
    for (double& k : kernel) k /= sum;

    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int k = -radius; k <= radius; ++k) {
            int idx = i + k;
            if (idx < 0) idx = 0;
            else if (idx >= n) idx = n - 1;
            acc += input[idx] * kernel[k + radius];
        }
        output[i] = acc;
    }
    return output;
}

std::vector<double> BaselineFitter::fit_als(
    const std::vector<double>& p_db,
    const std::vector<double>& freqs_mhz,
    const std::vector<bool>& passband_mask,
    const std::vector<bool>& hi_window,
    const AstroConfig& config
) {
    int n = static_cast<int>(p_db.size());
    if (n < 4) return p_db;

    std::vector<double> w(n, 1.0);

    for (int i = 0; i < n; ++i) {
        if (!passband_mask[i]) w[i] = 0.02;
        if (hi_window[i]) w[i] = 0.0001;
    }

    double lam = config.als_lambda;
    double p = config.als_p;

    // Second difference operator D has D^T D entries:
    // S_diag: [1, 5, 6, 6, ..., 6, 5, 1]
    // S_sub1: [-2, -4, -4, ..., -4, -2]
    // S_sub2: [1, 1, ..., 1]
    std::vector<double> s_diag(n, 6.0);
    s_diag[0] = 1.0;
    if (n > 1) s_diag[1] = 5.0;
    if (n > 2) s_diag[n - 2] = 5.0;
    if (n > 3) s_diag[n - 1] = 1.0;

    std::vector<double> s_sub1(n - 1, -4.0);
    if (n > 1) s_sub1[0] = -2.0;
    if (n > 2) s_sub1[n - 2] = -2.0;

    std::vector<double> s_sub2(n - 2, 1.0);

    std::vector<double> A_diag(n);
    std::vector<double> A_sub1(n - 1);
    std::vector<double> A_sub2(n - 2);
    for (int i = 0; i < n - 1; ++i) A_sub1[i] = lam * s_sub1[i];
    for (int i = 0; i < n - 2; ++i) A_sub2[i] = lam * s_sub2[i];

    std::vector<double> rhs(n);
    std::vector<double> z(n, 0.0);

    const int iterations = 8;
    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < n; ++i) {
            A_diag[i] = w[i] + lam * s_diag[i];
            rhs[i] = w[i] * p_db[i];
        }

        solve_pentadiagonal_symmetric(n, A_diag.data(), A_sub1.data(), A_sub2.data(), rhs.data(), z.data());

        for (int i = 0; i < n; ++i) {
            w[i] = (p_db[i] > z[i]) ? p : (1.0 - p);
            if (!passband_mask[i]) w[i] = 0.02;
            if (hi_window[i]) w[i] = 0.0001;
        }
    }

    // Center baseline on the median of clean passband noise residuals
    std::vector<double> clean_residuals;
    clean_residuals.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            clean_residuals.push_back(p_db[i] - z[i]);
        }
    }

    double noise_offset = median_of(clean_residuals);
    std::vector<double> baseline_db(n);
    for (int i = 0; i < n; ++i) {
        baseline_db[i] = z[i] + noise_offset;
    }
    return baseline_db;
}

// Polynomial fitting via normal equations with adaptive Tikhonov regularization
std::vector<double> BaselineFitter::fit_poly(
    const std::vector<double>& p_db,
    const std::vector<double>& freqs_mhz,
    const std::vector<bool>& passband_mask,
    const std::vector<bool>& hi_window,
    const AstroConfig& config
) {
    int n = static_cast<int>(p_db.size());
    if (n < 4) return p_db;

    int deg = std::clamp(config.poly_degree, 2, 14);
    int m = deg + 1; // number of coefficients

    // Normalize frequencies on passband
    double mean_f = 0.0;
    double std_f = 0.0;
    int count_pb = 0;
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i]) {
            mean_f += freqs_mhz[i];
            count_pb++;
        }
    }
    if (count_pb < m) return p_db;
    mean_f /= count_pb;

    for (int i = 0; i < n; ++i) {
        if (passband_mask[i]) {
            double df = freqs_mhz[i] - mean_f;
            std_f += df * df;
        }
    }
    std_f = (count_pb > 1) ? std::sqrt(std_f / (count_pb - 1)) : 1.0;
    if (std_f < 1e-6) std_f = 1.0;

    std::vector<double> x_norm(n);
    for (int i = 0; i < n; ++i) {
        x_norm[i] = (freqs_mhz[i] - mean_f) / std_f;
    }

    std::vector<double> weights(n, 0.0);
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            weights[i] = 1.0;
        }
    }

    std::vector<double> coeffs(m, 0.0);
    coeffs[0] = median_of(p_db); // Safe initial baseline level

    for (int iter = 0; iter < 6; ++iter) {
        // Build weighted normal equations: (X^T W X) c = X^T W y
        std::vector<double> XTWX_orig(m * m, 0.0);
        std::vector<double> XTWy(m, 0.0);

        for (int i = 0; i < n; ++i) {
            double wi = weights[i];
            if (wi <= 0.0) continue;
            double yi = p_db[i];

            // Evaluate powers of x_norm[i]
            std::vector<double> xp(m);
            xp[0] = 1.0;
            for (int p = 1; p < m; ++p) xp[p] = xp[p - 1] * x_norm[i];

            for (int r = 0; r < m; ++r) {
                XTWy[r] += xp[r] * wi * yi;
                for (int c = 0; c < m; ++c) {
                    XTWX_orig[r * m + c] += xp[r] * wi * xp[c];
                }
            }
        }

        // Adaptive Cholesky solve with ridge damping
        bool cholesky_ok = false;
        double lambda = 1e-6;
        std::vector<double> L(m * m, 0.0);

        for (int attempt = 0; attempt < 5 && !cholesky_ok; ++attempt) {
            std::vector<double> A = XTWX_orig;
            for (int r = 0; r < m; ++r) {
                A[r * m + r] += lambda * (1.0 + XTWX_orig[r * m + r]);
            }

            std::fill(L.begin(), L.end(), 0.0);
            cholesky_ok = true;
            for (int i = 0; i < m; ++i) {
                for (int j = 0; j <= i; ++j) {
                    double sum = 0.0;
                    for (int k = 0; k < j; ++k) sum += L[i * m + k] * L[j * m + k];
                    if (i == j) {
                        double val = A[i * m + i] - sum;
                        if (val <= 1e-12) { cholesky_ok = false; break; }
                        L[i * m + j] = std::sqrt(val);
                    } else {
                        L[i * m + j] = (A[i * m + j] - sum) / L[j * m + j];
                    }
                }
                if (!cholesky_ok) break;
            }
            if (!cholesky_ok) lambda *= 10.0;
        }

        if (cholesky_ok) {
            std::vector<double> y_sol(m);
            for (int i = 0; i < m; ++i) {
                double sum = 0.0;
                for (int k = 0; k < i; ++k) sum += L[i * m + k] * y_sol[k];
                y_sol[i] = (XTWy[i] - sum) / L[i * m + i];
            }
            for (int i = m - 1; i >= 0; --i) {
                double sum = 0.0;
                for (int k = i + 1; k < m; ++k) sum += L[k * m + i] * coeffs[k];
                coeffs[i] = (y_sol[i] - sum) / L[i * m + i];
            }
        }

        // Sigma clipping outlier rejection
        std::vector<double> res_clean;
        for (int i = 0; i < n; ++i) {
            if (passband_mask[i] && !hi_window[i]) {
                double p_eval = 0.0;
                double cur_x = 1.0;
                for (int k = 0; k < m; ++k) {
                    p_eval += coeffs[k] * cur_x;
                    cur_x *= x_norm[i];
                }
                res_clean.push_back(p_db[i] - p_eval);
            }
        }
        double med_r = median_of(res_clean);
        double std_r = 0.0;
        for (double r : res_clean) std_r += (r - med_r) * (r - med_r);
        std_r = (!res_clean.empty()) ? std::sqrt(std_r / res_clean.size()) : 0.05;
        if (std_r < 1e-4) std_r = 0.05;

        for (int i = 0; i < n; ++i) {
            if (passband_mask[i] && !hi_window[i]) {
                double p_eval = 0.0;
                double cur_x = 1.0;
                for (int k = 0; k < m; ++k) {
                    p_eval += coeffs[k] * cur_x;
                    cur_x *= x_norm[i];
                }
                double diff = p_db[i] - p_eval;
                weights[i] = (diff < 1.5 * std_r && diff > -3.0 * std_r) ? 1.0 : 0.0;
            }
        }
    }

    std::vector<double> baseline_db(n);
    std::vector<double> clean_res;
    for (int i = 0; i < n; ++i) {
        double p_eval = 0.0;
        double cur_x = 1.0;
        for (int k = 0; k < m; ++k) {
            p_eval += coeffs[k] * cur_x;
            cur_x *= x_norm[i];
        }
        baseline_db[i] = p_eval;
        if (passband_mask[i] && !hi_window[i]) {
            clean_res.push_back(p_db[i] - p_eval);
        }
    }

    double offset = median_of(clean_res);
    for (int i = 0; i < n; ++i) baseline_db[i] += offset;
    return baseline_db;
}

// Ripple filter: Interpolation across HI window and Gaussian smoothing
std::vector<double> BaselineFitter::fit_filter(
    const std::vector<double>& p_db,
    const std::vector<double>& freqs_mhz,
    const std::vector<bool>& passband_mask,
    const std::vector<bool>& hi_window,
    const AstroConfig& config
) {
    int n = static_cast<int>(p_db.size());
    std::vector<double> interpolated(n);

    // Collect valid sample points outside HI window
    std::vector<double> x_valid, y_valid;
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            x_valid.push_back(static_cast<double>(i));
            y_valid.push_back(p_db[i]);
        }
    }

    if (x_valid.size() < 2) return p_db;

    // Linear interpolation across entire grid
    size_t k = 0;
    for (int i = 0; i < n; ++i) {
        double x = static_cast<double>(i);
        if (x <= x_valid.front()) {
            interpolated[i] = y_valid.front();
        } else if (x >= x_valid.back()) {
            interpolated[i] = y_valid.back();
        } else {
            while (k + 1 < x_valid.size() && x_valid[k + 1] < x) ++k;
            double x0 = x_valid[k], x1 = x_valid[k + 1];
            double y0 = y_valid[k], y1 = y_valid[k + 1];
            double alpha = (x - x0) / (x1 - x0);
            interpolated[i] = (1.0 - alpha) * y0 + alpha * y1;
        }
    }

    double bin_res_hz = config.sample_rate_hz / static_cast<double>(n);
    double sigma_bins = std::max(3.0, (config.filter_smoothing_sigma_khz * 1e3) / bin_res_hz);
    std::vector<double> smoothed = gaussian_smooth_1d(interpolated, sigma_bins);

    std::vector<double> clean_res;
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            clean_res.push_back(p_db[i] - smoothed[i]);
        }
    }
    double offset = median_of(clean_res);
    for (int i = 0; i < n; ++i) smoothed[i] += offset;
    return smoothed;
}

BaselineResults BaselineFitter::fit(
    const std::vector<double>& power,
    const std::vector<double>& freqs_mhz,
    const AstroConfig& config
) {
    int nfft = static_cast<int>(power.size());
    BaselineResults res;
    res.baseline_power.resize(nfft);
    res.baseline_db.resize(nfft);

    if (nfft == 0) return res;

    // 1. Passband mask (inner ~87%)
    int edge_bins = static_cast<int>(nfft * config.passband_edge_fraction);
    std::vector<bool> passband_mask(nfft, true);
    for (int i = 0; i < edge_bins; ++i) passband_mask[i] = false;
    for (int i = nfft - edge_bins; i < nfft; ++i) passband_mask[i] = false;

    // 2. HI protection window
    double hi_half_mhz = config.hi_protection_half_width_khz / 1e3;
    std::vector<bool> hi_window(nfft, false);
    for (int i = 0; i < nfft; ++i) {
        if (freqs_mhz[i] >= (HYDROGEN_REST_FREQ_MHZ - hi_half_mhz) &&
            freqs_mhz[i] <= (HYDROGEN_REST_FREQ_MHZ + hi_half_mhz)) {
            hi_window[i] = true;
        }
    }

    // Convert raw power to dB
    std::vector<double> p_db(nfft);
    for (int i = 0; i < nfft; ++i) {
        p_db[i] = 10.0 * std::log10(std::max(power[i], 1e-12));
    }

    if (config.baseline_method == BaselineMethod::Poly) {
        res.baseline_db = fit_poly(p_db, freqs_mhz, passband_mask, hi_window, config);
    } else if (config.baseline_method == BaselineMethod::ALS) {
        res.baseline_db = fit_als(p_db, freqs_mhz, passband_mask, hi_window, config);
    } else {
        res.baseline_db = fit_filter(p_db, freqs_mhz, passband_mask, hi_window, config);
    }

    // Convert baseline dB back to linear power
    for (int i = 0; i < nfft; ++i) {
        res.baseline_power[i] = std::max(1e-12, std::pow(10.0, res.baseline_db[i] / 10.0));
    }

    return res;
}

void BaselineFitter::refine_local(
    const std::vector<double>& vel_kms,
    std::vector<double>& calibrated_k,
    std::vector<double>& calibrated_db,
    const AstroConfig& config,
    double& out_slope,
    double& out_intercept
) {
    size_t n = vel_kms.size();
    out_slope = 0.0;
    out_intercept = 0.0;
    if (n != calibrated_k.size() || n != calibrated_db.size()) return;

    double v_thresh = config.line_free_velocity_kms;
    std::vector<double> x_free, y_free;
    for (size_t i = 0; i < n; ++i) {
        double v = vel_kms[i];
        if (v >= -200.0 && v <= 200.0 && std::abs(v) >= v_thresh) {
            x_free.push_back(v);
            y_free.push_back(calibrated_db[i]);
        }
    }

    if (x_free.size() >= 10) {
        // Fit linear regression: y = slope * x + intercept
        double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
        size_t m = x_free.size();
        for (size_t i = 0; i < m; ++i) {
            sum_x += x_free[i];
            sum_y += y_free[i];
            sum_xx += x_free[i] * x_free[i];
            sum_xy += x_free[i] * y_free[i];
        }
        double denom = (m * sum_xx - sum_x * sum_x);
        if (std::abs(denom) > 1e-12) {
            out_slope = (m * sum_xy - sum_x * sum_y) / denom;
            out_intercept = (sum_y - out_slope * sum_x) / m;
        }

        for (size_t i = 0; i < n; ++i) {
            double trend_db = out_slope * vel_kms[i] + out_intercept;
            calibrated_db[i] -= trend_db;
            calibrated_k[i] = config.system_temp_k * (std::pow(10.0, calibrated_db[i] / 10.0) - 1.0);
        }
    }
}

} // namespace astro
