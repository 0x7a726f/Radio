#include "sidecar_reader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <algorithm>

namespace astro {

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n\"'");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(start, end - start + 1);
}

// Simple robust JSON extractor for key-values
static bool json_extract_number(const std::string& content, const std::string& key, double& out_val) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(content, match, rgx)) {
        try {
            out_val = std::stod(match[1].str());
            return true;
        } catch (...) {}
    }
    return false;
}

static bool json_extract_string(const std::string& content, const std::string& key, std::string& out_str) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(content, match, rgx)) {
        out_str = match[1].str();
        return true;
    }
    return false;
}

static bool json_extract_bool(const std::string& content, const std::string& key, bool& out_bool) {
    std::regex rgx("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(content, match, rgx)) {
        out_bool = (match[1].str() == "true");
        return true;
    }
    return false;
}

bool SidecarReader::parse_json_sidecar(const std::string& json_path, ObservationMetadata& meta) {
    std::ifstream file(json_path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    bool has_data = false;
    double val = 0.0;
    if (json_extract_number(content, "center_freq_hz", val)) {
        meta.center_freq_hz = val;
        has_data = true;
    }
    if (json_extract_number(content, "sample_rate_hz", val)) {
        meta.sample_rate_hz = val;
        has_data = true;
    }
    if (json_extract_number(content, "gain_db", val)) {
        meta.gain_db = val;
        meta.has_gain = true;
    }
    if (json_extract_number(content, "lo_offset_khz", val)) {
        meta.lo_offset_khz = val;
    }
    bool bval = false;
    if (json_extract_bool(content, "bias_tee", bval)) {
        meta.bias_tee = bval;
    }
    std::string sval;
    if (json_extract_string(content, "device", sval) || json_extract_string(content, "device_name", sval)) {
        meta.device_name = sval;
    }
    if (json_extract_string(content, "timestamp", sval)) {
        meta.timestamp = sval;
    }

    if (has_data) {
        meta.found_sidecar = true;
        meta.sidecar_source = std::filesystem::path(json_path).filename().string();
        return true;
    }
    return false;
}

bool SidecarReader::parse_txt_sidecar(const std::string& txt_path, ObservationMetadata& meta) {
    std::ifstream file(txt_path);
    if (!file.is_open()) return false;

    std::string line;
    bool has_data = false;

    while (std::getline(file, line)) {
        if (line.find("Tuned SDR Center LO:") != std::string::npos || line.find("Center Frequency (LO):") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val_str = trim(line.substr(colon + 1));
                meta.center_freq_hz = parse_frequency_or_rate(val_str, true);
                has_data = true;
            }
        } else if (line.find("Sampling Rate (Fs):") != std::string::npos || line.find("SDR Sample Rate:") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val_str = trim(line.substr(colon + 1));
                meta.sample_rate_hz = parse_frequency_or_rate(val_str, true);
                has_data = true;
            }
        } else if (line.find("RF Gain:") != std::string::npos || line.find("Gain:") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val_str = trim(line.substr(colon + 1));
                try {
                    meta.gain_db = std::stod(val_str);
                    meta.has_gain = true;
                } catch (...) {}
            }
        } else if (line.find("Bias-Tee") != std::string::npos || line.find("Bias Tee") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val_str = trim(line.substr(colon + 1));
                std::transform(val_str.begin(), val_str.end(), val_str.begin(), [](unsigned char c) { return std::tolower(c); });
                meta.bias_tee = (val_str.find("on") != std::string::npos || val_str.find("enabled") != std::string::npos || val_str.find("true") != std::string::npos);
            }
        } else if (line.find("LO Offset") != std::string::npos || line.find("Offset Shift:") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val_str = trim(line.substr(colon + 1));
                val_str.erase(std::remove(val_str.begin(), val_str.end(), ','), val_str.end());
                try {
                    meta.lo_offset_khz = std::stod(val_str);
                } catch (...) {}
            }
        } else if (line.find("Device:") != std::string::npos || line.find("Frontend Device:") != std::string::npos || line.find("Device Name:") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                meta.device_name = trim(line.substr(colon + 1));
            }
        }
    }

    if (has_data) {
        meta.found_sidecar = true;
        meta.sidecar_source = std::filesystem::path(txt_path).filename().string();
        return true;
    }
    return false;
}

bool SidecarReader::parse_filename_fallback(const std::string& cs16_path, ObservationMetadata& meta) {
    std::string fname = std::filesystem::path(cs16_path).filename().string();
    // Pattern example: 2026-11-11_17-47-26_2400000SPS_1420410000Hz.cs16
    std::regex rate_regex("([0-9]+)SPS", std::regex::icase);
    std::regex freq_regex("([0-9]+)Hz", std::regex::icase);

    std::smatch match;
    bool found = false;
    if (std::regex_search(fname, match, rate_regex)) {
        try {
            meta.sample_rate_hz = std::stod(match[1].str());
            found = true;
        } catch (...) {}
    }
    if (std::regex_search(fname, match, freq_regex)) {
        try {
            meta.center_freq_hz = std::stod(match[1].str());
            found = true;
        } catch (...) {}
    }
    if (found) {
        meta.found_sidecar = true;
        meta.sidecar_source = "filename inference";
        return true;
    }
    return false;
}

ObservationMetadata SidecarReader::parse(const std::string& cs16_path) {
    ObservationMetadata meta;
    std::filesystem::path p(cs16_path);
    std::filesystem::path base_path = p.parent_path() / p.stem();

    // 1. Try .json sidecar
    std::filesystem::path json_path = base_path;
    json_path += ".json";
    if (std::filesystem::exists(json_path) && parse_json_sidecar(json_path.string(), meta)) {
        return meta;
    }

    // 2. Try .txt sidecar
    std::filesystem::path txt_path = base_path;
    txt_path += ".txt";
    if (std::filesystem::exists(txt_path) && parse_txt_sidecar(txt_path.string(), meta)) {
        return meta;
    }

    // 3. Fallback: Parse from filename tokens
    parse_filename_fallback(cs16_path, meta);
    return meta;
}

bool SidecarReader::apply_to_config(const ObservationMetadata& meta, AstroConfig& config) {
    if (!meta.found_sidecar) return false;
    if (meta.center_freq_hz > 1e5) {
        config.center_freq_hz = meta.center_freq_hz;
    }
    if (meta.sample_rate_hz > 1e4) {
        config.sample_rate_hz = meta.sample_rate_hz;
    }
    return true;
}

std::vector<std::string> SidecarReader::scan_cs16_files(const std::vector<std::string>& search_paths) {
    std::vector<std::string> results;
    for (const auto& path_str : search_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(path_str, ec)) continue;

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path_str, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".cs16") {
                    results.push_back(std::filesystem::absolute(entry.path()).string());
                }
            }
        } catch (...) {}
    }

    // Sort by last modified time (newest first)
    std::sort(results.begin(), results.end(), [](const std::string& a, const std::string& b) {
        std::error_code ec1, ec2;
        auto time_a = std::filesystem::last_write_time(a, ec1);
        auto time_b = std::filesystem::last_write_time(b, ec2);
        return time_a > time_b;
    });

    // Deduplicate
    results.erase(std::unique(results.begin(), results.end()), results.end());
    return results;
}

} // namespace astro
