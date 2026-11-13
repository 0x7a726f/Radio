#include "sdr_device.h"
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>

static std::wstring to_wide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

static std::string to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

static std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)::tolower(c); });
    return s;
}

RtlSdrDevice::RtlSdrDevice() {
    load_dll();
}

RtlSdrDevice::~RtlSdrDevice() {
    close();
    unload_dll();
}

bool RtlSdrDevice::load_dll() {
    if (dll_handle_) return true;

    // Candidates to search for rtlsdr.dll
    std::vector<std::wstring> candidates;

    // Check executable directory and local bin/PATH
    wchar_t exe_path[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) {
        wchar_t* last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash) {
            *last_slash = L'\0';
            candidates.push_back(std::wstring(exe_path) + L"\\rtlsdr.dll");
            candidates.push_back(std::wstring(exe_path) + L"\\bin\\rtlsdr.dll");
        }
    }

    candidates.push_back(L"rtlsdr.dll");

    HMODULE mod = nullptr;
    for (const auto& path : candidates) {
        mod = LoadLibraryW(path.c_str());
        if (mod) break;
    }

    if (!mod) {
        status_detail_ = "rtlsdr.dll could not be loaded.";
        return false;
    }

    dll_handle_ = (void*)mod;

    #define LOAD_PROC(name) fn_##name = (pfn_rtlsdr_##name)GetProcAddress(mod, "rtlsdr_" #name)
    LOAD_PROC(get_device_count);
    LOAD_PROC(get_device_name);
    LOAD_PROC(get_device_usb_strings);
    LOAD_PROC(open);
    LOAD_PROC(close);
    LOAD_PROC(set_center_freq);
    LOAD_PROC(get_center_freq);
    LOAD_PROC(set_sample_rate);
    LOAD_PROC(get_sample_rate);
    LOAD_PROC(set_tuner_gain_mode);
    LOAD_PROC(set_tuner_gain);
    LOAD_PROC(get_tuner_gains);
    LOAD_PROC(set_freq_correction);
    LOAD_PROC(reset_buffer);
    LOAD_PROC(read_sync);
    LOAD_PROC(set_bias_tee);
    #undef LOAD_PROC

    if (!fn_open || !fn_close || !fn_read_sync) {
        status_detail_ = "rtlsdr.dll is missing core API exports.";
        unload_dll();
        return false;
    }

    status_detail_ = "rtlsdr.dll loaded successfully.";
    return true;
}

void RtlSdrDevice::unload_dll() {
    if (dll_handle_) {
        FreeLibrary((HMODULE)dll_handle_);
        dll_handle_ = nullptr;
    }
}

