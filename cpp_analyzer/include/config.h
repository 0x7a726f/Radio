#pragma once

#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include "constants.h"

namespace astro {

enum class WindowType {
    Blackman,
    Hann,
    BlackmanHarris
};

enum class BaselineMethod {
    ALS,          // Asymmetric Least Squares / Whittaker smoothing
    Poly,         // Iterative sigma-clipped high-order polynomial
    RippleFilter  // Ripple-tuned smoothed background
};

struct AstroConfig {
    // SDR & Hardware Settings
    double center_freq_hz = 1420.5e6;      // SDR LO Tuned Frequency (e.g. 1420.5 MHz)
    double sample_rate_hz = 2.4e6;         // SDR Sampling Rate (e.g. 2.4 MSPS)
    double system_temp_k = 100.0;          // Assumed system noise temperature (Kelvin)

    // DSP Settings
    int fft_size = 8192;                   // Number of FFT bins
    WindowType window = WindowType::Blackman;
    int chunk_blocks = 512;                // FFT blocks per streaming batch
    double waterfall_dt_s = 1.0;           // Integration time per waterfall row
    double max_duration_s = 0.0;           // 0.0 or negative = full observation

    // DC Excision & RFI Settings
    double dc_clear_hz = 5000.0;           // Bandwidth around 0 Hz IF to clear (+/- 2.5 kHz)
    double rfi_threshold_mad = 4.5;        // Outlier detection threshold (MAD units)

    // Baseline Estimation Settings
    BaselineMethod baseline_method = BaselineMethod::RippleFilter;
    double passband_edge_fraction = 0.065; // Outer skirts fraction (6.5%)
    double hi_protection_half_width_khz = 90.0; // Protection zone (+/- 90 kHz)
    double filter_smoothing_sigma_khz = 35.0;   // Gaussian filter sigma in kHz
    double als_lambda = 2000.0;            // ALS smoothness parameter
    double als_p = 0.02;                   // ALS asymmetry parameter
    int poly_degree = 10;                  // Polynomial degree
    int local_baseline_order = 1;          // Local baseline order (1 = linear)
    double line_free_velocity_kms = 40.0;  // Radial velocity threshold (|v| > 40 km/s)

    // Output & Logging
    std::string output_dir = "output";
    bool verbose = true;
};

// String to WindowType helper
inline WindowType parse_window_type(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "hann" || s == "hanning") return WindowType::Hann;
    if (s == "blackmanharris" || s == "blackman_harris" || s == "bh") return WindowType::BlackmanHarris;
    return WindowType::Blackman;
}

inline std::string window_type_to_string(WindowType w) {
    switch (w) {
        case WindowType::Hann: return "Hann";
        case WindowType::BlackmanHarris: return "Blackman-Harris";
        default: return "Blackman";
    }
}

// String to BaselineMethod helper
inline BaselineMethod parse_baseline_method(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "als" || s == "whittaker") return BaselineMethod::ALS;
    if (s == "poly" || s == "polynomial") return BaselineMethod::Poly;
    return BaselineMethod::RippleFilter;
}

inline std::string baseline_method_to_string(BaselineMethod m) {
    switch (m) {
        case BaselineMethod::ALS: return "als";
        case BaselineMethod::Poly: return "poly";
        default: return "ripple_filter";
    }
}

inline bool str_ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

// Flexible frequency or rate parser: supports "1420.5", "1420.5MHz", "1,420.5MHz", "2400000", "2.4MSPS"
inline double parse_frequency_or_rate(const std::string& val_str, bool default_is_mhz = true) {
    std::string cleaned = val_str;
    cleaned.erase(cleaned.begin(), std::find_if(cleaned.begin(), cleaned.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    cleaned.erase(std::find_if(cleaned.rbegin(), cleaned.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), cleaned.end());

    // Remove any thousands separator commas
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ','), cleaned.end());

    std::string lower = cleaned;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    double mult = 1.0;
    if (str_ends_with(lower, "mhz") || str_ends_with(lower, "msps")) {
        mult = 1e6;
        cleaned = cleaned.substr(0, cleaned.size() - (str_ends_with(lower, "mhz") ? 3 : 4));
    } else if (str_ends_with(lower, "khz") || str_ends_with(lower, "ksps")) {
        mult = 1e3;
        cleaned = cleaned.substr(0, cleaned.size() - (str_ends_with(lower, "khz") ? 3 : 4));
    } else if (str_ends_with(lower, "hz") || str_ends_with(lower, "sps")) {
        mult = 1.0;
        cleaned = cleaned.substr(0, cleaned.size() - (str_ends_with(lower, "hz") ? 2 : 3));
    } else if (str_ends_with(lower, "m")) {
        mult = 1e6;
        cleaned = cleaned.substr(0, cleaned.size() - 1);
    } else if (str_ends_with(lower, "k")) {
        mult = 1e3;
        cleaned = cleaned.substr(0, cleaned.size() - 1);
    }

    try {
        double val = std::stod(cleaned);
        if (mult != 1.0) {
            return val * mult;
        }
        if (default_is_mhz && val < 10000.0) {
            return val * 1e6;
        }
        return val;
    } catch (...) {
        return default_is_mhz ? 1420.5e6 : 2.4e6;
    }
}

} // namespace astro
