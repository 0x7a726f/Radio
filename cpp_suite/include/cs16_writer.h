#pragma once

#include "constants.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>

struct RecordingStats {
    std::string status;       // "IDLE" or "RECORDING"
    std::string filename;
    std::string full_path;
    std::string size_str;
    std::string free_space_str;
    std::string time_left_str;
    std::string elapsed_str;
    uint64_t bytes_written = 0;
    uint64_t samples_written = 0;
    uint64_t dropped_blocks = 0;
    double free_space_gb = 0.0;
    double elapsed_seconds = 0.0;
};

class Cs16Writer {
public:
    explicit Cs16Writer(const std::string& output_dir = "recordings");
    ~Cs16Writer();

    std::string start_recording(
        uint32_t sample_rate_hz,
        uint64_t center_freq_hz,
        const std::string& device_name = "RTL-SDR",
        float gain_db = 25.4f,
        bool bias_tee = false,
        const std::string& custom_filepath = "",
        double max_duration_s = 0.0
    );

    bool stop_recording();
    bool is_recording() const { return is_recording_.load(); }

    void feed_samples(const uint8_t* data, size_t length);

    RecordingStats get_stats();
    std::string get_current_filepath() const;
    std::string get_last_recorded_filepath() const;

private:
    void writer_worker();
    void write_accompanied_txt(const std::string& txt_path, double duration_s);
    void write_sidecar_json(const std::string& json_path, double duration_s);

    std::string output_dir_;
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> stop_worker_{false};
    mutable std::mutex path_mutex_;
    std::string current_filepath_;
    std::string last_recorded_filepath_;
    std::string iso_start_timestamp_;

    uint32_t sample_rate_hz_ = 2400000;
    uint64_t center_freq_hz_ = 1420500000;
    std::string device_name_ = "RTL-SDR";
    float gain_db_ = 25.4f;
    bool bias_tee_ = false;
    double max_duration_s_ = 0.0;

    std::chrono::high_resolution_clock::time_point start_time_;
    std::atomic<uint64_t> bytes_written_{0};
    std::atomic<uint64_t> samples_written_{0};
    std::atomic<uint64_t> dropped_blocks_{0};

    // Vectorized conversion lookup table: uint8 -> int16
    int16_t lut_[256];

    // Background disk writing queue
    std::mutex queue_mutex_;
    std::mutex stop_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::vector<uint8_t>> write_queue_;
    std::thread writer_thread_;
};
