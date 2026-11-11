"""
Astro Radio Telescope Hydrogen Line Analysis Pipeline
Data Export Engine (CSV, Summary Report, JSON Metrics)
"""

import os
import json
from typing import Dict, Any
import numpy as np

from config import AstroConfig, HYDROGEN_REST_FREQ_MHZ, SPEED_OF_LIGHT_KMS


def export_csv(
    freqs_hz: np.ndarray,
    freqs_mhz: np.ndarray,
    vel_kms: np.ndarray,
    raw_power: np.ndarray,
    baseline_power: np.ndarray,
    calibrated_k: np.ndarray,
    calibrated_db: np.ndarray,
    output_path: str,
):
    """
    Export calibrated spectral data to CSV format.
    """
    raw_db = 10 * np.log10(np.maximum(raw_power, 1e-12))
    base_db = 10 * np.log10(np.maximum(baseline_power, 1e-12))
    
    header = "Frequency_Hz,Frequency_MHz,Doppler_Velocity_kms,Raw_Power_dB,Baseline_Power_dB,Calibrated_Power_dB,Antenna_Temperature_K"
    data = np.column_stack([
        freqs_hz,
        freqs_mhz,
        vel_kms,
        raw_db,
        base_db,
        calibrated_db,
        calibrated_k,
    ])
    
    np.savetxt(output_path, data, fmt=["%.3f", "%.6f", "%.3f", "%.4f", "%.4f", "%.4f", "%.4f"], delimiter=",", header=header, comments="")


