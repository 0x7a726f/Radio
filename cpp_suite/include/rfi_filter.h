#pragma once

#include <vector>
#include "config.h"

namespace astro {

class RFIFilter {
public:
    // Scans for narrow-band RFI comb spikes and interpolates them out, protecting the HI line window
    static std::vector<double> mitigate(
        const std::vector<double>& power,
        const std::vector<double>& freqs_mhz,
        const AstroConfig& config,
        int& out_num_rfi
    );
};

} // namespace astro
