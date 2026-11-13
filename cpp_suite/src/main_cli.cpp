#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <thread>

#include "constants.h"
#include "config.h"
#include "sdr_controller.h"
#include "cs16_writer.h"
#include "sidecar_reader.h"
#include "streaming_processor.h"
#include "dc_excision.h"
#include "rfi_filter.h"
#include "baseline_fitter.h"
#include "astrophysics.h"
#include "exporter.h"

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
    if (s.find("ghz") != std::string::npos || s.find("g") != std::string::npos) {
        mul = 1e9;
    } else if (s.find("mhz") != std::string::npos || s.find("msps") != std::string::npos || s.find("m") != std::string::npos) {
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

static void print_banner() {
    std::cout << "\n======================================================================\n";
    std::cout << "  HYDROGEN LINE UNIFIED SDR & ASTROPHYSICS SUITE (CLI)\n";
    std::cout << "======================================================================\n";
}

static void print_help(const char* prog) {
    print_banner();
    std::cout << "Usage:\n"
              << "  " << prog << " record  [options]   Run headless real-time SDR CS16 recorder\n"
              << "  " << prog << " analyze [options]   Run scientific radio astronomy offline analysis\n"
              << "\n"
              << "----------------------------------------------------------------------\n"
              << "RECORD MODE OPTIONS (Subcommand: record):\n"
              << "  --fc <freq>              Center LO frequency (e.g. 1420.5M, 1420500000)\n"
              << "  --fs <rate>              Sample rate (e.g. 2.4M, 2400000)\n"
              << "  -d, -t, --duration <sec> Capture duration in seconds (default: 60)\n"
              << "  -g, --gain <dB>          Tuner LNA gain in dB (default: 25.4)\n"
              << "  --bias-tee               Enable 4.5V LNA bias-tee\n"
              << "  -o, --output <path>      Output .cs16 file path\n"
              << "  --virtual                Force Virtual SDR simulation mode\n"
              << "\n"
              << "ANALYZE MODE OPTIONS (Subcommand: analyze):\n"
              << "  FILE, -f, --file PATH    Path to raw CS16 file (auto-discovered if omitted)\n"
              << "  --fc <freq>              Center LO frequency override\n"
              << "  --fs <rate>              Sample rate override\n"
              << "  -n, --fft-size <bins>    FFT channel size (2048, 4096, 8192, 16384). Default: 8192\n"
              << "  -t, --duration <sec>     Max duration in seconds to process (default: entire file)\n"
              << "  --baseline <method>      Baseline model: 'ripple_filter', 'als', 'poly'\n"
              << "  --tsys <Kelvin>          Assumed system noise temperature (default: 100.0 K)\n"
              << "  -o, --output-dir <dir>   Output directory for PNG plots and CSV (default: 'output')\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " record --fc 1420.5M --fs 2.4M --duration 60 -o capture.cs16\n"
              << "  " << prog << " analyze capture.cs16 -o output/\n"
              << "======================================================================\n";
}

int run_record(int argc, char* argv[], int start_idx) {
    double duration_s = 60.0;
    uint64_t center_freq_hz = SdrConstants::DEFAULT_CENTER_FREQ_HZ;
    uint32_t sample_rate_hz = SdrConstants::DEFAULT_SAMPLE_RATE_HZ;
    float gain_db = SdrConstants::DEFAULT_GAIN_DB;
    bool bias_tee = false;
    std::string output_path = "";
    bool force_virtual = false;

    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--duration" || arg == "-d" || arg == "-t") {
            if (i + 1 < argc) {
                duration_s = std::atof(argv[++i]);
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--fc") {
            if (i + 1 < argc) {
                center_freq_hz = parse_freq_or_rate(argv[++i]);
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--fs") {
            if (i + 1 < argc) {
                sample_rate_hz = static_cast<uint32_t>(parse_freq_or_rate(argv[++i]));
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--gain" || arg == "-g") {
            if (i + 1 < argc) {
                gain_db = static_cast<float>(std::atof(argv[++i]));
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--bias-tee") {
            bias_tee = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--virtual") {
            force_virtual = true;
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unrecognized option for record: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    std::cout << "\n======================================================================\n"
              << "  HYDROGEN LINE SDR CS16 HEADLESS RECORDER\n"
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

    sdr.add_sample_callback([&writer](const uint8_t* data, size_t len) {
        writer.feed_samples(data, len);
    });

    if (!sdr.initialize(force_virtual, 0)) {
        std::cerr << "Failed to initialize SDR device!\n";
        return 1;
    }

    sdr.request_frequency(center_freq_hz);
    sdr.request_sample_rate(sample_rate_hz);
    sdr.request_gain(gain_db, false);
    if (bias_tee) sdr.request_bias_tee(true);

    std::string actual_file = writer.start_recording(
        sample_rate_hz, center_freq_hz, sdr.get_device_name(), gain_db, bias_tee, output_path, duration_s
    );

    std::cout << "Recording started: " << actual_file << "\n";
    sdr.start_streaming();

    auto t0 = std::chrono::high_resolution_clock::now();
    while (!g_interrupted.load() && writer.is_recording()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto now = std::chrono::high_resolution_clock::now();
        double el = std::chrono::duration<double>(now - t0).count();
        auto stats = writer.get_stats();

        std::cout << "\r[REC] " << std::fixed << std::setprecision(1) << el << "/" << duration_s << "s"
                  << " | Size: " << stats.size_str
                  << " | Samples: " << (stats.samples_written / 1000000.0) << "M"
                  << " | Drops: " << stats.dropped_blocks
                  << " | Ring: " << std::setprecision(1) << sdr.get_ring_buffer_fill_pct() << "%"
                  << std::flush;

        if (duration_s > 0.0 && el >= duration_s) break;
    }
    std::cout << "\n";

    sdr.stop_streaming();
    writer.stop_recording();
    sdr.shutdown();

    std::cout << "\nRecording completed successfully!\n"
              << "File saved: " << actual_file << "\n"
              << "Sidecar metadata (.json, .txt) written alongside binary.\n";
    return 0;
}

int run_analyze(int argc, char* argv[], int start_idx) {
    using namespace astro;

    std::string filepath;
    std::string fc_str;
    std::string fs_str;
    std::string baseline_str;
    std::string out_dir = "output";
    int fft_size = 8192;
    double duration_s = 0.0;
    double tsys = 100.0;

    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc) {
                filepath = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--fc" || arg == "--center-freq") {
            if (i + 1 < argc) {
                fc_str = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--fs" || arg == "--sample-rate") {
            if (i + 1 < argc) {
                fs_str = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "-n" || arg == "--fft-size") {
            if (i + 1 < argc) {
                fft_size = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "-t" || arg == "--duration") {
            if (i + 1 < argc) {
                duration_s = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--baseline") {
            if (i + 1 < argc) {
                baseline_str = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "--tsys") {
            if (i + 1 < argc) {
                tsys = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg == "-o" || arg == "--output-dir") {
            if (i + 1 < argc) {
                out_dir = argv[++i];
            } else {
                std::cerr << "Error: Missing value for " << arg << "\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            if (filepath.empty()) {
                filepath = arg;
            } else {
                std::cerr << "Warning: Extra positional argument ignored: " << arg << "\n";
            }
        } else {
            std::cerr << "Error: Unrecognized option for analyze: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    // Auto-discover if no file provided
    if (filepath.empty()) {
        std::vector<std::string> search_dirs = {
            ".",
            "recordings",
            "bin/recordings",
            "../recordings",
            "../bin/recordings"
        };
        auto discovered = SidecarReader::scan_cs16_files(search_dirs);
        if (!discovered.empty()) {
            std::cout << "\nDiscovered CS16 files in workspace:\n";
            for (size_t i = 0; i < std::min<size_t>(discovered.size(), 5); ++i) {
                uint64_t sz = std::filesystem::file_size(discovered[i]);
                std::cout << "  [" << (i + 1) << "] " << discovered[i]
                          << " (" << std::fixed << std::setprecision(1) << sz / (1024.0 * 1024.0) << " MB)\n";
            }
            std::cout << "Selecting newest capture: " << discovered[0] << "\n\n";
            filepath = discovered[0];
        } else {
            std::cerr << "Error: No CS16 files found. Please provide a path using: radio_suite_cli analyze <file.cs16>\n";
            return 1;
        }
    }

    if (!std::filesystem::exists(filepath)) {
        std::cerr << "Error: Specified file does not exist: " << filepath << "\n";
        return 1;
    }

    AstroConfig config;
    config.fft_size = fft_size;
    config.max_duration_s = duration_s;
    config.system_temp_k = tsys;
    config.output_dir = out_dir;

    if (!baseline_str.empty()) {
        config.baseline_method = parse_baseline_method(baseline_str);
    }

    // Auto-detect and parse sidecar
    ObservationMetadata sidecar = SidecarReader::parse(filepath);
    if (sidecar.found_sidecar) {
        SidecarReader::apply_to_config(sidecar, config);
        std::cout << "  [Metadata] Sidecar auto-detected: " << sidecar.sidecar_source << "\n"
                  << "             Center Frequency: " << (config.center_freq_hz / 1e6) << " MHz\n"
                  << "             Sample Rate:      " << (config.sample_rate_hz / 1e6) << " MSPS\n";
    }

    if (!fc_str.empty()) config.center_freq_hz = parse_frequency_or_rate(fc_str, true);
    if (!fs_str.empty()) config.sample_rate_hz = parse_frequency_or_rate(fs_str, false);

    std::cout << "\n======================================================================\n"
              << "  ASTROPHYSICAL DSP ANALYSIS PIPELINE\n"
              << "======================================================================\n"
              << "  Target CS16:       " << filepath << "\n"
              << "  Center LO:         " << (config.center_freq_hz / 1e6) << " MHz\n"
              << "  Sample Rate:       " << (config.sample_rate_hz / 1e6) << " MSPS\n"
              << "  FFT Channels:      " << config.fft_size << "\n"
              << "  Baseline Method:   " << baseline_method_to_string(config.baseline_method) << "\n"
              << "  System Temp Tsys:  " << config.system_temp_k << " K\n"
              << "  Output Directory:  " << config.output_dir << "\n"
              << "----------------------------------------------------------------------\n";

    // 1. Streaming accumulation
    std::cout << "--> Accumulating FFT power spectra...\n";
    StreamingProcessor processor(filepath, config);
    StreamingResults stream = processor.process([](double pct, double speed, double eta) {
        std::cout << "\r[Progress] " << std::fixed << std::setprecision(1) << pct << "%"
                  << " | Throughput: " << speed << " MB/s"
                  << " | ETA: " << eta << "s   " << std::flush;
    });
    std::cout << "\nProcessed " << stream.total_samples << " complex samples in "
              << std::fixed << std::setprecision(2) << stream.elapsed_processing_time_s << "s ("
              << stream.throughput_mb_s << " MB/s)\n";

    // 2. DC Notch Excision
    std::cout << "--> Excising center DC local oscillator spike...\n";
    DCExcisionInfo dc_info;
    std::vector<double> dc_clean = DCExcision::excise(stream.averaged_power, config, dc_info);

    // 3. RFI Mitigation
    std::cout << "--> Mitigating RFI spikes...\n";
    int num_rfi = 0;
    std::vector<double> rfi_clean = RFIFilter::mitigate(dc_clean, stream.freq_mhz, config, num_rfi);

    // 4. Baseline Modeling
    std::cout << "--> Fitting baseline passband (" << baseline_method_to_string(config.baseline_method) << ")...\n";
    BaselineResults baseline = BaselineFitter::fit(rfi_clean, stream.freq_mhz, config);

    // 5. Scientific Calibration
    std::cout << "--> Calibrating to Kelvin and computing Doppler velocities...\n";
    size_t nfft = stream.freq_mhz.size();
    std::vector<double> cal_k(nfft);
    std::vector<double> cal_db(nfft);
    std::vector<double> v_kms = Astrophysics::compute_doppler_velocity(stream.freq_hz);

    for (size_t i = 0; i < nfft; ++i) {
        double base_safe = std::max(1e-12, baseline.baseline_power[i]);
        cal_k[i] = config.system_temp_k * (rfi_clean[i] - baseline.baseline_power[i]) / base_safe;
        cal_db[i] = 10.0 * std::log10(std::max(1e-6, rfi_clean[i] / base_safe));
    }

    double slope = 0.0, intercept = 0.0;
    BaselineFitter::refine_local(v_kms, cal_k, cal_db, config, slope, intercept);

    // 6. Astrophysics Metrics & Gaussian Curve Fit
    std::cout << "--> Computing astrophysical metrics and Levenberg-Marquardt fit...\n";
    AstrophysicalMetrics metrics = Astrophysics::calculate_metrics(
        stream.freq_mhz, v_kms, cal_k, cal_db, config
    );

    // 7. Structured Export
    std::cout << "--> Rendering publication-grade PNGs and CSV exports...\n";
    FileMetadata meta;
    meta.filepath = filepath;
    meta.filesize = stream.file_size_bytes;
    meta.total_samples = stream.total_samples;
    meta.duration_s = stream.total_duration_s;
    meta.processing_time_s = stream.elapsed_processing_time_s;
    meta.throughput_mb_s = stream.throughput_mb_s;

    Exporter::export_all_artifacts(
        config.output_dir, filepath, config, meta, stream,
        rfi_clean, baseline.baseline_power, cal_k, cal_db, metrics
    );

    // 8. Print Executive Summary
    std::cout << "\n======================================================================\n"
              << "  ASTROPHYSICAL EXECUTIVE REPORT\n"
              << "======================================================================\n"
              << std::fixed << std::setprecision(4)
              << "  Peak Frequency:         " << metrics.peak_freq_mhz << " MHz\n"
              << std::setprecision(2)
              << "  Radial Velocity:        " << metrics.peak_vel_kms << " km/s\n"
              << "  Peak Brightness Temp:   " << metrics.peak_temp_k << " K (" << metrics.peak_power_db << " dB)\n"
              << "  Noise Floor RMS:        " << metrics.noise_std_k << " K\n"
              << "  Detection SNR:          " << metrics.snr << " sigma\n"
              << "  Line FWHM Width:        " << metrics.fwhm_kms << " km/s\n"
              << "  Integrated Flux Area:   " << metrics.integrated_flux_k_kms << " K*km/s\n"
              << "  H I Column Density:     " << std::scientific << metrics.column_density_cm2 << " atoms/cm^2\n"
              << std::fixed
              << "----------------------------------------------------------------------\n"
              << "  All artifacts successfully written to: " << config.output_dir << "/\n"
              << "======================================================================\n";

    return 0;
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return (char)::tolower(c); });

    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        print_help(argv[0]);
        return 0;
    }

    if (cmd == "record") {
        return run_record(argc, argv, 2);
    } else if (cmd == "analyze") {
        return run_analyze(argc, argv, 2);
    } else {
        // Auto-detect mode based on flags or input file
        bool has_record_flag = false;
        bool has_analyze_flag = false;
        bool is_cs16_file = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-o" || arg == "--output" || arg == "--output-dir") {
                // Skip output path parameter so an output .cs16 is not confused with an input file to analyze
                i++;
                continue;
            }
            if (arg == "--gain" || arg == "-g" || arg == "--bias-tee" || arg == "--virtual") {
                has_record_flag = true;
            }
            if (arg == "--baseline" || arg == "--tsys" || arg == "-n" || arg == "--fft-size") {
                has_analyze_flag = true;
            }
            if (arg.size() >= 5 && arg.substr(arg.size() - 5) == ".cs16") {
                is_cs16_file = true;
            }
        }

        if (has_record_flag && !is_cs16_file && !has_analyze_flag) {
            return run_record(argc, argv, 1);
        } else if (is_cs16_file || has_analyze_flag || std::filesystem::exists(argv[1])) {
            return run_analyze(argc, argv, 1);
        } else if (cmd.rfind("--fc", 0) == 0 || cmd.rfind("-d", 0) == 0 || cmd.rfind("-t", 0) == 0) {
            return run_record(argc, argv, 1);
        } else {
            std::cerr << "Error: Unknown subcommand or file '" << argv[1] << "'.\n";
            print_help(argv[0]);
            return 1;
        }
    }
}
