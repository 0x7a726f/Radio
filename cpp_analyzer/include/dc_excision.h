#pragma once

#include <vector>
#include <cstdint>
#include "config.h"

namespace astro {

struct DCExcisionInfo {
    int dc_bin = 0;
    double dc_center_freq_mhz = 0.0;
    int cleared_bins = 0;
    double cleared_bandwidth_khz = 0.0;
};

class DCExcision {
public:
    static std::vector<double> excise(const std::vector<double>& power, const AstroConfig& config, DCExcisionInfo& out_info);
};

} // namespace astro
