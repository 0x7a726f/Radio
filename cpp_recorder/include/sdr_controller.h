#pragma once

#include "sdr_device.h"
#include "constants.h"
#include "lockfree_ringbuffer.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <atomic>
#include <future>

enum class SdrCommandType {
    SetFrequency,
    SetSampleRate,
    SetGain,
    SetBiasTee,
    SetPpm,
    ResetBuffer,
    Stop
};

struct SdrCommand {
    SdrCommandType type;
    uint64_t uint_param = 0;
    float float_param = 0.0f;
    bool bool_param = false;
    int int_param = 0;
    std::shared_ptr<std::promise<bool>> promise;
};

class SdrController {
public:
    using SampleCallback = std::function<void(const uint8_t* data, size_t length)>;
    using FreqChangedCallback = std::function<void(uint64_t new_freq_hz)>;

    SdrController();
    ~SdrController();

    bool initialize(bool force_virtual = false, uint32_t device_index = 0);
    void shutdown();

    bool start_streaming();
    void stop_streaming();
    bool is_streaming() const { return is_streaming_.load(); }

    // Asynchronous non-blocking control methods (Safe to call from GUI thread)
    std::future<bool> request_frequency(uint64_t freq_hz);
    std::future<bool> request_sample_rate(uint32_t rate_hz);
    std::future<bool> request_gain(float gain_db, bool auto_gain);
    std::future<bool> request_bias_tee(bool enabled);
    std::future<bool> request_ppm(int ppm);

    // Synchronous queries (reads cached atomic state)
    uint64_t get_current_frequency() const { return current_freq_.load(); }
    uint32_t get_current_sample_rate() const { return current_rate_.load(); }
    float get_current_gain() const { return current_gain_.load(); }
    bool is_auto_gain() const { return auto_gain_.load(); }
    bool is_bias_tee_enabled() const { return bias_tee_.load(); }
    int get_current_ppm() const { return ppm_.load(); }
    bool is_simulated() const { return is_simulated_.load(); }
    std::string get_device_name() const;
    std::string get_status_detail() const;
    std::vector<float> get_valid_gains();

    // Callbacks
    void add_sample_callback(SampleCallback cb);
    void set_freq_changed_callback(FreqChangedCallback cb) { freq_changed_cb_ = cb; }

    // Lock-Free Ring Buffer Telemetry
    float get_ring_buffer_fill_pct() const { return ring_buffer_.fill_percentage(); }
    uint64_t get_dropped_samples_count() const { return ring_buffer_.dropped_count(); }
    LockFreeSpscRingBuffer<uint8_t>& get_ring_buffer() { return ring_buffer_; }

    static std::vector<std::string> scan_all_devices();
    static std::vector<std::string> check_blocking_apps();

private:
    void stream_worker();
    void process_pending_commands();

    std::unique_ptr<ISdrDevice> device_;
    std::atomic<bool> is_streaming_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread worker_thread_;

    std::atomic<uint64_t> current_freq_{SdrConstants::DEFAULT_CENTER_FREQ_HZ};
    std::atomic<uint32_t> current_rate_{SdrConstants::DEFAULT_SAMPLE_RATE_HZ};
    std::atomic<float> current_gain_{SdrConstants::DEFAULT_GAIN_DB};
    std::atomic<bool> auto_gain_{false};
    std::atomic<bool> bias_tee_{false};
    std::atomic<int> ppm_{0};
    std::atomic<bool> is_simulated_{true};

    // Lifecycle & device access mutex
    std::mutex lifecycle_mutex_;

    // Thread-safe command queue
    std::mutex cmd_mutex_;
    std::vector<SdrCommand> cmd_queue_;

    // Sample subscribers
    std::mutex cb_mutex_;
    std::vector<SampleCallback> sample_callbacks_;
    FreqChangedCallback freq_changed_cb_;

    std::vector<uint8_t> rx_buffer_;
    LockFreeSpscRingBuffer<uint8_t> ring_buffer_{SdrConstants::RING_BUFFER_CAPACITY};
};
