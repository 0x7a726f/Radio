#include "dc_excision.h"
#include <cmath>
#include <algorithm>

namespace astro {

static double calculate_median_inplace(std::vector<double>& v) {
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

std::vector<double> DCExcision::excise(const std::vector<double>& power, const AstroConfig& config, DCExcisionInfo& out_info) {
    int nfft = static_cast<int>(power.size());
    std::vector<double> clean_power = power;
    if (nfft == 0) return clean_power;

    double bin_res_hz = config.sample_rate_hz / static_cast<double>(nfft);
    int dc_bin = nfft / 2;

    int half_width_bins = std::max(1, static_cast<int>(std::ceil((config.dc_clear_hz / 2.0) / bin_res_hz)));
    int guard_bins = std::max(2, half_width_bins * 2);

    int start_dc = std::max(0, dc_bin - half_width_bins);
    int end_dc = std::min(nfft - 1, dc_bin + half_width_bins);

    int left_start = std::max(0, start_dc - guard_bins);
    int right_end = std::min(nfft, end_dc + 1 + guard_bins);

    std::vector<double> left_guard(power.begin() + left_start, power.begin() + start_dc);
    std::vector<double> right_guard(power.begin() + end_dc + 1, power.begin() + right_end);

    std::vector<double> all_guards;
    all_guards.reserve(left_guard.size() + right_guard.size());
    all_guards.insert(all_guards.end(), left_guard.begin(), left_guard.end());
    all_guards.insert(all_guards.end(), right_guard.begin(), right_guard.end());

    double replacement_median = 0.0;
    if (all_guards.empty()) {
        std::vector<double> tmp = power;
        replacement_median = calculate_median_inplace(tmp);
    } else {
        replacement_median = calculate_median_inplace(all_guards);
    }
    double left_val = left_guard.empty() ? replacement_median : calculate_median_inplace(left_guard);
    double right_val = right_guard.empty() ? replacement_median : calculate_median_inplace(right_guard);

    int count = end_dc - start_dc + 1;
    if (count == 1) {
        clean_power[start_dc] = (left_val + right_val) * 0.5;
    } else {
        for (int i = 0; i < count; ++i) {
            double alpha = static_cast<double>(i) / (count - 1);
            clean_power[start_dc + i] = (1.0 - alpha) * left_val + alpha * right_val;
        }
    }

    out_info.dc_bin = dc_bin;
    out_info.dc_center_freq_mhz = config.center_freq_hz / 1e6;
    out_info.cleared_bins = count;
    out_info.cleared_bandwidth_khz = count * bin_res_hz / 1e3;

    return clean_power;
}

} // namespace astro
