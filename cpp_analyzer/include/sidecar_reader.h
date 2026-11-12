#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "config.h"

namespace astro {

struct ObservationMetadata {
    bool found_sidecar = false;
    std::string sidecar_source;
    double center_freq_hz = 0.0;
    double sample_rate_hz = 0.0;
    double gain_db = 0.0;
    bool has_gain = false;
    bool bias_tee = false;
    double lo_offset_khz = 0.0;
    std::string device_name;
    std::string timestamp;
};

class SidecarReader {
public:
    // Read and parse sidecar (.json or .txt) matching cs16_path
    static ObservationMetadata parse(const std::string& cs16_path);

    // Apply discovered metadata into AstroConfig if found
    static bool apply_to_config(const ObservationMetadata& meta, AstroConfig& config);

    // Scan directories for available .cs16 files, sorted by newest first
    static std::vector<std::string> scan_cs16_files(const std::vector<std::string>& search_paths);

private:
    static bool parse_json_sidecar(const std::string& json_path, ObservationMetadata& meta);
    static bool parse_txt_sidecar(const std::string& txt_path, ObservationMetadata& meta);
    static bool parse_filename_fallback(const std::string& cs16_path, ObservationMetadata& meta);
};

} // namespace astro
