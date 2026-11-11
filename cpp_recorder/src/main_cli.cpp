#include "sdr_controller.h"
#include "cs16_writer.h"
#include "constants.h"

#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>

static std::atomic<bool> g_interrupted{false};

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_interrupted.store(true);
        return TRUE;
    }
    return FALSE;
}

static uint64_t parse_freq_or_rate(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)::tolower(c); });
    double mul = 1.0;
    if (s.find("mhz") != std::string::npos || s.find("msps") != std::string::npos || s.find("m") != std::string::npos) {
        mul = 1e6;
    } else if (s.find("khz") != std::string::npos || s.find("ksps") != std::string::npos || s.find("k") != std::string::npos) {
        mul = 1e3;
    }
    double v = 0.0;
    if (sscanf(s.c_str(), "%lf", &v) == 1) {
        if (mul == 1.0 && v < 10000.0) {
            v *= 1e6;
        } else {
            v *= mul;
        }
        return (uint64_t)v;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    double duration_s = 60.0;
    uint64_t center_freq_hz = SdrConstants::DEFAULT_CENTER_FREQ_HZ;
    uint32_t sample_rate_hz = SdrConstants::DEFAULT_SAMPLE_RATE_HZ;
    float gain_db = SdrConstants::DEFAULT_GAIN_DB;
    bool bias_tee = false;
    std::string output_path = "";
    bool force_virtual = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" || arg == "-d") {
            if (i + 1 < argc) duration_s = std::atof(argv[++i]);
        } else if (arg == "--fc") {
            if (i + 1 < argc) center_freq_hz = parse_freq_or_rate(argv[++i]);
        } else if (arg == "--fs") {
            if (i + 1 < argc) sample_rate_hz = (uint32_t)parse_freq_or_rate(argv[++i]);
        } else if (arg == "--gain" || arg == "-g") {
            if (i + 1 < argc) gain_db = (float)std::atof(argv[++i]);
        } else if (arg == "--bias-tee") {
            bias_tee = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) output_path = argv[++i];
        } else if (arg == "--virtual") {
            force_virtual = true;
        } else if (arg == "--cli") {
            // No-op, compatibility flag
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: sdr_recorder_cli [options]\n"
                      << "  --duration, -d <seconds>  Capture duration (default 60)\n"
                      << "  --fc <freq>              Center frequency e.g. 1420.5M, 1420500000\n"
                      << "  --fs <rate>              Sample rate e.g. 2.4M, 2400000\n"
                      << "  --gain, -g <dB>          Tuner LNA gain in dB (default 25.4)\n"
                      << "  --bias-tee               Enable 4.5V LNA bias-tee\n"
                      << "  -o, --output <path>      Output .cs16 file path\n"
                      << "  --virtual                Force Virtual SDR simulation mode\n";
            return 0;
        }
    }

    std::cout << "======================================================================\n"
              << "  HYDROGEN LINE SDR CS16 HEADLESS RECORDER (C++)\n"
              << "======================================================================\n"
              << std::fixed << std::setprecision(3)
              << "  Center Frequency:  " << (center_freq_hz / 1e6) << " MHz\n"
              << std::setprecision(2)
              << "  Sample Rate:       " << (sample_rate_hz / 1e6) << " MSPS\n"
              << std::setprecision(1)
              << "  Duration:          " << duration_s << " seconds\n"
              << "  Gain:              " << gain_db << " dB\n"
              << "  Bias-Tee (4.5V):   " << (bias_tee ? "Enabled" : "Disabled") << "\n"
              << "----------------------------------------------------------------------\n";

    SdrController sdr;
    Cs16Writer writer("recordings");

    sdr.add_sample_callback([&](const uint8_t* data, size_t len) {
        writer.feed_samples(data, len);
    });

    sdr.initialize(force_virtual, 0);
    sdr.request_frequency(center_freq_hz).wait();
    sdr.request_sample_rate(sample_rate_hz).wait();
    sdr.request_gain(gain_db, false).wait();
    sdr.request_bias_tee(bias_tee).wait();

    std::cout << "Using device: " << sdr.get_device_name() << "\n";

    std::string actual_file = writer.start_recording(
        sample_rate_hz, center_freq_hz, sdr.get_device_name(),
        gain_db, bias_tee, output_path, duration_s
    );

    std::cout << "Recording to: " << actual_file << "\n";

    sdr.start_streaming();
    auto start_t = std::chrono::high_resolution_clock::now();

    while (!g_interrupted.load()) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_t).count();
        if (elapsed >= duration_s || !writer.is_recording()) {
            break;
        }

        RecordingStats st = writer.get_stats();
        std::cout << "\rRecording: " << std::setw(5) << std::fixed << std::setprecision(1) << elapsed
                  << "s / " << std::setw(5) << duration_s << "s | "
                  << "Size: " << st.size_str << " | "
                  << "Free: " << st.free_space_str << " | "
                  << "Samples: " << st.samples_written
                  << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (g_interrupted.load()) {
        std::cout << "\nInterrupted by user.\n";
    }

    std::cout << "\nFinalizing recording...\n";
    writer.stop_recording();
    sdr.stop_streaming();
    sdr.shutdown();

    RecordingStats final_st = writer.get_stats();
    std::cout << "----------------------------------------------------------------------\n"
              << "Recording finished successfully!\n"
              << "CS16 Data File:     " << final_st.full_path << "\n"
              << "Accompanied TXT:    " << final_st.full_path.substr(0, final_st.full_path.find_last_of('.')) + ".txt\n"
              << "Sidecar JSON:       " << final_st.full_path.substr(0, final_st.full_path.find_last_of('.')) + ".json\n"
              << "Total Samples:      " << final_st.samples_written << "\n"
              << "Total Bytes:        " << final_st.bytes_written << " bytes\n"
              << "======================================================================\n";

    return 0;
}
