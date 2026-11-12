#pragma once

#include <vector>
#include "config.h"

namespace astro {

struct BaselineResults {
    std::vector<double> baseline_power;          // Linear power baseline matching raw input
    std::vector<double> baseline_db;             // In dB scale
};

class BaselineFitter {
public:
    // Stage 1: Fit instrumental baseline across the SDR passband
    static BaselineResults fit(
        const std::vector<double>& power,
        const std::vector<double>& freqs_mhz,
        const AstroConfig& config
    );

    // Stage 2: Refine local baseline in zoomed HI line velocity space (-200 to +200 km/s)
    static void refine_local(
        const std::vector<double>& vel_kms,
        std::vector<double>& calibrated_k,
        std::vector<double>& calibrated_db,
        const AstroConfig& config,
        double& out_slope,
        double& out_intercept
    );

private:
    static std::vector<double> fit_als(
        const std::vector<double>& p_db,
        const std::vector<double>& freqs_mhz,
        const std::vector<bool>& passband_mask,
        const std::vector<bool>& hi_window,
        const AstroConfig& config
    );

    static std::vector<double> fit_poly(
        const std::vector<double>& p_db,
        const std::vector<double>& freqs_mhz,
        const std::vector<bool>& passband_mask,
        const std::vector<bool>& hi_window,
        const AstroConfig& config
    );

    static std::vector<double> fit_filter(
        const std::vector<double>& p_db,
        const std::vector<double>& freqs_mhz,
        const std::vector<bool>& passband_mask,
        const std::vector<bool>& hi_window,
        const AstroConfig& config
    );
};

// Fast Pentadiagonal Linear System Solver for Whittaker / ALS:
// Solves A * x = rhs, where A is symmetric positive-definite pentadiagonal.
// Zero dynamic heap allocations in hot path, O(N) runtime.
void solve_pentadiagonal_symmetric(
    int n,
    const double* d,    // main diagonal [0..n-1]
    const double* e,    // sub/super diagonal 1 [0..n-2]
    const double* f,    // sub/super diagonal 2 [0..n-3]
    const double* rhs,  // right-hand side [0..n-1]
    double* x           // solution output [0..n-1]
);

} // namespace astro
