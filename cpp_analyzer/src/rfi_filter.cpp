#include "rfi_filter.h"
#include <cmath>
#include <algorithm>

namespace astro {

static double calc_median(std::vector<double> v) {
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

std::vector<double> RFIFilter::mitigate(
    const std::vector<double>& power,
    const std::vector<double>& freqs_mhz,
    const AstroConfig& config,
    int& out_num_rfi
) {
    int nfft = static_cast<int>(power.size());
    std::vector<double> clean_power = power;
    out_num_rfi = 0;
    if (nfft < 32) return clean_power;

    double hi_half_width_mhz = config.hi_protection_half_width_khz / 1e3;
    int edge_bins = static_cast<int>(nfft * 0.05);

    std::vector<bool> valid_mask(nfft, true);
    for (int i = 0; i < edge_bins; ++i) valid_mask[i] = false;
    for (int i = nfft - edge_bins; i < nfft; ++i) valid_mask[i] = false;

    for (int i = 0; i < nfft; ++i) {
        if (freqs_mhz[i] >= (HYDROGEN_REST_FREQ_MHZ - hi_half_width_mhz) &&
            freqs_mhz[i] <= (HYDROGEN_REST_FREQ_MHZ + hi_half_width_mhz)) {
            valid_mask[i] = false; // Protected HI window
        }
    }

    // Rolling median filter (kernel size = 15)
    const int kernel_size = 15;
    const int pad_width = kernel_size / 2;
    std::vector<double> padded(nfft + 2 * pad_width);
    for (int i = 0; i < pad_width; ++i) padded[i] = clean_power[0];
    for (int i = 0; i < nfft; ++i) padded[pad_width + i] = clean_power[i];
    for (int i = 0; i < pad_width; ++i) padded[pad_width + nfft + i] = clean_power[nfft - 1];

    std::vector<double> rolling_median(nfft);
    std::vector<double> window_buf(kernel_size);
    for (int i = 0; i < nfft; ++i) {
        for (int k = 0; k < kernel_size; ++k) {
            window_buf[k] = padded[i + k];
        }
        rolling_median[i] = calc_median(window_buf);
    }

    std::vector<double> residuals(nfft);
    std::vector<double> residuals_valid;
    residuals_valid.reserve(nfft);
    for (int i = 0; i < nfft; ++i) {
        residuals[i] = clean_power[i] - rolling_median[i];
        if (valid_mask[i]) {
            residuals_valid.push_back(residuals[i]);
        }
    }

    if (residuals_valid.empty()) return clean_power;

    double med_res = calc_median(residuals_valid);
    std::vector<double> abs_diffs(residuals_valid.size());
    for (size_t i = 0; i < residuals_valid.size(); ++i) {
        abs_diffs[i] = std::abs(residuals_valid[i] - med_res);
    }
    double mad = calc_median(abs_diffs);
    double sigma_est = std::max(1e-12, 1.4826 * mad);

    double threshold = config.rfi_threshold_mad * sigma_est;
    std::vector<int> rfi_indices;
    for (int i = 0; i < nfft; ++i) {
        if (valid_mask[i] && residuals[i] > threshold) {
            rfi_indices.push_back(i);
        }
    }

    out_num_rfi = static_cast<int>(rfi_indices.size());
    for (int idx : rfi_indices) {
        int l_idx = std::max(0, idx - 4);
        int r_idx = std::min(nfft - 1, idx + 4);
        clean_power[idx] = (clean_power[l_idx] + clean_power[r_idx]) * 0.5;
    }

    return clean_power;
}

} // namespace astro
