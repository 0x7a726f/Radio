#include "baseline_fitter.h"
#include "simd_dsp.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <cstring>

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

static double median_of_inplace(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    size_t n = v.size();
    size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    if (n % 2 != 0) {
        return v[mid];
    } else {
        auto left_max = std::max_element(v.begin(), v.begin() + mid);
        return (*left_max + v[mid]) * 0.5;
    }
}

// Symmetric branchless AVX2+FMA 1D Gaussian smoothing with edge replication
static std::vector<double> gaussian_smooth_1d(const std::vector<double>& input, double sigma) {
    if (sigma <= 0.0 || input.size() < 3) return input;
    const int n = static_cast<int>(input.size());
    std::vector<double> output(n);

    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    thread_local std::vector<double> tl_half_kernel;
    thread_local std::vector<double> tl_padded;

    tl_half_kernel.resize(static_cast<size_t>(radius) + 1);
    const double inv_2sig2 = -0.5 / (sigma * sigma);
    double sum = 1.0; // k = 0 weight is 1.0
    tl_half_kernel[0] = 1.0;
    for (int k = 1; k <= radius; ++k) {
        double g = std::exp(inv_2sig2 * static_cast<double>(k) * static_cast<double>(k));
        tl_half_kernel[k] = g;
        sum += 2.0 * g;
    }
    const double inv_sum = 1.0 / sum;
    for (int k = 0; k <= radius; ++k) tl_half_kernel[k] *= inv_sum;

    const size_t pad_len = static_cast<size_t>(n) + 2ULL * static_cast<size_t>(radius);
    if (tl_padded.size() < pad_len) tl_padded.resize(pad_len);
    double* p = tl_padded.data();
    const double first_v = input.front();
    const double last_v = input.back();
    for (int k = 0; k < radius; ++k) p[k] = first_v;
    std::memcpy(p + radius, input.data(), static_cast<size_t>(n) * sizeof(double));
    for (int k = 0; k < radius; ++k) p[radius + n + k] = last_v;

    const double* hk = tl_half_kernel.data();
    const double* center_base = p + radius;
    double* out = output.data();
    int i = 0;

#if defined(__AVX2__)
    const __m256d k0 = _mm256_set1_pd(hk[0]);
    for (; i + 8 <= n; i += 8) {
        const double* c = center_base + i;
        __m256d acc0 = _mm256_mul_pd(k0, _mm256_loadu_pd(c));
        __m256d acc1 = _mm256_mul_pd(k0, _mm256_loadu_pd(c + 4));
        for (int k = 1; k <= radius; ++k) {
            const __m256d kv = _mm256_set1_pd(hk[k]);
            const __m256d s0 = _mm256_add_pd(_mm256_loadu_pd(c - k), _mm256_loadu_pd(c + k));
            const __m256d s1 = _mm256_add_pd(_mm256_loadu_pd(c + 4 - k), _mm256_loadu_pd(c + 4 + k));
#if SIMD_HAS_FMA
            acc0 = _mm256_fmadd_pd(kv, s0, acc0);
            acc1 = _mm256_fmadd_pd(kv, s1, acc1);
#else
            acc0 = _mm256_add_pd(acc0, _mm256_mul_pd(kv, s0));
            acc1 = _mm256_add_pd(acc1, _mm256_mul_pd(kv, s1));
#endif
        }
        _mm256_storeu_pd(out + i, acc0);
        _mm256_storeu_pd(out + i + 4, acc1);
    }
    for (; i + 4 <= n; i += 4) {
        const double* c = center_base + i;
        __m256d acc = _mm256_mul_pd(k0, _mm256_loadu_pd(c));
        for (int k = 1; k <= radius; ++k) {
            const __m256d kv = _mm256_set1_pd(hk[k]);
            const __m256d s = _mm256_add_pd(_mm256_loadu_pd(c - k), _mm256_loadu_pd(c + k));
#if SIMD_HAS_FMA
            acc = _mm256_fmadd_pd(kv, s, acc);
#else
            acc = _mm256_add_pd(acc, _mm256_mul_pd(kv, s));
#endif
        }
        _mm256_storeu_pd(out + i, acc);
    }
#endif

    for (; i < n; ++i) {
        const double* c = center_base + i;
        double acc = hk[0] * c[0];
        for (int k = 1; k <= radius; ++k) {
            acc += hk[k] * (c[-k] + c[k]);
        }
        out[i] = acc;
    }
    return output;
}