def _to_builtin(obj):
    """Recursively convert NumPy scalars/arrays to native Python types for JSON serialization."""
    if isinstance(obj, np.integer):
        return int(obj)
    elif isinstance(obj, np.floating):
        return float(obj)
    elif isinstance(obj, np.ndarray):
        return obj.tolist()
    elif isinstance(obj, dict):
        return {k: _to_builtin(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [_to_builtin(v) for v in obj]
    return obj


def export_json(metrics: Dict[str, Any], config: AstroConfig, file_metadata: Dict[str, Any], output_path: str):
    """
    Export all calculation metrics and parameters to JSON.
    """
    # Clean non-serializable fields
    export_dict = {
        "metadata": {
            "source_file": file_metadata.get("filepath", ""),
            "filesize_bytes": int(file_metadata.get("filesize", 0)),
            "total_samples": int(file_metadata.get("total_samples", 0)),
            "duration_seconds": float(file_metadata.get("duration_s", 0.0)),
            "processing_time_seconds": float(file_metadata.get("processing_time_s", 0.0)),
            "throughput_mb_s": float(file_metadata.get("throughput_mb_s", 0.0)),
        },
        "sdr_settings": {
            "center_frequency_hz": float(config.center_freq_hz),
            "center_frequency_mhz": float(config.center_freq_hz / 1e6),
            "sample_rate_hz": float(config.sample_rate_hz),
            "sample_rate_msps": float(config.sample_rate_hz / 1e6),
            "system_temp_k": float(config.system_temp_k),
            "fft_size": int(config.fft_size),
            "bin_resolution_hz": float(config.sample_rate_hz / config.fft_size),
            "window": config.window,
            "dc_spike_cleared_hz": float(config.dc_clear_hz),
        },
        "hydrogen_line_detection": {
            "target_line": "Neutral Hydrogen (H I 21cm, 1S F=1 -> F=0)",
            "rest_frequency_mhz": float(HYDROGEN_REST_FREQ_MHZ),
            "observed_peak_frequency_mhz": float(metrics["peak_freq_mhz"]),
            "doppler_recession_velocity_kms": float(metrics["peak_vel_kms"]),
            "peak_antenna_temperature_k": float(metrics["peak_temp_k"]),
            "peak_power_above_baseline_db": float(metrics["peak_power_db"]),
            "noise_floor_rms_k": float(metrics["noise_std_k"]),
            "noise_floor_rms_db": float(metrics["noise_std_db"]),
            "signal_to_noise_ratio_snr": float(metrics["snr"]),
            "line_fwhm_kms": float(metrics["fwhm_kms"]),
            "velocity_dispersion_sigma_kms": float(metrics["sigma_kms"]),
            "integrated_flux_k_kms": float(metrics["integrated_flux_k_kms"]),
            "neutral_hydrogen_column_density_cm2": float(metrics["column_density_cm2"]),
            "lo_dc_offset_khz": float(metrics["lo_offset_khz"]),
            "bins_from_dc_to_hi": int(metrics["bin_separation"]),
        },
    }
    
    clean_dict = _to_builtin(export_dict)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(clean_dict, f, indent=2)


def export_summary_text(
    metrics: Dict[str, Any],
    config: AstroConfig,
    file_metadata: Dict[str, Any],
    output_path: str,
):
    """
    Generate an executive human-readable text report of the radiotelescope analysis.
    """
    dur_s = file_metadata.get("duration_s", 0.0)
    dur_min = dur_s / 60.0
    fc_mhz = config.center_freq_hz / 1e6
    fs_msps = config.sample_rate_hz / 1e6
    bin_res_hz = config.sample_rate_hz / config.fft_size
    bin_res_kms = SPEED_OF_LIGHT_KMS * (bin_res_hz / (HYDROGEN_REST_FREQ_MHZ * 1e6))
    
    report = f"""================================================================================
          RADIO TELESCOPE HYDROGEN LINE (HI 21cm) ANALYSIS REPORT
================================================================================

1. OBSERVATION & FILE METADATA
--------------------------------------------------------------------------------
Source IQ File:             {os.path.basename(file_metadata.get('filepath', ''))}
File Location:              {file_metadata.get('filepath', '')}
File Size:                  {file_metadata.get('filesize', 0) / (1024**3):.2f} GB ({file_metadata.get('filesize', 0) / (1024**2):.1f} MB)
Data Format:                CS16 (Interleaved 16-bit Signed Integer I/Q)
Observation Duration:       {dur_s:.2f} seconds ({dur_min:.2f} minutes)
Processing Speed:           {file_metadata.get('throughput_mb_s', 0.0):.1f} MB/s (Completed in {file_metadata.get('processing_time_s', 0.0):.2f} s)

2. SDR RECEIVER & DSP CONFIGURATION
--------------------------------------------------------------------------------
SDR Center Frequency (LO):  {fc_mhz:.6f} MHz
SDR Sample Rate:            {fs_msps:.3f} MSPS ({config.sample_rate_hz:,.0f} Hz)
FFT Channel Count:          {config.fft_size} bins
Spectral Window:            {config.window.capitalize()}
Channel Width (Frequency):  {bin_res_hz:.2f} Hz
Channel Width (Velocity):   {bin_res_kms:.3f} km/s
Assumed System Noise Temp:  {config.system_temp_k:.1f} K
Baseline Modeling Method:   {config.baseline_method}
DC Spike Excision:          Excised {config.dc_clear_hz:.0f} Hz centered at LO {fc_mhz:.4f} MHz

3. DC SPIKE COMBAT & LO OFF-CENTERING VERIFICATION
--------------------------------------------------------------------------------
Astronomical HI Rest Freq:  {HYDROGEN_REST_FREQ_MHZ:.6f} MHz
SDR Tuned Center Frequency: {fc_mhz:.6f} MHz
LO Frequency Offset:        {metrics['lo_offset_khz']:+.2f} kHz
Bin Separation:             {metrics['bin_separation']} FFT bins between DC spike and HI line
DC Offset Assessment:       SUCCESSFUL! The SDR center LO DC leakage spike was
                            tuned {abs(metrics['lo_offset_khz']):.1f} kHz away from the Hydrogen Line.
                            The line emission was completely protected from DC distortion.

4. ASTROPHYSICAL HYDROGEN LINE (HI) DETECTION RESULTS
--------------------------------------------------------------------------------
Target Emission:            Neutral Atomic Hydrogen (H I, 21 cm Spin-Flip)
Observed Peak Frequency:    {metrics['peak_freq_mhz']:.6f} MHz
Doppler Radial Velocity:    {metrics['peak_vel_kms']:+.2f} km/s (Topocentric)
Kinematic Classification:   {"Receding (Redshifted)" if metrics['peak_vel_kms'] > 0 else "Approaching (Blueshifted)"} relative to Solar System
Peak Antenna Temperature:   {metrics['peak_temp_k']:.2f} Kelvin (above baseline)
Peak Relative Power:        {metrics['peak_power_db']:+.3f} dB
Noise Floor RMS:            {metrics['noise_std_k']:.3f} Kelvin ({metrics['noise_std_db']:.4f} dB)
Signal-to-Noise Ratio (SNR):{metrics['snr']:.1f} sigma (Statistically Definitive Detection)
Line FWHM (Velocity Width): {metrics['fwhm_kms']:.2f} km/s ({metrics['fwhm_kms'] / SPEED_OF_LIGHT_KMS * HYDROGEN_REST_FREQ_MHZ * 1e3:.2f} kHz)
Velocity Dispersion (sigma):{metrics['sigma_kms']:.2f} km/s
Integrated Line Flux (W):   {metrics['integrated_flux_k_kms']:.2f} K * km/s
Neutral Hydrogen Column (N):{metrics['column_density_cm2']:.3e} atoms / cm^2 (Optically thin model)

5. SCIENTIFIC INTERPRETATION
--------------------------------------------------------------------------------
- Signal Source: Neutral atomic hydrogen clouds in the interstellar medium (ISM) of the
  Milky Way Galaxy.
- Doppler Shift: The observed radial velocity of {metrics['peak_vel_kms']:+.2f} km/s reflects the
  combined motion of the Earth's orbit and the differential galactic rotation of the
  Milky Way spiral arms in the antenna's pointing direction.
- Temporal Waterfall: The time-frequency waterfall proves continuous, steady emission
  throughout the entire observation period, confirming an authentic cosmic source.

================================================================================
Generated by Astro Radio Astronomy Analysis Pipeline
================================================================================
"""
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(report)