std::vector<std::string> RtlSdrDevice::detect_blocking_processes() {
    std::vector<std::string> blocking;
    static const std::vector<std::string> known_sdr = {
        "satdump", "sdrpp", "sdrangel", "tv_app", "rtl_tcp", "dump1090", "welle"
    };

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return blocking;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            std::string proc_name = to_lower_str(to_utf8(pe.szExeFile));
            for (const auto& target : known_sdr) {
                if (proc_name.find(target) != std::string::npos) {
                    blocking.push_back(to_utf8(pe.szExeFile));
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return blocking;
}

std::vector<std::string> RtlSdrDevice::scan_devices() {
    RtlSdrDevice dev_probe;
    if (!dev_probe.load_dll() || !dev_probe.fn_get_device_count) {
        return {"Virtual SDR Mode (Simulation)"};
    }

    uint32_t count = dev_probe.fn_get_device_count();
    if (count == 0) {
        return {"Virtual SDR Mode (Simulation)"};
    }

    std::vector<std::string> result;
    for (uint32_t i = 0; i < count; ++i) {
        char mfg[256] = {0}, prod[256] = {0}, ser[256] = {0};
        int ret = -1;
        if (dev_probe.fn_get_device_usb_strings) {
            ret = dev_probe.fn_get_device_usb_strings(i, mfg, prod, ser);
        }
        if (ret == 0 && (prod[0] != '\0' || mfg[0] != '\0')) {
            std::string name = std::string(mfg) + " " + prod;
            if (ser[0] != '\0') {
                name += " #" + std::string(ser);
            }
            result.push_back(name);
        } else if (dev_probe.fn_get_device_name) {
            const char* name = dev_probe.fn_get_device_name(i);
            result.push_back(name ? name : "Generic RTL2832U #" + std::to_string(i));
        } else {
            result.push_back("Generic RTL2832U #" + std::to_string(i));
        }
    }
    return result;
}

bool RtlSdrDevice::open(uint32_t index) {
    close();

    // Check for blocking apps holding WinUSB
    auto blocking = detect_blocking_processes();
    if (!blocking.empty()) {
        status_detail_ = "USB device held by active SDR process. Running in Virtual mode.";
        return false;
    }

    if (!load_dll() || !fn_open) {
        status_detail_ = "rtlsdr.dll unavailable.";
        return false;
    }

    uint32_t count = fn_get_device_count ? fn_get_device_count() : 0;
    if (count == 0 || index >= count) {
        status_detail_ = "No hardware RTL-SDR dongle detected.";
        return false;
    }

    void* dev = nullptr;
    int ret = fn_open(&dev, index);
    if (ret != 0 || !dev) {
        status_detail_ = "Hardware found but open failed (error code " + std::to_string(ret) + ").";
        return false;
    }

    dev_ = dev;
    {
        char mfg[256] = {0}, prod[256] = {0}, ser[256] = {0};
        int usb_ret = fn_get_device_usb_strings ? fn_get_device_usb_strings(index, mfg, prod, ser) : -1;
        if (usb_ret == 0 && (prod[0] != '\0' || mfg[0] != '\0')) {
            device_name_ = std::string(mfg) + " " + prod;
            if (ser[0] != '\0') device_name_ += " #" + std::string(ser);
        } else if (fn_get_device_name) {
            const char* name = fn_get_device_name(index);
            device_name_ = name ? name : ("Generic RTL2832U #" + std::to_string(index));
        } else {
            device_name_ = "RTL-SDR #" + std::to_string(index);
        }
    }
    status_detail_ = "Connected: " + device_name_;

    // Set initial configuration
    set_sample_rate(sample_rate_hz_);
    set_frequency(center_freq_hz_);
    set_gain(gain_db_, auto_gain_);
    set_bias_tee(bias_tee_);
    set_ppm(ppm_);

    return true;
}

void RtlSdrDevice::close() {
    if (dev_ && fn_close) {
        if (fn_set_bias_tee && bias_tee_) {
            fn_set_bias_tee(dev_, 0);
        }
        fn_close(dev_);
        dev_ = nullptr;
    }
}

std::vector<float> RtlSdrDevice::get_valid_gains() {
    if (dev_ && fn_get_tuner_gains) {
        int gains_buf[64] = {0};
        int n = fn_get_tuner_gains(dev_, gains_buf);
        if (n > 0) {
            std::vector<float> list;
            list.reserve(n);
            for (int i = 0; i < n; ++i) {
                list.push_back(gains_buf[i] / 10.0f);
            }
            return list;
        }
    }
    // Fallback standard R820T / R828D gains
    return {0.0f, 0.9f, 1.4f, 2.7f, 3.7f, 7.7f, 8.7f, 12.5f, 14.4f, 15.7f, 16.6f,
            19.7f, 20.7f, 22.9f, 25.4f, 28.0f, 29.7f, 32.8f, 33.8f, 36.4f, 37.2f,
            38.6f, 40.2f, 42.1f, 43.4f, 43.9f, 44.5f, 48.0f, 49.6f};
}

bool RtlSdrDevice::set_sample_rate(uint32_t rate_hz) {
    sample_rate_hz_ = rate_hz;
    if (dev_ && fn_set_sample_rate) {
        int ret = fn_set_sample_rate(dev_, rate_hz);
        if (fn_reset_buffer) fn_reset_buffer(dev_);
        return (ret == 0);
    }
    return false;
}

bool RtlSdrDevice::set_frequency(uint64_t freq_hz) {
    if (dev_ && fn_set_center_freq) {
        int ret = -1;
        // Retry up to 3 times in case of transient I2C bus collision
        for (int attempt = 0; attempt < 3; ++attempt) {
            ret = fn_set_center_freq(dev_, (uint32_t)freq_hz);
            if (ret == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (ret != 0) {
            status_detail_ = "Error: rtlsdr_set_center_freq failed after retries (code " + std::to_string(ret) + ")";
            return false;
        }

        // Verify PLL lock by querying tuner frequency back from driver
        if (fn_get_center_freq) {
            uint32_t actual = fn_get_center_freq(dev_);
            if (actual == 0) {
                status_detail_ = "Warning: rtlsdr_get_center_freq returned 0 (Tuner PLL lock check failed)";
            } else {
                center_freq_hz_ = actual;
                if (std::abs((int64_t)actual - (int64_t)freq_hz) > 200000) {
                    status_detail_ = "Tuned to " + std::to_string(actual / 1e6) + " MHz (PLL clamped/quantized)";
                } else {
                    status_detail_ = "Connected: " + device_name_;
                }
            }
        } else {
            center_freq_hz_ = freq_hz;
        }

        if (fn_reset_buffer) {
            fn_reset_buffer(dev_);
        }
        return true;
    }
    return false;
}

bool RtlSdrDevice::set_gain(float gain_db, bool auto_gain) {
    auto_gain_ = auto_gain;
    if (!auto_gain) {
        // Snap requested gain to closest discrete hardware gain step
        auto valid = get_valid_gains();
        if (!valid.empty()) {
            float best = valid.front();
            float best_diff = std::abs(gain_db - best);
            for (float g : valid) {
                float d = std::abs(gain_db - g);
                if (d < best_diff) {
                    best_diff = d;
                    best = g;
                }
            }
            gain_db_ = best;
        } else {
            gain_db_ = gain_db;
        }
    } else {
        gain_db_ = gain_db;
    }

    if (dev_ && fn_set_tuner_gain_mode) {
        if (auto_gain) {
            fn_set_tuner_gain_mode(dev_, 0); // AGC auto
            return true;
        } else {
            fn_set_tuner_gain_mode(dev_, 1); // Manual
            if (fn_set_tuner_gain) {
                return (fn_set_tuner_gain(dev_, static_cast<int>(std::lround(gain_db_ * 10.0f))) == 0);
            }
        }
    }
    return false;
}

bool RtlSdrDevice::set_bias_tee(bool enabled) {
    bias_tee_ = enabled;
    if (dev_ && fn_set_bias_tee) {
        return (fn_set_bias_tee(dev_, enabled ? 1 : 0) == 0);
    }
    return false;
}

bool RtlSdrDevice::set_ppm(int ppm) {
    ppm_ = ppm;
    if (dev_ && fn_set_freq_correction) {
        return (fn_set_freq_correction(dev_, ppm) == 0);
    }
    return false;
}

int RtlSdrDevice::read_samples(uint8_t* buffer, int length, int* bytes_read) {
    if (dev_ && fn_read_sync) {
        return fn_read_sync(dev_, buffer, length, bytes_read);
    }
    return -1;
}

void RtlSdrDevice::reset_buffer() {
    if (dev_ && fn_reset_buffer) {
        fn_reset_buffer(dev_);
    }
}
