#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <atomic>
#include "config.h"

namespace astro {

struct StreamingResults {
    std::vector<double> averaged_power;          // Size: fft_size, shifted (0 Hz IF centered)
    std::vector<float> waterfall_data;           // Row-major 2D matrix: num_rows x fft_size
    std::vector<double> waterfall_times;         // Timestamp (seconds) for each waterfall row
    int waterfall_rows = 0;
    int waterfall_cols = 0;
    std::vector<double> freq_hz;                 // Absolute frequency in Hz
    std::vector<double> freq_mhz;                // Absolute frequency in MHz
    uint64_t total_samples = 0;
    uint64_t total_blocks = 0;
    double total_duration_s = 0.0;
    double elapsed_processing_time_s = 0.0;
    double throughput_mb_s = 0.0;
    uint64_t file_size_bytes = 0;
};

// Progress callback: (percent_0_to_100, current_mb_s, eta_seconds)
using ProgressCallback = std::function<void(double percent, double speed_mb_s, double eta_s)>;

class StreamingProcessor {
public:
    StreamingProcessor(const std::string& filepath, const AstroConfig& config);
    ~StreamingProcessor();

    // Execute streaming spectral accumulation & waterfall extraction
    StreamingResults process(ProgressCallback progress_cb = nullptr);

    // Cancel processing if running
    void cancel() { cancelled_ = true; }
    void set_cancel_token(const std::atomic<bool>* token) { external_cancel_ = token; }
    bool is_cancelled() const { return cancelled_ || (external_cancel_ && external_cancel_->load()); }

    uint64_t get_file_size() const { return file_size_; }
    uint64_t get_total_samples() const { return total_samples_; }
    double get_total_duration_s() const { return total_duration_s_; }

    // Window generation helper
    static std::vector<float> generate_window(WindowType type, int size);

private:
    std::string filepath_;
    AstroConfig config_;
    uint64_t file_size_ = 0;
    uint64_t total_samples_ = 0;
    uint64_t samples_to_process_ = 0;
    uint64_t bytes_to_process_ = 0;
    double total_duration_s_ = 0.0;
    double duration_to_process_s_ = 0.0;
    bool cancelled_ = false;
    const std::atomic<bool>* external_cancel_ = nullptr;
};

} // namespace astro
