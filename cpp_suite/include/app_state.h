#pragma once

#include <string>
#include <mutex>
#include <filesystem>

enum class AppSection {
    Record,
    Analyze
};

struct AppState {
    AppSection current_section = AppSection::Record;
    
    // Seamless Record -> Analyze Bridge
    std::string last_recorded_filepath;
    std::string target_analyze_filepath;
    bool request_auto_analyze = false;

    // Active Recording Isolation State
    bool is_recording = false;
    std::string active_recording_filepath;

    // Mutex for any cross-thread state changes
    std::mutex state_mutex;

    void set_last_recorded_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(state_mutex);
        last_recorded_filepath = path;
    }

    std::string get_last_recorded_file() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return last_recorded_filepath;
    }

    void set_recording_active(bool active, const std::string& path = "") {
        std::lock_guard<std::mutex> lock(state_mutex);
        is_recording = active;
        active_recording_filepath = active ? path : "";
    }

    bool is_recording_active() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return is_recording;
    }

    std::string get_active_recording_filepath() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return active_recording_filepath;
    }

    bool is_file_actively_recording(const std::string& path) {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!is_recording || active_recording_filepath.empty() || path.empty()) return false;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::exists(active_recording_filepath, ec)) {
            return std::filesystem::equivalent(path, active_recording_filepath, ec);
        }
        return (path == active_recording_filepath);
    }

    void switch_to_analyze(const std::string& filepath, bool auto_run = true) {
        std::lock_guard<std::mutex> lock(state_mutex);
        target_analyze_filepath = filepath;
        last_recorded_filepath = filepath;
        request_auto_analyze = auto_run;
        current_section = AppSection::Analyze;
    }

    void switch_to_record() {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_section = AppSection::Record;
    }

    bool check_and_clear_auto_analyze(std::string& target_out) {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!request_auto_analyze) return false;
        request_auto_analyze = false;
        target_out = target_analyze_filepath;
        return true;
    }

    void set_section(AppSection sec) {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_section = sec;
    }

    AppSection get_section() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return current_section;
    }
};
