#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>

#include "app_state.h"
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

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while(0)

#define ASSERT_TRUE(a, msg)  TEST_ASSERT((a), msg)
#define ASSERT_FALSE(a, msg) TEST_ASSERT(!(a), msg)
#define ASSERT_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)

int main() {
    using namespace astro;

    std::cout << "======================================================================\n";
    std::cout << "  RUNNING INTEGRATION TEST: test_end_to_end_bridge\n";
    std::cout << "  (SDR Record -> Bridge Switch -> Offline Scientific Analysis Pipeline)\n";
    std::cout << "======================================================================\n";

    AppState app_state;
    std::string test_dir = "tests/test_tmp_e2e";
    std::string output_dir = "tests/test_output_e2e";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::remove_all(output_dir, ec);
    std::filesystem::create_directories(test_dir, ec);
    std::filesystem::create_directories(output_dir, ec);

    std::string test_cs16_file = test_dir + "/e2e_survey.cs16";

    // -----------------------------------------------------------------------
    // STEP 1: INITIALIZE SDR IN RECORD MODE & START CAPTURE
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 1] Initializing SDR Controller in Record Mode (Virtual HI Simulator)...\n";
    ASSERT_EQ(app_state.get_section(), AppSection::Record, "State starts in Record section");

    SdrController sdr;
    Cs16Writer writer(test_dir);

    sdr.add_sample_callback([&writer](const uint8_t* data, size_t len) {
        writer.feed_samples(data, len);
    });

    bool sdr_init_ok = sdr.initialize(true, 0); // Force virtual simulator mode
    ASSERT_TRUE(sdr_init_ok, "Virtual SDR simulator initialized successfully");
    ASSERT_TRUE(sdr.is_simulated(), "SDR device is in simulated mode");

    uint64_t center_freq_hz = 1420500000ULL; // 1420.5 MHz LO
    uint32_t sample_rate_hz = 2400000;       // 2.4 MSPS
    float gain_db = 25.4f;

    sdr.request_frequency(center_freq_hz);
    sdr.request_sample_rate(sample_rate_hz);
    sdr.request_gain(gain_db, false);

    // Register active recording in AppState
    app_state.set_recording_active(true, test_cs16_file);
    ASSERT_TRUE(app_state.is_recording_active(), "AppState reports active recording");
    ASSERT_TRUE(app_state.is_file_actively_recording(test_cs16_file), "Collision check flags in-flight file");

    std::string recorded_path = writer.start_recording(
        sample_rate_hz, center_freq_hz, sdr.get_device_name(), gain_db, false, test_cs16_file, 3.0
    );
    std::cout << "  Recording started: " << recorded_path << "\n";

    sdr.start_streaming();
    ASSERT_TRUE(sdr.is_streaming(), "SDR is actively streaming");

    // Stream for 3 seconds to capture multi-megabyte CS16 data
    std::cout << "  Capturing simulated 21cm Hydrogen Line data (3.0s)...\n";
    auto start_time = std::chrono::steady_clock::now();
    while (writer.is_recording()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if (elapsed >= 3.1) break;
    }

    sdr.stop_streaming();
    writer.stop_recording();
    sdr.shutdown();

    // Release recording state in AppState
    app_state.set_recording_active(false, "");
    app_state.set_last_recorded_file(recorded_path);
    ASSERT_FALSE(app_state.is_recording_active(), "Recording is no longer active");
    ASSERT_FALSE(app_state.is_file_actively_recording(recorded_path), "File is no longer blocked from analysis");
    ASSERT_EQ(app_state.get_last_recorded_file(), recorded_path, "Last recorded file is registered");

    // -----------------------------------------------------------------------
    // STEP 2: VERIFY GENERATED BINARY & SIDECARS ON DISK
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 2] Verifying generated CS16 binary and metadata sidecars...\n";
    ASSERT_TRUE(std::filesystem::exists(recorded_path), "Recorded .cs16 file exists on disk");
    uint64_t file_size = std::filesystem::file_size(recorded_path);
    std::cout << "  CS16 file size: " << file_size << " bytes (" << (file_size / (1024.0 * 1024.0)) << " MB)\n";
    ASSERT_TRUE(file_size >= 1000000ULL, "CS16 file contains substantial data (>= 1MB)");

    std::string json_sidecar = recorded_path.substr(0, recorded_path.find_last_of('.')) + ".json";
    std::string txt_sidecar  = recorded_path.substr(0, recorded_path.find_last_of('.')) + ".txt";

    ASSERT_TRUE(std::filesystem::exists(json_sidecar), "JSON sidecar metadata exists");
    ASSERT_TRUE(std::filesystem::exists(txt_sidecar), "TXT sidecar metadata exists");

    // Verify sidecar contents
    std::ifstream json_ifs(json_sidecar);
    std::string json_str((std::istreambuf_iterator<char>(json_ifs)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(json_str.find("1420500000") != std::string::npos, "JSON sidecar contains correct center frequency");
    ASSERT_TRUE(json_str.find("2400000") != std::string::npos, "JSON sidecar contains correct sample rate");

    // -----------------------------------------------------------------------
    // STEP 3: TRIGGER RECORD -> ANALYZE BRIDGE
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 3] Triggering [ ⚡ Analyze This Recording Now ] Bridge...\n";
    app_state.switch_to_analyze(recorded_path, true);
    ASSERT_EQ(app_state.get_section(), AppSection::Analyze, "AppState successfully switched to Analyze section");

    std::string target_to_analyze;
    bool bridge_fired = app_state.check_and_clear_auto_analyze(target_to_analyze);
    ASSERT_TRUE(bridge_fired, "Bridge auto_analyze flag caught by Analyze section");
    ASSERT_EQ(target_to_analyze, recorded_path, "Target file for analysis matches recording path");

    // -----------------------------------------------------------------------
    // STEP 4: EXECUTE COMPLETE SCIENTIFIC ANALYSIS PIPELINE
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 4] Running Scientific Offline Analysis Pipeline...\n";

    ObservationMetadata sidecar = SidecarReader::parse(target_to_analyze);
    ASSERT_TRUE(sidecar.found_sidecar, "SidecarReader successfully detected metadata sidecar");
    ASSERT_EQ(sidecar.center_freq_hz, center_freq_hz, "Parsed center LO matches recording");
    ASSERT_EQ(sidecar.sample_rate_hz, sample_rate_hz, "Parsed sample rate matches recording");

    AstroConfig config;
    config.fft_size = 8192;
    config.system_temp_k = 100.0;
    config.output_dir = output_dir;
    config.baseline_method = BaselineMethod::RippleFilter;
    SidecarReader::apply_to_config(sidecar, config);

    // 4.1 Streaming accumulation
    StreamingProcessor processor(target_to_analyze, config);
    StreamingResults stream = processor.process(nullptr);
    std::cout << "  Accumulated " << stream.total_samples << " complex samples in "
              << stream.elapsed_processing_time_s << "s (" << stream.throughput_mb_s << " MB/s)\n";
    ASSERT_TRUE(stream.total_samples > 0, "Accumulated non-zero samples");
    ASSERT_EQ(stream.averaged_power.size(), static_cast<size_t>(config.fft_size), "Spectrum matches FFT size");

    // 4.2 DC Notch Excision
    DCExcisionInfo dc_info;
    std::vector<double> dc_clean = DCExcision::excise(stream.averaged_power, config, dc_info);
    ASSERT_TRUE(dc_info.dc_bin > 0, "DC excision detected center bin");

    // 4.3 RFI Mitigation
    int num_rfi = 0;
    std::vector<double> rfi_clean = RFIFilter::mitigate(dc_clean, stream.freq_mhz, config, num_rfi);

    // 4.4 Baseline Modeling
    BaselineResults baseline = BaselineFitter::fit(rfi_clean, stream.freq_mhz, config);
    ASSERT_EQ(baseline.baseline_power.size(), static_cast<size_t>(config.fft_size), "Baseline fitted successfully");

    // 4.5 Scientific Calibration
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

    // 4.6 Astrophysical Metrics & Gaussian Curve Fit
    AstrophysicalMetrics metrics = Astrophysics::calculate_metrics(
        stream.freq_mhz, v_kms, cal_k, cal_db, config
    );

    // 4.7 Publication-Grade PNG & CSV Export
    FileMetadata meta;
    meta.filepath = target_to_analyze;
    meta.filesize = stream.file_size_bytes;
    meta.total_samples = stream.total_samples;
    meta.duration_s = stream.total_duration_s;
    meta.processing_time_s = stream.elapsed_processing_time_s;
    meta.throughput_mb_s = stream.throughput_mb_s;

    bool export_ok = Exporter::export_all_artifacts(
        config.output_dir, target_to_analyze, config, meta, stream,
        rfi_clean, baseline.baseline_power, cal_k, cal_db, metrics
    );
    ASSERT_TRUE(export_ok, "Exporter successfully rendered all artifacts");

    // -----------------------------------------------------------------------
    // STEP 5: VERIFY 21CM HYDROGEN LINE ASTROPHYSICAL PARAMETERS
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 5] Validating Extracted 21cm Astrophysical Metrics...\n";
    std::cout << std::fixed << std::setprecision(4)
              << "  Observed Peak Freq:     " << metrics.peak_freq_mhz << " MHz\n"
              << std::setprecision(2)
              << "  Doppler Radial Velocity:" << metrics.peak_vel_kms << " km/s\n"
              << "  Antenna Temperature T_A:" << metrics.peak_temp_k << " K\n"
              << "  Peak Power (dB):        " << metrics.peak_power_db << " dB\n"
              << "  Detection SNR:          " << metrics.snr << " sigma\n"
              << "  Line FWHM:              " << metrics.fwhm_kms << " km/s\n"
              << "  Integrated Flux:        " << metrics.integrated_flux_k_kms << " K*km/s\n"
              << std::scientific
              << "  H I Column Density:     " << metrics.column_density_cm2 << " cm^-2\n"
              << std::fixed;

    // Peak frequency should be near rest frequency 1420.40575 MHz
    ASSERT_TRUE(std::abs(metrics.peak_freq_mhz - 1420.4058) < 0.15,
                "Observed peak frequency matches 21cm Hydrogen Line within tolerance");

    // Antenna temperature should be positive
    ASSERT_TRUE(metrics.peak_temp_k > 0.0, "Antenna temperature T_A is positive");

    // Doppler velocity should be physically reasonable (|v| < 100 km/s for local HI)
    ASSERT_TRUE(std::abs(metrics.peak_vel_kms) < 100.0, "Doppler velocity is within physical galactic envelope");

    // SNR should demonstrate unambiguous detection
    ASSERT_TRUE(metrics.snr >= 1.5, "Detection SNR indicates statistically significant detection (>= 1.5 sigma)");

    // Integrated flux must be positive
    ASSERT_TRUE(metrics.integrated_flux_k_kms > 0.0, "Integrated flux is positive");

    // Column density must be in astrophysical range (e.g. 10^18 to 10^22 cm^-2)
    ASSERT_TRUE(metrics.column_density_cm2 > 1e18, "H I column density is in valid interstellar range");

    // -----------------------------------------------------------------------
    // STEP 6: VERIFY ALL 8 GENERATED ARTIFACT FILES
    // -----------------------------------------------------------------------
    std::cout << "\n[Step 6] Verifying all generated scientific artifact files on disk...\n";
    std::vector<std::string> expected_artifacts = {
        output_dir + "/1_hi_spectrum_calibrated.png",
        output_dir + "/2_hi_raw_and_baseline.png",
        output_dir + "/3_hi_doppler_velocity.png",
        output_dir + "/4_hi_waterfall_time_frequency.png",
        output_dir + "/5_hi_master_dashboard.png",
        output_dir + "/calibrated_spectrum.csv",
        output_dir + "/analysis_report.json",
        output_dir + "/analysis_report.txt"
    };

    for (const auto& art : expected_artifacts) {
        bool exists = std::filesystem::exists(art);
        uint64_t sz = exists ? std::filesystem::file_size(art) : 0;
        std::cout << "  Artifact: " << art << " [" << (exists ? "OK" : "MISSING") << ", " << sz << " bytes]\n";
        ASSERT_TRUE(exists && sz > 0, "Artifact exists and has non-zero size: " + art);
    }

    // Cleanup test artifacts
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::remove_all(output_dir, ec);

    std::cout << "\n----------------------------------------------------------------------\n";
    std::cout << "RESULTS: " << g_tests_passed << " / " << g_tests_run << " assertions PASSED.\n";
    if (g_tests_passed == g_tests_run) {
        std::cout << "[SUCCESS] test_end_to_end_bridge PASSED (100%)\n";
        return 0;
    } else {
        std::cerr << "[FAILURE] " << (g_tests_run - g_tests_passed) << " assertions FAILED!\n";
        return 1;
    }
}
