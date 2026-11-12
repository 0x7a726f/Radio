#include "sdr_controller.h"
#include "virtual_sdr.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <iostream>

SdrController::SdrController()
    : rx_buffer_(SdrConstants::SDR_CHUNK_BYTES) {
}

SdrController::~SdrController() {
    shutdown();
}

bool SdrController::initialize(bool force_virtual, uint32_t device_index) {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    if (is_streaming_) {
        is_streaming_ = false;
        stop_requested_ = true;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    if (device_) {
        device_->close();
        device_.reset();
    }

    bool hardware_ok = false;
    if (!force_virtual) {
        auto dev = std::make_unique<RtlSdrDevice>();
        if (dev->open(device_index)) {
            device_ = std::move(dev);
            is_simulated_ = false;
            hardware_ok = true;
        }
    }

    if (!hardware_ok) {
        auto vdev = std::make_unique<VirtualSdrDevice>();
        vdev->open(device_index);
        vdev->set_frequency(current_freq_.load());
        vdev->set_sample_rate(current_rate_.load());
        vdev->set_gain(current_gain_.load(), auto_gain_.load());
        vdev->set_bias_tee(bias_tee_.load());
        vdev->set_ppm(ppm_.load());
        device_ = std::move(vdev);
        is_simulated_ = true;
    }

    current_freq_.store(device_->get_frequency());
    current_rate_.store(device_->get_sample_rate());
    current_gain_.store(device_->get_gain());
    auto_gain_.store(device_->is_auto_gain());
    bias_tee_.store(device_->get_bias_tee());
    ppm_.store(device_->get_ppm());

    return true;
}

void SdrController::shutdown() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    if (is_streaming_) {
        is_streaming_ = false;
        stop_requested_ = true;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    if (device_) {
        device_->close();
        device_.reset();
    }
}

bool SdrController::start_streaming() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    if (is_streaming_) return true;
    if (!device_) {
        auto dev = std::make_unique<RtlSdrDevice>();
        if (dev->open(0)) {
            device_ = std::move(dev);
            is_simulated_ = false;
        } else {
            auto vdev = std::make_unique<VirtualSdrDevice>();
            vdev->open(0);
            device_ = std::move(vdev);
            is_simulated_ = true;
        }
    }

    stop_requested_ = false;
    is_streaming_ = true;
    worker_thread_ = std::thread(&SdrController::stream_worker, this);
    return true;
}

void SdrController::stop_streaming() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    if (!is_streaming_) return;

    is_streaming_ = false;
    stop_requested_ = true;

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

std::future<bool> SdrController::request_frequency(uint64_t freq_hz) {
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    SdrCommand cmd;
    cmd.type = SdrCommandType::SetFrequency;
    cmd.uint_param = freq_hz;
    cmd.promise = prom;

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    if (!is_streaming_.load()) {
        std::lock_guard<std::mutex> lk(lifecycle_mutex_);
        if (!is_streaming_.load()) {
            process_pending_commands();
        }
    }
    return fut;
}

std::future<bool> SdrController::request_sample_rate(uint32_t rate_hz) {
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    SdrCommand cmd;
    cmd.type = SdrCommandType::SetSampleRate;
    cmd.uint_param = rate_hz;
    cmd.promise = prom;

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    if (!is_streaming_.load()) {
        std::lock_guard<std::mutex> lk(lifecycle_mutex_);
        if (!is_streaming_.load()) {
            process_pending_commands();
        }
    }
    return fut;
}

std::future<bool> SdrController::request_gain(float gain_db, bool auto_gain) {
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    SdrCommand cmd;
    cmd.type = SdrCommandType::SetGain;
    cmd.float_param = gain_db;
    cmd.bool_param = auto_gain;
    cmd.promise = prom;

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    if (!is_streaming_.load()) {
        std::lock_guard<std::mutex> lk(lifecycle_mutex_);
        if (!is_streaming_.load()) {
            process_pending_commands();
        }
    }
    return fut;
}

std::future<bool> SdrController::request_bias_tee(bool enabled) {
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    SdrCommand cmd;
    cmd.type = SdrCommandType::SetBiasTee;
    cmd.bool_param = enabled;
    cmd.promise = prom;

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    if (!is_streaming_.load()) {
        std::lock_guard<std::mutex> lk(lifecycle_mutex_);
        if (!is_streaming_.load()) {
            process_pending_commands();
        }
    }
    return fut;
}

std::future<bool> SdrController::request_ppm(int ppm) {
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    SdrCommand cmd;
    cmd.type = SdrCommandType::SetPpm;
    cmd.int_param = ppm;
    cmd.promise = prom;

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    if (!is_streaming_.load()) {
        std::lock_guard<std::mutex> lk(lifecycle_mutex_);
        if (!is_streaming_.load()) {
            process_pending_commands();
        }
    }
    return fut;
}

std::string SdrController::get_device_name() const {
    return device_ ? device_->get_name() : "None";
}

