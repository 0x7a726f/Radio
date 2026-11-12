#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "constants.h"
#include "config.h"
#include "sidecar_reader.h"
#include "streaming_processor.h"
#include "dc_excision.h"
#include "rfi_filter.h"
#include "baseline_fitter.h"
#include "astrophysics.h"
#include "exporter.h"

using namespace astro;

static void print_banner() {
    std::cout << "\n======================================================================\n";
    std::cout << "  ASTRO - 21cm HYDROGEN LINE C++ SCIENTIFIC DSP ANALYZER (CLI)\n";
    std::cout << "======================================================================\n";
}

static void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " [FILE] [OPTIONS]\n\n"
              << "Positional / File Arguments:\n"
              << "  FILE, -f, --file PATH      Path to the raw CS16 I/Q recording file.\n\n"
              << "SDR Receiver & DSP Options:\n"
              << "  --fc, --center-freq FREQ   SDR center LO frequency (e.g. 1420.5MHz, 1420500000).\n"
              << "  --fs, --sample-rate RATE   SDR sample rate (e.g. 2.4MSPS, 2400000).\n"
              << "  -n, --fft-size SIZE        FFT channel size (e.g. 4096, 8192, 16384). Default: 8192.\n"
              << "  -t, --duration SECS        Max duration in seconds to process (default: entire file).\n"
              << "  --baseline METHOD          Baseline model algorithm ('als', 'poly', 'ripple_filter').\n"
              << "  --tsys TEMP_K              Assumed system noise temp in Kelvin. Default: 100.0.\n"
              << "  -o, --output-dir DIR       Output directory. Default: 'output'.\n"
              << "  -h, --help                 Display this help menu.\n";
}