std::vector<double> BaselineFitter::fit_als(
    const std::vector<double>& p_db,
    const std::vector<double>& freqs_mhz,
    const std::vector<uint8_t>& passband_mask,
    const std::vector<uint8_t>& hi_window,
    const AstroConfig& config
) {
    (void)freqs_mhz;
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

    double noise_offset = median_of_inplace(clean_residuals);
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
    const std::vector<uint8_t>& passband_mask,
    const std::vector<uint8_t>& hi_window,
    const AstroConfig& config
) {
    int n = static_cast<int>(p_db.size());
    if (n < 4) return p_db;

    int deg = std::clamp(config.poly_degree, 2, 14);
    int m = deg + 1; // number of coefficients (<= 15)

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
    const double inv_std_f = 1.0 / std_f;

    std::vector<double> x_norm(n);
    for (int i = 0; i < n; ++i) {
        x_norm[i] = (freqs_mhz[i] - mean_f) * inv_std_f;
    }

    std::vector<double> weights(n, 0.0);
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            weights[i] = 1.0;
        }
    }

    double coeffs[16] = {0.0};
    std::vector<double> scratch_res = p_db;
    coeffs[0] = median_of_inplace(scratch_res); // Safe initial baseline level
    scratch_res.reserve(n);

    for (int iter = 0; iter < 6; ++iter) {
        double XTWX_orig[16 * 16] = {0.0};
        double XTWy[16] = {0.0};

        for (int i = 0; i < n; ++i) {
            double wi = weights[i];
            if (wi <= 0.0) continue;
            double yi = p_db[i];
            double xi = x_norm[i];

            double xp[16];
            xp[0] = 1.0;
            for (int p = 1; p < m; ++p) xp[p] = xp[p - 1] * xi;

            double wi_yi = wi * yi;
            for (int r = 0; r < m; ++r) {
                double wxr = wi * xp[r];
                XTWy[r] += xp[r] * wi_yi;
                for (int c = r; c < m; ++c) {
                    XTWX_orig[r * m + c] += wxr * xp[c];
                }
            }
        }
        for (int r = 1; r < m; ++r) {
            for (int c = 0; c < r; ++c) {
                XTWX_orig[r * m + c] = XTWX_orig[c * m + r];
            }
        }

        // Adaptive Cholesky solve with ridge damping
        bool cholesky_ok = false;
        double lambda = 1e-6;
        double A[16 * 16];
        double L[16 * 16];

        for (int attempt = 0; attempt < 5 && !cholesky_ok; ++attempt) {
            std::memcpy(A, XTWX_orig, static_cast<size_t>(m * m) * sizeof(double));
            for (int r = 0; r < m; ++r) {
                A[r * m + r] += lambda * (1.0 + XTWX_orig[r * m + r]);
            }

            std::memset(L, 0, static_cast<size_t>(m * m) * sizeof(double));
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
            double y_sol[16];
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

        // Sigma clipping outlier rejection (single pass for residual collection)
        scratch_res.clear();
        for (int i = 0; i < n; ++i) {
            if (passband_mask[i] && !hi_window[i]) {
                double p_eval = coeffs[m - 1];
                double xi = x_norm[i];
                for (int k = m - 2; k >= 0; --k) p_eval = p_eval * xi + coeffs[k];
                scratch_res.push_back(p_db[i] - p_eval);
            }
        }
        // Preserve unsorted residuals aligned with active mask order before in-place median
        double sum_res_sq = 0.0;
        std::vector<double> diffs_unsorted = scratch_res;
        double med_r = median_of_inplace(scratch_res);
        for (double r : diffs_unsorted) sum_res_sq += (r - med_r) * (r - med_r);
        double std_r = (!diffs_unsorted.empty()) ? std::sqrt(sum_res_sq / diffs_unsorted.size()) : 0.05;
        if (std_r < 1e-4) std_r = 0.05;

        size_t d_idx = 0;
        const double hi_thresh = 1.5 * std_r;
        const double lo_thresh = -3.0 * std_r;
        for (int i = 0; i < n; ++i) {
            if (passband_mask[i] && !hi_window[i]) {
                double diff = diffs_unsorted[d_idx++];
                weights[i] = (diff < hi_thresh && diff > lo_thresh) ? 1.0 : 0.0;
            }
        }
    }

    std::vector<double> baseline_db(n);
    scratch_res.clear();
    for (int i = 0; i < n; ++i) {
        double p_eval = coeffs[m - 1];
        double xi = x_norm[i];
        for (int k = m - 2; k >= 0; --k) p_eval = p_eval * xi + coeffs[k];
        baseline_db[i] = p_eval;
        if (passband_mask[i] && !hi_window[i]) {
            scratch_res.push_back(p_db[i] - p_eval);
        }
    }

    double offset = median_of_inplace(scratch_res);
    for (int i = 0; i < n; ++i) baseline_db[i] += offset;
    return baseline_db;
}