std::string SdrController::get_status_detail() const {
    return device_ ? device_->get_status_detail() : "Disconnected";
}

std::vector<float> SdrController::get_valid_gains() {
    return device_ ? device_->get_valid_gains() : std::vector<float>();
}

void SdrController::add_sample_callback(SampleCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    sample_callbacks_.push_back(cb);
}

std::vector<std::string> SdrController::scan_all_devices() {
    return RtlSdrDevice::scan_devices();
}

std::vector<std::string> SdrController::check_blocking_apps() {
    return RtlSdrDevice::detect_blocking_processes();
}

void SdrController::process_pending_commands() {
    while (true) {
        std::vector<SdrCommand> local_queue;
        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            if (cmd_queue_.empty()) break;
            local_queue = std::move(cmd_queue_);
            cmd_queue_.clear();
        }

        // Coalesce idempotent commands to eliminate tuning lag, driver thrashing, and freezes
        int last_freq_idx = -1;
        int last_rate_idx = -1;
        int last_gain_idx = -1;
        int last_bias_idx = -1;
        int last_ppm_idx  = -1;

        for (int i = 0; i < (int)local_queue.size(); ++i) {
            switch (local_queue[i].type) {
                case SdrCommandType::SetFrequency:  last_freq_idx = i; break;
                case SdrCommandType::SetSampleRate: last_rate_idx = i; break;
                case SdrCommandType::SetGain:       last_gain_idx = i; break;
                case SdrCommandType::SetBiasTee:    last_bias_idx = i; break;
                case SdrCommandType::SetPpm:        last_ppm_idx  = i; break;
                default: break;
            }
        }

        for (int i = 0; i < (int)local_queue.size(); ++i) {
            auto& cmd = local_queue[i];

            bool is_superseded = false;
            if (cmd.type == SdrCommandType::SetFrequency && i < last_freq_idx) is_superseded = true;
            else if (cmd.type == SdrCommandType::SetSampleRate && i < last_rate_idx) is_superseded = true;
            else if (cmd.type == SdrCommandType::SetGain && i < last_gain_idx) is_superseded = true;
            else if (cmd.type == SdrCommandType::SetBiasTee && i < last_bias_idx) is_superseded = true;
            else if (cmd.type == SdrCommandType::SetPpm && i < last_ppm_idx) is_superseded = true;

            if (is_superseded) {
                if (cmd.promise) {
                    cmd.promise->set_value(true);
                }
                continue;
            }

            bool success = false;
            if (device_) {
                switch (cmd.type) {
                    case SdrCommandType::SetFrequency: {
                        success = device_->set_frequency(cmd.uint_param);
                        uint64_t actual = device_->get_frequency();
                        current_freq_.store(actual);
                        if (freq_changed_cb_) {
                            freq_changed_cb_(actual);
                        }
                        break;
                    }
                    case SdrCommandType::SetSampleRate: {
                        success = device_->set_sample_rate((uint32_t)cmd.uint_param);
                        current_rate_.store(device_->get_sample_rate());
                        break;
                    }
                    case SdrCommandType::SetGain: {
                        success = device_->set_gain(cmd.float_param, cmd.bool_param);
                        current_gain_.store(device_->get_gain());
                        auto_gain_.store(device_->is_auto_gain());
                        break;
                    }
                    case SdrCommandType::SetBiasTee: {
                        success = device_->set_bias_tee(cmd.bool_param);
                        bias_tee_.store(device_->get_bias_tee());
                        break;
                    }
                    case SdrCommandType::SetPpm: {
                        success = device_->set_ppm(cmd.int_param);
                        ppm_.store(device_->get_ppm());
                        break;
                    }
                    case SdrCommandType::ResetBuffer: {
                        device_->reset_buffer();
                        success = true;
                        break;
                    }
                    case SdrCommandType::Stop: {
                        success = true;
                        break;
                    }
                }
            }

            if (cmd.promise) {
                cmd.promise->set_value(success);
            }
        }
    }
}

void SdrController::stream_worker() {
#ifdef _WIN32
    // Elevate worker thread priority to TIME_CRITICAL to eliminate OS scheduling jitter and USB underruns
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        // Fallback to HIGHEST if TIME_CRITICAL is restricted by OS policy
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    }
#endif

    while (!stop_requested_) {
        // Safe point to execute queued hardware control commands
        process_pending_commands();

        if (!is_streaming_ || !device_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int bytes_read = 0;
        int ret = device_->read_samples(rx_buffer_.data(), (int)rx_buffer_.size(), &bytes_read);
        if (ret == 0 && bytes_read > 0) {
            // High-throughput lock-free SPSC buffer ingestion
            ring_buffer_.write_lossy(rx_buffer_.data(), (size_t)bytes_read);

            std::lock_guard<std::mutex> lock(cb_mutex_);
            for (auto& cb : sample_callbacks_) {
                cb(rx_buffer_.data(), (size_t)bytes_read);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}
