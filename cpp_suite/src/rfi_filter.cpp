#include "rfi_filter.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace astro {

static double calc_median_inplace(std::vector<double>& v) {
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

static inline double calc_median_15(const double* src) {
    double win[15];
    std::memcpy(win, src, 15 * sizeof(double));
    std::nth_element(win, win + 7, win + 15);
    return win[7];
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

    std::vector<uint8_t> valid_mask(nfft, 1);
    for (int i = 0; i < edge_bins; ++i) valid_mask[i] = 0;
    for (int i = nfft - edge_bins; i < nfft; ++i) valid_mask[i] = 0;

    for (int i = 0; i < nfft; ++i) {
        if (freqs_mhz[i] >= (HYDROGEN_REST_FREQ_MHZ - hi_half_width_mhz) &&
            freqs_mhz[i] <= (HYDROGEN_REST_FREQ_MHZ + hi_half_width_mhz)) {
            valid_mask[i] = 0; // Protected HI window
        }
    }

    // Rolling median filter (kernel size = 15) with zero heap allocs per bin
    constexpr int kernel_size = 15;
    constexpr int pad_width = kernel_size / 2;
    std::vector<double> padded(nfft + 2 * pad_width);
    for (int i = 0; i < pad_width; ++i) padded[i] = clean_power[0];
    std::memcpy(padded.data() + pad_width, clean_power.data(), static_cast<size_t>(nfft) * sizeof(double));
    for (int i = 0; i < pad_width; ++i) padded[pad_width + nfft + i] = clean_power[nfft - 1];

    std::vector<double> residuals(nfft);
    std::vector<double> residuals_valid;
    residuals_valid.reserve(nfft);
    for (int i = 0; i < nfft; ++i) {
        double med = calc_median_15(&padded[i]);
        double res = clean_power[i] - med;
        residuals[i] = res;
        if (valid_mask[i]) {
            residuals_valid.push_back(res);
        }
    }

    if (residuals_valid.empty()) return clean_power;

    double med_res = calc_median_inplace(residuals_valid);
    for (double& r : residuals_valid) {
        r = std::abs(r - med_res);
    }
    double mad = calc_median_inplace(residuals_valid);
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
