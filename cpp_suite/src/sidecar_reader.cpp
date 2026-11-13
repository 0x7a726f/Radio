#include "sidecar_reader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <cstdlib>

namespace astro {

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n\"'");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(start, end - start + 1);
}

// Fast JSON key-value scanner avoiding repeated std::regex compilations
static size_t find_json_value_start(const std::string& content, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = content.find(needle, pos)) != std::string::npos) {
        size_t p = pos + needle.size();
        while (p < content.size() && std::isspace(static_cast<unsigned char>(content[p]))) ++p;
        if (p < content.size() && content[p] == ':') {
            ++p;
            while (p < content.size() && std::isspace(static_cast<unsigned char>(content[p]))) ++p;
            return p;
        }
        pos = p;
    }
    return std::string::npos;
}

static bool json_extract_number(const std::string& content, const std::string& key, double& out_val) {
    size_t p = find_json_value_start(content, key);
    if (p == std::string::npos || p >= content.size()) return false;
    const char* start = content.c_str() + p;
    char* end = nullptr;
    double val = std::strtod(start, &end);
    if (end != start) {
        out_val = val;
        return true;
    }
    return false;
}

static bool json_extract_string(const std::string& content, const std::string& key, std::string& out_str) {
    size_t p = find_json_value_start(content, key);
    if (p == std::string::npos || p >= content.size() || content[p] != '"') return false;
    size_t close = content.find('"', p + 1);
    if (close == std::string::npos) return false;
    out_str = content.substr(p + 1, close - (p + 1));
    return true;
}

static bool json_extract_bool(const std::string& content, const std::string& key, bool& out_bool) {
    size_t p = find_json_value_start(content, key);
    if (p == std::string::npos || p >= content.size()) return false;
    if (content.compare(p, 4, "true") == 0) {
        out_bool = true;
        return true;
    }
    if (content.compare(p, 5, "false") == 0) {
        out_bool = false;
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
    static const std::regex rate_regex("([0-9]+)SPS", std::regex::icase);
    static const std::regex freq_regex("([0-9]+)Hz", std::regex::icase);

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
    struct FoundEntry {
        std::string path;
        std::filesystem::file_time_type mtime;
    };
    std::vector<FoundEntry> entries;
    std::unordered_set<std::string> seen_canonical;

    for (const auto& path_str : search_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(path_str, ec)) continue;

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path_str, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".cs16") {
                    std::error_code canon_ec;
                    auto canon = std::filesystem::weakly_canonical(entry.path(), canon_ec);
                    std::string canon_key = (canon_ec ? std::filesystem::absolute(entry.path()) : canon).generic_string();
#ifdef _WIN32
                    std::transform(canon_key.begin(), canon_key.end(), canon_key.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                    if (seen_canonical.insert(std::move(canon_key)).second) {
                        std::error_code time_ec;
                        auto mtime = entry.last_write_time(time_ec);
                        std::string abs_path = std::filesystem::absolute(entry.path(), canon_ec).string();
                        entries.push_back({std::move(abs_path), mtime});
                    }
                }
            }
        } catch (...) {}
    }

    // Sort by cached last modified time (newest first)
    std::sort(entries.begin(), entries.end(), [](const FoundEntry& a, const FoundEntry& b) {
        return a.mtime > b.mtime;
    });

    std::vector<std::string> results;
    results.reserve(entries.size());
    for (auto& e : entries) {
        results.push_back(std::move(e.path));
    }
    return results;
}

} // namespace astro
