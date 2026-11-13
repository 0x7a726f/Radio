#pragma once

#include "constants.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <deque>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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
    const std::string& get_output_dir() const { return output_dir_; }

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
    std::atomic<bool> sidecars_written_{false};

#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
#endif

    // Background disk writing queue and reusable block buffer pool
    std::mutex lifecycle_mutex_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::vector<uint8_t>> write_queue_;
    std::vector<std::vector<uint8_t>> free_pool_;
    std::thread writer_thread_;
};