// Ripple filter: Interpolation across HI window and Gaussian smoothing
std::vector<double> BaselineFitter::fit_filter(
    const std::vector<double>& p_db,
    const std::vector<double>& freqs_mhz,
    const std::vector<uint8_t>& passband_mask,
    const std::vector<uint8_t>& hi_window,
    const AstroConfig& config
) {
    (void)freqs_mhz;
    int n = static_cast<int>(p_db.size());
    std::vector<double> interpolated(n);

    // Collect valid sample points outside HI window
    std::vector<double> x_valid, y_valid;
    x_valid.reserve(n);
    y_valid.reserve(n);
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
    clean_res.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (passband_mask[i] && !hi_window[i]) {
            clean_res.push_back(p_db[i] - smoothed[i]);
        }
    }
    double offset = median_of_inplace(clean_res);
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
    std::vector<uint8_t> passband_mask(nfft, 1);
    for (int i = 0; i < edge_bins; ++i) passband_mask[i] = 0;
    for (int i = nfft - edge_bins; i < nfft; ++i) passband_mask[i] = 0;

    // 2. HI protection window
    double hi_half_mhz = config.hi_protection_half_width_khz / 1e3;
    std::vector<uint8_t> hi_window(nfft, 0);
    for (int i = 0; i < nfft; ++i) {
        if (freqs_mhz[i] >= (HYDROGEN_REST_FREQ_MHZ - hi_half_mhz) &&
            freqs_mhz[i] <= (HYDROGEN_REST_FREQ_MHZ + hi_half_mhz)) {
            hi_window[i] = 1;
        }
    }

    // Vectorized conversion from raw power to dB
    std::vector<double> p_db(nfft);
    SimdDsp::power_to_db_f64(power.data(), p_db.data(), nfft, 1e-12);

    if (config.baseline_method == BaselineMethod::Poly) {
        res.baseline_db = fit_poly(p_db, freqs_mhz, passband_mask, hi_window, config);
    } else if (config.baseline_method == BaselineMethod::ALS) {
        res.baseline_db = fit_als(p_db, freqs_mhz, passband_mask, hi_window, config);
    } else {
        res.baseline_db = fit_filter(p_db, freqs_mhz, passband_mask, hi_window, config);
    }

    // Vectorized conversion from baseline dB back to linear power
    SimdDsp::db_to_power_f64(res.baseline_db.data(), res.baseline_power.data(), nfft);
    for (int i = 0; i < nfft; ++i) {
        if (res.baseline_power[i] < 1e-12) res.baseline_power[i] = 1e-12;
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
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        double v = vel_kms[i];
        if (v >= -200.0 && v <= 200.0 && std::abs(v) >= v_thresh) {
            double y = calibrated_db[i];
            sum_x += v;
            sum_y += y;
            sum_xx += v * v;
            sum_xy += v * y;
            m++;
        }
    }

    if (m >= 10) {
        double denom = (static_cast<double>(m) * sum_xx - sum_x * sum_x);
        if (std::abs(denom) > 1e-12) {
            out_slope = (static_cast<double>(m) * sum_xy - sum_x * sum_y) / denom;
            out_intercept = (sum_y - out_slope * sum_x) / static_cast<double>(m);
        }

        for (size_t i = 0; i < n; ++i) {
            calibrated_db[i] -= (out_slope * vel_kms[i] + out_intercept);
        }
        // Vectorized conversion to antenna temperature Kelvin
        SimdDsp::db_to_power_f64(calibrated_db.data(), calibrated_k.data(), n);
        const double t_sys = config.system_temp_k;
        for (size_t i = 0; i < n; ++i) {
            calibrated_k[i] = t_sys * (calibrated_k[i] - 1.0);
        }
    }
}

} // namespace astro
