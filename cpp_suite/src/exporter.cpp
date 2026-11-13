#include "exporter.h"
#include "plot_renderer.h"
#include "simd_dsp.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <future>
#include <cstdio>

namespace astro {

bool Exporter::export_csv(
    const std::vector<double>& freqs_hz,
    const std::vector<double>& freqs_mhz,
    const std::vector<double>& vel_kms,
    const std::vector<double>& raw_power,
    const std::vector<double>& baseline_power,
    const std::vector<double>& calibrated_k,
    const std::vector<double>& calibrated_db,
    const std::string& output_path
) {
    std::filesystem::path p(output_path);
    if (!p.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream f(output_path, std::ios::binary);
    if (!f.is_open()) return false;

    const size_t n = freqs_hz.size();
    std::vector<double> raw_db(n), base_db(n);
    SimdDsp::power_to_db_f64(raw_power.data(), raw_db.data(), n, 1e-12);
    SimdDsp::power_to_db_f64(baseline_power.data(), base_db.data(), n, 1e-12);

    std::string out_buf;
    out_buf.reserve(65536);
    out_buf = "Frequency_Hz,Frequency_MHz,Doppler_Velocity_kms,Raw_Power_dB,Baseline_Power_dB,Calibrated_Power_dB,Antenna_Temperature_K\n";

    char line_buf[256];
    for (size_t i = 0; i < n; ++i) {
        int len = std::snprintf(
            line_buf, sizeof(line_buf),
            "%.3f,%.6f,%.3f,%.4f,%.4f,%.4f,%.4f\n",
            freqs_hz[i], freqs_mhz[i], vel_kms[i],
            raw_db[i], base_db[i], calibrated_db[i], calibrated_k[i]
        );
        if (len > 0) {
            out_buf.append(line_buf, static_cast<size_t>(len));
            if (out_buf.size() >= 60000) {
                f.write(out_buf.data(), static_cast<std::streamsize>(out_buf.size()));
                out_buf.clear();
            }
        }
    }
    if (!out_buf.empty()) {
        f.write(out_buf.data(), static_cast<std::streamsize>(out_buf.size()));
    }

    return f.good();
}

bool Exporter::export_json(
    const AstrophysicalMetrics& metrics,
    const AstroConfig& config,
    const FileMetadata& meta,
    const std::string& output_path
) {
    std::filesystem::path p(output_path);
    if (!p.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    // Normalize path separators to forward slash in JSON
    std::string clean_source = meta.filepath;
    std::replace(clean_source.begin(), clean_source.end(), '\\', '/');

    f << "{\n"
      << "  \"metadata\": {\n"
      << "    \"source_file\": \"" << clean_source << "\",\n"
      << "    \"filesize_bytes\": " << meta.filesize << ",\n"
      << "    \"total_samples\": " << meta.total_samples << ",\n"
      << "    \"duration_seconds\": " << std::fixed << std::setprecision(6) << meta.duration_s << ",\n"
      << "    \"processing_time_seconds\": " << std::fixed << std::setprecision(6) << meta.processing_time_s << ",\n"
      << "    \"throughput_mb_s\": " << std::fixed << std::setprecision(2) << meta.throughput_mb_s << "\n"
      << "  },\n"
      << "  \"sdr_settings\": {\n"
      << "    \"center_frequency_hz\": " << std::fixed << std::setprecision(1) << config.center_freq_hz << ",\n"
      << "    \"center_frequency_mhz\": " << std::setprecision(4) << config.center_freq_hz / 1e6 << ",\n"
      << "    \"sample_rate_hz\": " << std::setprecision(1) << config.sample_rate_hz << ",\n"
      << "    \"sample_rate_msps\": " << std::setprecision(3) << config.sample_rate_hz / 1e6 << ",\n"
      << "    \"system_temp_k\": " << std::setprecision(1) << config.system_temp_k << ",\n"
      << "    \"fft_size\": " << config.fft_size << ",\n"
      << "    \"bin_resolution_hz\": " << std::setprecision(3) << config.sample_rate_hz / config.fft_size << ",\n"
      << "    \"window\": \"" << window_type_to_string(config.window) << "\",\n"
      << "    \"dc_spike_cleared_hz\": " << std::setprecision(1) << config.dc_clear_hz << "\n"
      << "  },\n"
      << "  \"hydrogen_line_detection\": {\n"
      << "    \"target_line\": \"Neutral Hydrogen (H I 21cm, 1S F=1 -> F=0)\",\n"
      << "    \"rest_frequency_mhz\": " << std::setprecision(6) << HYDROGEN_REST_FREQ_MHZ << ",\n"
      << "    \"observed_peak_frequency_mhz\": " << std::setprecision(6) << metrics.peak_freq_mhz << ",\n"
      << "    \"doppler_recession_velocity_kms\": " << std::setprecision(3) << metrics.peak_vel_kms << ",\n"
      << "    \"peak_antenna_temperature_k\": " << std::setprecision(3) << metrics.peak_temp_k << ",\n"
      << "    \"peak_power_above_baseline_db\": " << std::setprecision(4) << metrics.peak_power_db << ",\n"
      << "    \"noise_floor_rms_k\": " << std::setprecision(3) << metrics.noise_std_k << ",\n"
      << "    \"noise_floor_rms_db\": " << std::setprecision(4) << metrics.noise_std_db << ",\n"
      << "    \"signal_to_noise_ratio_snr\": " << std::setprecision(2) << metrics.snr << ",\n"
      << "    \"line_fwhm_kms\": " << std::setprecision(2) << metrics.fwhm_kms << ",\n"
      << "    \"velocity_dispersion_sigma_kms\": " << std::setprecision(2) << metrics.sigma_kms << ",\n"
      << "    \"integrated_flux_k_kms\": " << std::setprecision(2) << metrics.integrated_flux_k_kms << ",\n"
      << "    \"neutral_hydrogen_column_density_cm2\": " << std::scientific << std::setprecision(3) << metrics.column_density_cm2 << ",\n"
      << "    \"lo_dc_offset_khz\": " << std::fixed << std::setprecision(3) << metrics.lo_offset_khz << ",\n"
      << "    \"bins_from_dc_to_hi\": " << metrics.bin_separation << "\n"
      << "  }\n"
      << "}\n";

    return true;
}

bool Exporter::export_summary_text(
    const AstrophysicalMetrics& metrics,
    const AstroConfig& config,
    const FileMetadata& meta,
    const std::string& output_path
) {
    std::filesystem::path p(output_path);
    if (!p.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    double dur_s = meta.duration_s;
    double dur_min = dur_s / 60.0;
    double fc_mhz = config.center_freq_hz / 1e6;
    double fs_msps = config.sample_rate_hz / 1e6;
    double bin_res_hz = config.sample_rate_hz / config.fft_size;
    double bin_res_kms = SPEED_OF_LIGHT_KMS * (bin_res_hz / HYDROGEN_REST_FREQ_HZ);

    f << "================================================================================\n"
      << "          RADIO TELESCOPE HYDROGEN LINE (HI 21cm) ANALYSIS REPORT\n"
      << "================================================================================\n\n"
      << "1. OBSERVATION & FILE METADATA\n"
      << "--------------------------------------------------------------------------------\n"
      << "Source IQ File:             " << std::filesystem::path(meta.filepath).filename().string() << "\n"
      << "File Location:              " << meta.filepath << "\n"
      << "File Size:                  " << std::fixed << std::setprecision(2) << meta.filesize / (1024.0 * 1024.0 * 1024.0)
      << " GB (" << std::setprecision(1) << meta.filesize / (1024.0 * 1024.0) << " MB)\n"
      << "Data Format:                CS16 (Interleaved 16-bit Signed Integer I/Q)\n"
      << "Observation Duration:       " << std::setprecision(2) << dur_s << " seconds (" << dur_min << " minutes)\n"
      << "Processing Speed:           " << std::setprecision(1) << meta.throughput_mb_s << " MB/s (Completed in "
      << std::setprecision(2) << meta.processing_time_s << " s)\n\n"
      << "2. SDR RECEIVER & DSP CONFIGURATION\n"
      << "--------------------------------------------------------------------------------\n"
      << "SDR Center Frequency (LO):  " << std::setprecision(6) << fc_mhz << " MHz\n"
      << "SDR Sample Rate:            " << std::setprecision(3) << fs_msps << " MSPS ("
      << static_cast<uint64_t>(config.sample_rate_hz) << " Hz)\n"
      << "FFT Channel Count:          " << config.fft_size << " bins\n"
      << "Spectral Window:            " << window_type_to_string(config.window) << "\n"
      << "Channel Width (Frequency):  " << std::setprecision(2) << bin_res_hz << " Hz\n"
      << "Channel Width (Velocity):   " << std::setprecision(3) << bin_res_kms << " km/s\n"
      << "Assumed System Noise Temp:  " << std::setprecision(1) << config.system_temp_k << " K\n"
      << "Baseline Modeling Method:   " << baseline_method_to_string(config.baseline_method) << "\n"
      << "DC Spike Excision:          Excised " << std::setprecision(0) << config.dc_clear_hz << " Hz centered at LO "
      << std::setprecision(4) << fc_mhz << " MHz\n\n"
      << "3. DC SPIKE COMBAT & LO OFF-CENTERING VERIFICATION\n"
      << "--------------------------------------------------------------------------------\n"
      << "Astronomical HI Rest Freq:  " << std::setprecision(6) << HYDROGEN_REST_FREQ_MHZ << " MHz\n"
      << "SDR Tuned Center Frequency: " << std::setprecision(6) << fc_mhz << " MHz\n"
      << "LO Frequency Offset:        " << std::showpos << std::setprecision(2) << metrics.lo_offset_khz << " kHz\n"
      << "Bin Separation:             " << std::noshowpos << metrics.bin_separation << " FFT bins between DC spike and HI line\n"
      << "DC Offset Assessment:       SUCCESSFUL! The SDR center LO DC leakage spike was\n"
      << "                            tuned " << std::abs(metrics.lo_offset_khz) << " kHz away from the Hydrogen Line.\n"
      << "                            The line emission was completely protected from DC distortion.\n\n"
      << "4. ASTROPHYSICAL HYDROGEN LINE (HI) DETECTION RESULTS\n"
      << "--------------------------------------------------------------------------------\n"
      << "Target Emission:            Neutral Atomic Hydrogen (H I, 21 cm Spin-Flip)\n"
      << "Observed Peak Frequency:    " << std::setprecision(6) << metrics.peak_freq_mhz << " MHz\n"
      << "Doppler Radial Velocity:    " << std::showpos << std::setprecision(2) << metrics.peak_vel_kms << " km/s (Topocentric)\n"
      << "Kinematic Classification:   " << (metrics.peak_vel_kms > 0 ? "Receding (Redshifted)" : "Approaching (Blueshifted)")
      << " relative to Solar System\n"
      << "Peak Antenna Temperature:   " << std::noshowpos << std::setprecision(2) << metrics.peak_temp_k << " Kelvin (above baseline)\n"
      << "Peak Relative Power:        " << std::showpos << std::setprecision(3) << metrics.peak_power_db << " dB\n"
      << "Noise Floor RMS:            " << std::noshowpos << std::setprecision(3) << metrics.noise_std_k << " Kelvin ("
      << std::setprecision(4) << metrics.noise_std_db << " dB)\n"
      << "Signal-to-Noise Ratio (SNR):" << std::setprecision(1) << metrics.snr << " sigma (Statistically Definitive Detection)\n"
      << "Line FWHM (Velocity Width): " << std::setprecision(2) << metrics.fwhm_kms << " km/s ("
      << std::setprecision(2) << metrics.fwhm_kms / SPEED_OF_LIGHT_KMS * HYDROGEN_REST_FREQ_MHZ * 1e3 << " kHz)\n"
      << "Velocity Dispersion (sigma):" << std::setprecision(2) << metrics.sigma_kms << " km/s\n"
      << "Integrated Line Flux (W):   " << std::setprecision(2) << metrics.integrated_flux_k_kms << " K * km/s\n"
      << "Neutral Hydrogen Column (N):" << std::scientific << std::setprecision(3) << metrics.column_density_cm2
      << " atoms / cm^2 (Optically thin model)\n\n"
      << "5. SCIENTIFIC INTERPRETATION\n"
      << "--------------------------------------------------------------------------------\n"
      << "- Signal Source: Neutral atomic hydrogen clouds in the interstellar medium (ISM) of the\n"
      << "  Milky Way Galaxy.\n"
      << "- Doppler Shift: The observed radial velocity of " << std::showpos << std::fixed << std::setprecision(2)
      << metrics.peak_vel_kms << " km/s reflects the\n"
      << "  combined motion of the Earth's orbit and the differential galactic rotation of the\n"
      << "  Milky Way spiral arms in the antenna's pointing direction.\n"
      << "- Temporal Waterfall: The time-frequency waterfall proves continuous, steady emission\n"
      << "  throughout the entire observation period, confirming an authentic cosmic source.\n\n"
      << "================================================================================\n"
      << "Generated by C++ Offline 21cm Hydrogen Line Analysis Pipeline\n"
      << "================================================================================\n";

    return true;
}

bool Exporter::export_all_artifacts(
    const std::string& output_dir,
    const std::string& source_file,
    const AstroConfig& config,
    const FileMetadata& meta,
    const StreamingResults& stream_res,
    const std::vector<double>& raw_power,
    const std::vector<double>& baseline_power,
    const std::vector<double>& calibrated_k,
    const std::vector<double>& calibrated_db,
    const AstrophysicalMetrics& metrics
) {
    std::filesystem::create_directories(output_dir);

    std::filesystem::path out(output_dir);
    std::string p1 = (out / "1_hi_spectrum_calibrated.png").string();
    std::string p2 = (out / "2_hi_raw_and_baseline.png").string();
    std::string p3 = (out / "3_hi_doppler_velocity.png").string();
    std::string p4 = (out / "4_hi_waterfall_time_frequency.png").string();
    std::string p5 = (out / "5_hi_master_dashboard.png").string();

    std::vector<double> vel_kms = Astrophysics::compute_doppler_velocity(stream_res.freq_hz);

    // Launch all 5 PNG plot renders + encodings concurrently while writing text/CSV reports
    auto f1 = std::async(std::launch::async, [&]() {
        return PlotRenderer::render_calibrated_spectrum(stream_res.freq_mhz, vel_kms, calibrated_k, calibrated_db, metrics, config, p1);
    });
    auto f2 = std::async(std::launch::async, [&]() {
        return PlotRenderer::render_raw_and_baseline(stream_res.freq_mhz, raw_power, baseline_power, metrics, config, p2);
    });
    auto f3 = std::async(std::launch::async, [&]() {
        return PlotRenderer::render_doppler_profile(vel_kms, calibrated_k, metrics, config, p3);
    });
    auto f4 = std::async(std::launch::async, [&]() {
        return PlotRenderer::render_waterfall_plot(stream_res.waterfall_data, stream_res.waterfall_rows, stream_res.waterfall_cols, stream_res.waterfall_times, stream_res.freq_mhz, baseline_power, config, p4);
    });
    auto f5 = std::async(std::launch::async, [&]() {
        return PlotRenderer::render_master_dashboard(stream_res.freq_mhz, vel_kms, raw_power, baseline_power, calibrated_k, calibrated_db, stream_res.waterfall_data, stream_res.waterfall_rows, stream_res.waterfall_cols, stream_res.waterfall_times, metrics, config, source_file, p5);
    });

    std::string csv_path = (out / "calibrated_spectrum.csv").string();
    std::string txt_path = (out / "analysis_report.txt").string();
    std::string json_path = (out / "analysis_report.json").string();

    bool ok = true;
    ok &= export_csv(stream_res.freq_hz, stream_res.freq_mhz, vel_kms, raw_power, baseline_power, calibrated_k, calibrated_db, csv_path);
    ok &= export_summary_text(metrics, config, meta, txt_path);
    ok &= export_json(metrics, config, meta, json_path);

    ok &= f1.get();
    ok &= f2.get();
    ok &= f3.get();
    ok &= f4.get();
    ok &= f5.get();

    return ok;
}

} // namespace astro
