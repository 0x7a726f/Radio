#pragma once

#include <string>
#include <vector>
#include "config.h"
#include "astrophysics.h"
#include "streaming_processor.h"

namespace astro {

struct FileMetadata {
    std::string filepath;
    uint64_t filesize = 0;
    uint64_t total_samples = 0;
    double duration_s = 0.0;
    double processing_time_s = 0.0;
    double throughput_mb_s = 0.0;
};

class Exporter {
public:
    static bool export_csv(
        const std::vector<double>& freqs_hz,
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& vel_kms,
        const std::vector<double>& raw_power,
        const std::vector<double>& baseline_power,
        const std::vector<double>& calibrated_k,
        const std::vector<double>& calibrated_db,
        const std::string& output_path
    );

    static bool export_json(
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const FileMetadata& meta,
        const std::string& output_path
    );

    static bool export_summary_text(
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const FileMetadata& meta,
        const std::string& output_path
    );

    static bool export_all_artifacts(
        const std::string& output_dir,
        const std::string& source_file,
        const AstroConfig& config,
        const FileMetadata& meta,
        const StreamingResults& stream_res,
        const std::vector<double>& raw_power,
        const std::vector<double>& baseline_power,
        const std::vector<double>& calibrated_k,
        const std::vector<double>& calibrated_db,
        const AstrophysicalMetrics& metrics
    );
};

} // namespace astro