int main(int argc, char* argv[]) {
    print_banner();

    std::string filepath;
    std::string fc_str;
    std::string fs_str;
    std::string baseline_str;
    std::string out_dir = "output";
    int fft_size = 8192;
    double duration_s = 0.0;
    double tsys = 100.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            filepath = argv[++i];
        } else if ((arg == "--fc" || arg == "--center-freq") && i + 1 < argc) {
            fc_str = argv[++i];
        } else if ((arg == "--fs" || arg == "--sample-rate") && i + 1 < argc) {
            fs_str = argv[++i];
        } else if ((arg == "-n" || arg == "--fft-size") && i + 1 < argc) {
            fft_size = std::stoi(argv[++i]);
        } else if ((arg == "-t" || arg == "--duration") && i + 1 < argc) {
            duration_s = std::stod(argv[++i]);
        } else if (arg == "--baseline" && i + 1 < argc) {
            baseline_str = argv[++i];
        } else if (arg == "--tsys" && i + 1 < argc) {
            tsys = std::stod(argv[++i]);
        } else if ((arg == "-o" || arg == "--output-dir") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg[0] != '-' && filepath.empty()) {
            filepath = arg;
        }
    }

    // Auto-discover files if not provided
    if (filepath.empty()) {
        std::vector<std::string> search_dirs = {
            ".",
            "recordings",
            "../recordings",
            "cpp_recorder/recordings",
            "../cpp_recorder/recordings",
            "Analyzer",
            "../Analyzer"
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
            std::cerr << "Error: No .cs16 file specified or found. Use --file <path>\n";
            return 1;
        }
    }

    if (!std::filesystem::exists(filepath)) {
        std::cerr << "Error: File does not exist: " << filepath << "\n";
        return 1;
    }

    // Auto-inspect sidecar (.json or .txt)
    ObservationMetadata sidecar = SidecarReader::parse(filepath);
    AstroConfig config;
    config.fft_size = fft_size;
    config.max_duration_s = duration_s;
    config.system_temp_k = tsys;
    config.output_dir = out_dir;

    if (!baseline_str.empty()) {
        config.baseline_method = parse_baseline_method(baseline_str);
    }

    if (sidecar.found_sidecar) {
        SidecarReader::apply_to_config(sidecar, config);
        std::cout << "  [Sidecar Detected: " << sidecar.sidecar_source << "]\n";
        std::cout << "    Center LO:   " << std::fixed << std::setprecision(6) << config.center_freq_hz / 1e6 << " MHz\n";
        std::cout << "    Sample Rate: " << std::fixed << std::setprecision(3) << config.sample_rate_hz / 1e6 << " MSPS\n";
        if (sidecar.has_gain) {
            std::cout << "    Gain:        " << std::fixed << std::setprecision(1) << sidecar.gain_db << " dB\n";
        }
        std::cout << "    Bias-Tee:    " << (sidecar.bias_tee ? "Enabled" : "Disabled") << "\n";
    }

    if (!fc_str.empty()) {
        config.center_freq_hz = parse_frequency_or_rate(fc_str, true);
    }
    if (!fs_str.empty()) {
        config.sample_rate_hz = parse_frequency_or_rate(fs_str, true);
    }

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "  Target File:       " << filepath << "\n";
    std::cout << "  Output Directory:  " << config.output_dir << "\n";
    std::cout << "  SDR Center LO:     " << std::fixed << std::setprecision(6) << config.center_freq_hz / 1e6 << " MHz\n";
    std::cout << "  Sample Rate:       " << std::fixed << std::setprecision(3) << config.sample_rate_hz / 1e6 << " MSPS\n";
    std::cout << "  FFT Size:          " << config.fft_size << " bins ("
              << std::fixed << std::setprecision(2) << config.sample_rate_hz / config.fft_size << " Hz/bin)\n";
    std::cout << "  Baseline Method:   " << baseline_method_to_string(config.baseline_method) << "\n";
    std::cout << "----------------------------------------------------------------------\n";

    try {
        // 1. Streaming accumulation
        std::cout << "\n  Streaming spectral accumulation & waterfall extraction...\n";
        StreamingProcessor processor(filepath, config);

        auto progress_printer = [](double pct, double speed, double eta) {
            int bar_len = 24;
            int filled = static_cast<int>(bar_len * (pct / 100.0));
            std::string bar(filled, '=');
            bar.append(bar_len - filled, '-');
            std::cout << "\r  [" << bar << "] " << std::fixed << std::setprecision(1) << pct << "% | "
                      << std::setprecision(1) << speed << " MB/s | ETA: " << std::setprecision(1) << eta << "s   " << std::flush;
        };

        auto t_start = std::chrono::high_resolution_clock::now();
        StreamingResults stream_res = processor.process(progress_printer);
        std::cout << "\r" << std::string(80, ' ') << "\r";

        std::cout << "  -> Streaming complete: " << stream_res.total_blocks << " FFT blocks accumulated ("
                  << std::fixed << std::setprecision(1) << stream_res.throughput_mb_s << " MB/s in "
                  << std::setprecision(2) << stream_res.elapsed_processing_time_s << " s)\n";

        // 2. DC offset spike excision
        std::cout << "  [1/5] Excising SDR center LO DC offset spike...\n";
        DCExcisionInfo dc_info;
        std::vector<double> dc_clean = DCExcision::excise(stream_res.averaged_power, config, dc_info);
        std::cout << "        -> DC spike cleared (" << std::fixed << std::setprecision(1)
                  << dc_info.cleared_bandwidth_khz << " kHz band at " << dc_info.dc_center_freq_mhz << " MHz)\n";

        // 3. Mitigate sharp RFI comb spikes
        std::cout << "  [2/5] Scanning for narrow-band RFI comb lines outside HI window...\n";
        int num_rfi = 0;
        std::vector<double> rfi_clean = RFIFilter::mitigate(dc_clean, stream_res.freq_mhz, config, num_rfi);
        if (num_rfi > 0) {
            std::cout << "        -> Neutralized " << num_rfi << " active RFI spikes via local median interpolation\n";
        } else {
            std::cout << "        -> Clean spectrum: no significant RFI spikes detected\n";
        }

        // 4. Model instrumental baseline
        std::cout << "  [3/5] Estimating instrumental bandpass baseline (" << baseline_method_to_string(config.baseline_method) << ")...\n";
        BaselineResults bl_res = BaselineFitter::fit(rfi_clean, stream_res.freq_mhz, config);

        // 5. Calibrate spectrum
        std::cout << "  [4/5] Computing calibrated antenna temperature and Doppler kinematics...\n";
        size_t nfft = stream_res.freq_mhz.size();
        std::vector<double> cal_k(nfft);
        std::vector<double> cal_db(nfft);
        std::vector<double> vel_kms = Astrophysics::compute_doppler_velocity(stream_res.freq_hz);

        for (size_t i = 0; i < nfft; ++i) {
            double base_safe = std::max(1e-12, bl_res.baseline_power[i]);
            cal_k[i] = config.system_temp_k * (rfi_clean[i] - bl_res.baseline_power[i]) / base_safe;
            cal_db[i] = 10.0 * std::log10(std::max(1e-6, rfi_clean[i] / base_safe));
        }

        // Stage 2 local baseline zeroing
        double slope = 0.0, intercept = 0.0;
        BaselineFitter::refine_local(vel_kms, cal_k, cal_db, config, slope, intercept);

        AstrophysicalMetrics metrics = Astrophysics::calculate_metrics(
            stream_res.freq_mhz, vel_kms, cal_k, cal_db, config
        );

        // 6. Data exports and High-DPI PNGs
        std::cout << "  [5/5] Generating publication-quality figures and data exports...\n";
        FileMetadata meta;
        meta.filepath = filepath;
        meta.filesize = stream_res.file_size_bytes;
        meta.total_samples = stream_res.total_samples;
        meta.duration_s = stream_res.total_duration_s;
        meta.processing_time_s = stream_res.elapsed_processing_time_s;
        meta.throughput_mb_s = stream_res.throughput_mb_s;

        Exporter::export_all_artifacts(
            config.output_dir, filepath, config, meta, stream_res,
            rfi_clean, bl_res.baseline_power, cal_k, cal_db, metrics
        );

        // Terminal Summary Report
        std::cout << "\n======================================================================\n";
        std::cout << "           ASTRO HYDROGEN LINE ANALYSIS COMPLETE\n";
        std::cout << "======================================================================\n";
        std::cout << "  Target Line:            Neutral Hydrogen (H I, 21 cm)\n";
        std::cout << "  Rest Frequency:         " << std::fixed << std::setprecision(6) << HYDROGEN_REST_FREQ_MHZ << " MHz\n";
        std::cout << "  Observed Peak:          " << std::setprecision(6) << metrics.peak_freq_mhz << " MHz\n";
        std::cout << "  Doppler Velocity:       " << std::showpos << std::setprecision(2) << metrics.peak_vel_kms << " km/s (Topocentric)\n";
        std::cout << "  Peak Brightness:        " << std::noshowpos << std::setprecision(2) << metrics.peak_temp_k
                  << " K  (" << std::showpos << std::setprecision(3) << metrics.peak_power_db << " dB)\n";
        std::cout << "  Noise Floor RMS:        " << std::noshowpos << std::setprecision(3) << metrics.noise_std_k << " K\n";
        std::cout << "  Signal-to-Noise Ratio:  " << std::setprecision(1) << metrics.snr << " sigma\n";
        std::cout << "  Line Width (FWHM):      " << std::setprecision(2) << metrics.fwhm_kms << " km/s\n";
        std::cout << "  Integrated Line Flux:   " << std::setprecision(2) << metrics.integrated_flux_k_kms << " K*km/s\n";
        std::cout << "  HI Column Density:      " << std::scientific << std::setprecision(2) << metrics.column_density_cm2 << " atoms/cm^2\n";
        std::cout << "  DC Spike Separation:    " << std::fixed << std::setprecision(1) << std::abs(metrics.lo_offset_khz)
                  << " kHz (" << metrics.bin_separation << " FFT bins)\n";
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "  Artifacts saved to: " << std::filesystem::absolute(config.output_dir).string() << "\n";
        std::cout << "    - 1_hi_spectrum_calibrated.png\n";
        std::cout << "    - 2_hi_raw_and_baseline.png\n";
        std::cout << "    - 3_hi_doppler_velocity.png\n";
        std::cout << "    - 4_hi_waterfall_time_frequency.png\n";
        std::cout << "    - 5_hi_master_dashboard.png\n";
        std::cout << "    - calibrated_spectrum.csv\n";
        std::cout << "    - analysis_report.txt\n";
        std::cout << "    - analysis_report.json\n";
        std::cout << "======================================================================\n\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nAnalysis Error: " << e.what() << "\n";
        return 1;
    }
}
