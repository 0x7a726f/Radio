#!/usr/bin/env python3
"""
Astro Radio Telescope Hydrogen Line Analysis Pipeline
Main Executable and CLI/Interactive Wizard
"""

import os
import sys
import argparse
import glob
import time
from typing import List, Optional, Tuple

import numpy as np

# Ensure standard encoding safety on Windows
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# Add local directory to path for imports
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from config import AstroConfig, HYDROGEN_REST_FREQ_MHZ
from dsp import (
    StreamingProcessor,
    excise_dc_spike,
    mitigate_rfi_spikes,
    fit_baseline,
    refine_local_baseline,
    compute_doppler_velocity,
    calculate_hi_metrics,
)
from visualizer import (
    plot_calibrated_spectrum,
    plot_raw_and_baseline,
    plot_doppler_profile,
    plot_waterfall,
    plot_master_dashboard,
)
from exporter import export_csv, export_json, export_summary_text


def parse_freq_or_rate(val_str: str, default_is_mhz: bool = True) -> float:
    """
    Parse flexible user input for frequency or sample rate.
    Handles '1420.5', '1420.5MHz', '1420500000', '2.4', '2.4MSPS', '2400000'.
    """
    cleaned = val_str.strip().lower()
    for suffix in ["mhz", "msps", "m"]:
        if cleaned.endswith(suffix):
            cleaned = cleaned[: -len(suffix)].strip()
            return float(cleaned) * 1e6
    for suffix in ["khz", "ksps", "k"]:
        if cleaned.endswith(suffix):
            cleaned = cleaned[: -len(suffix)].strip()
            return float(cleaned) * 1e3
    for suffix in ["hz", "sps"]:
        if cleaned.endswith(suffix):
            cleaned = cleaned[: -len(suffix)].strip()
            return float(cleaned)

    val = float(cleaned)
    # If the user entered e.g. 1420.5 or 2.4, it's in MHz
    if default_is_mhz and val < 10000.0:
        return val * 1e6
    return val


def find_cs16_files(parent_dir: str) -> List[str]:
    """
    Discover all .cs16 files in the given directory and its immediate subdirectories.
    """
    found = []
    # Check top-level
    top_matches = glob.glob(os.path.join(parent_dir, "*.cs16"))
    found.extend(top_matches)
    
    # Check subdirectories
    sub_matches = glob.glob(os.path.join(parent_dir, "*", "*.cs16"))
    found.extend(sub_matches)
    
    # Check 2 levels deep (e.g. SatDump/Astro/*.cs16)
    sub2_matches = glob.glob(os.path.join(parent_dir, "*", "*", "*.cs16"))
    found.extend(sub2_matches)
    
    # Deduplicate and sort by modification time (most recent first)
    unique_files = list(set(found))
    unique_files.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return unique_files


def interactive_wizard() -> Tuple[str, AstroConfig]:
    """
    Interactive prompt wizard when running without CLI arguments.
    """
    print("\n" + "=" * 70)
    print("      ASTRO - RADIO TELESCOPE HYDROGEN LINE ANALYSIS WIZARD")
    print("=" * 70)
    
    # Search for cs16 files in parent directory (E:\Code\Radio)
    parent_dir = os.path.abspath(os.path.join(current_dir, ".."))
    discovered = find_cs16_files(parent_dir)
    
    chosen_file = ""
    if discovered:
        print("\nDiscovered .cs16 files in workspace:")
        for idx, fpath in enumerate(discovered[:6], 1):
            sz_gb = os.path.getsize(fpath) / (1024**3)
            rel_path = os.path.relpath(fpath, parent_dir)
            print(f"  [{idx}] {rel_path} ({sz_gb:.2f} GB)")
        print(f"  [{len(discovered[:6]) + 1}] Enter a custom path manually...")
        
        choice = input(f"\nSelect a file [1-{len(discovered[:6]) + 1}] (default: 1): ").strip()
        if not choice or choice == "1":
            chosen_file = discovered[0]
        else:
            try:
                c_idx = int(choice)
                if 1 <= c_idx <= len(discovered[:6]):
                    chosen_file = discovered[c_idx - 1]
                else:
                    chosen_file = input("Enter full path to .cs16 file: ").strip().strip('"').strip("'")
            except ValueError:
                chosen_file = choice.strip('"').strip("'")
    else:
        chosen_file = input("Enter path to your .cs16 file: ").strip().strip('"').strip("'")

    if not os.path.exists(chosen_file):
        print(f"Error: File '{chosen_file}' does not exist.")
        sys.exit(1)

    print(f"\nTarget File Selected: {chosen_file}")
    
    # Center frequency prompt (combats DC spike)
    print("\n--- Center Frequency (Combats SDR DC Spikes) ---")
    print(f"Astronomical HI Rest Frequency is: {HYDROGEN_REST_FREQ_MHZ:.4f} MHz")
    print("Tip: If you tuned off-center (e.g. 1420.5 MHz or 1420.8 MHz), enter your exact LO frequency.")
    fc_input = input("Enter SDR Center Frequency in MHz [default: 1420.5]: ").strip()
    center_freq_hz = parse_freq_or_rate(fc_input, default_is_mhz=True) if fc_input else 1420.5e6

    # Sample rate prompt
    fs_input = input("\nEnter SDR Sample Rate in MSPS [default: 2.4]: ").strip()
    sample_rate_hz = parse_freq_or_rate(fs_input, default_is_mhz=True) if fs_input else 2.4e6

    # Duration prompt
    dur_input = input("\nDuration to analyze in seconds [press Enter for full observation]: ").strip()
    max_duration_s = float(dur_input) if dur_input else None

    # Baseline method prompt
    print("\nBaseline Estimation Methods:")
    print("  [1] ripple_filter (Recommended: smoothed baseline with protected HI line, handles filter ripples)")
    print("  [2] als           (Asymmetric Least Squares / Whittaker smoothing)")
    print("  [3] poly          (High-order polynomial fit)")
    bl_input = input("Select baseline method [1-3] (default: 1): ").strip()
    if bl_input == "2":
        baseline_method = "als"
    elif bl_input == "3":
        baseline_method = "poly"
    else:
        baseline_method = "ripple_filter"

    output_dir = os.path.join(current_dir, "output")
    config = AstroConfig(
        center_freq_hz=center_freq_hz,
        sample_rate_hz=sample_rate_hz,
        baseline_method=baseline_method,
        max_duration_s=max_duration_s,
        output_dir=output_dir,
    )
    return chosen_file, config


def run_pipeline(filepath: str, config: AstroConfig):
    """
    Execute the end-to-end radio telescope signal analysis pipeline.
    """
    # Create output directory
    os.makedirs(config.output_dir, exist_ok=True)
    
    print("\n" + "=" * 70)
    print("  STARTING ASTRO HYDROGEN LINE ANALYSIS")
    print("=" * 70)
    print(f"  Input File:         {filepath}")
    print(f"  Output Directory:   {config.output_dir}")
    print(f"  SDR Center LO:      {config.center_freq_hz / 1e6:.6f} MHz")
    print(f"  SDR Sample Rate:    {config.sample_rate_hz / 1e6:.3f} MSPS")
    print(f"  HI Rest Frequency:  {HYDROGEN_REST_FREQ_MHZ:.6f} MHz")
    print(f"  Baseline Method:    {config.baseline_method}")
    print("-" * 70)

    # 1. Stream IQ data and compute FFT accumulation & waterfall
    processor = StreamingProcessor(filepath, config)
    results = processor.process()

    raw_power = results["averaged_power"]
    waterfall = results["waterfall"]
    waterfall_times = results["waterfall_times"]
    freqs_hz = results["freq_hz"]
    freqs_mhz = results["freq_mhz"]

    # 2. Excise DC spike at 0 Hz IF (center frequency)
    print("  [1/5] Excising SDR center LO DC offset spike...")
    cleaned_power, dc_info = excise_dc_spike(raw_power, config)
    print(f"        -> DC spike cleared ({dc_info['cleared_bandwidth_khz']:.1f} kHz band at {dc_info['dc_center_freq_mhz']:.4f} MHz)")

    # 3. Mitigate sharp RFI comb spikes
    print("  [2/5] Scanning for narrow-band RFI comb lines outside HI window...")
    rfi_cleaned_power, n_rfi = mitigate_rfi_spikes(cleaned_power, freqs_mhz, config)
    if n_rfi > 0:
        print(f"        -> Neutralized {n_rfi} active RFI spikes via local median interpolation")
    else:
        print("        -> Clean spectrum: no significant RFI spikes detected")

    # 4. Model instrumental baseline
    print(f"  [3/5] Estimating instrumental bandpass baseline ({config.baseline_method})...")
    baseline_power = fit_baseline(rfi_cleaned_power, freqs_mhz, config)

    # 5. Calibrate spectrum
    print("  [4/5] Computing calibrated antenna temperature and Doppler kinematics...")
    base_safe = np.maximum(baseline_power, 1e-12)
    # Stage 1: Broad bandpass calibration
    calibrated_k_raw = config.system_temp_k * (rfi_cleaned_power - baseline_power) / base_safe
    calibrated_db_raw = 10.0 * np.log10(np.maximum(rfi_cleaned_power / base_safe, 1e-6))
    vel_kms = compute_doppler_velocity(freqs_hz)

    # Stage 2: Local baseline zeroing on line-free reference channels
    calibrated_k, calibrated_db, _ = refine_local_baseline(vel_kms, calibrated_k_raw, calibrated_db_raw, config)
    metrics = calculate_hi_metrics(freqs_mhz, vel_kms, calibrated_k, calibrated_db, config)

    file_meta = {
        "filepath": filepath,
        "filesize": processor.filesize,
        "total_samples": processor.total_samples,
        "duration_s": results["total_duration_s"],
        "processing_time_s": results["elapsed_processing_time_s"],
        "throughput_mb_s": (processor.bytes_to_process / (1024**2)) / results["elapsed_processing_time_s"],
    }

    # 6. Generate Publication Figures and Data Exports
    print("  [5/5] Generating publication-quality figures and data exports...")
    p1 = os.path.join(config.output_dir, "1_hi_spectrum_calibrated.png")
    p2 = os.path.join(config.output_dir, "2_hi_raw_and_baseline.png")
    p3 = os.path.join(config.output_dir, "3_hi_doppler_velocity.png")
    p4 = os.path.join(config.output_dir, "4_hi_waterfall_time_frequency.png")
    p5 = os.path.join(config.output_dir, "5_hi_master_dashboard.png")

    plot_calibrated_spectrum(freqs_mhz, vel_kms, calibrated_k, calibrated_db, metrics, config, p1)
    plot_raw_and_baseline(freqs_mhz, raw_power, baseline_power, metrics, config, p2)
    plot_doppler_profile(vel_kms, calibrated_k, metrics, config, p3)
    plot_waterfall(waterfall, waterfall_times, freqs_mhz, baseline_power, config, p4)
    plot_master_dashboard(freqs_mhz, vel_kms, raw_power, baseline_power, calibrated_k, calibrated_db,
                          waterfall, waterfall_times, metrics, config, filepath, p5)

    csv_path = os.path.join(config.output_dir, "calibrated_spectrum.csv")
    txt_path = os.path.join(config.output_dir, "analysis_report.txt")
    json_path = os.path.join(config.output_dir, "analysis_report.json")

    export_csv(freqs_hz, freqs_mhz, vel_kms, raw_power, baseline_power, calibrated_k, calibrated_db, csv_path)
    export_summary_text(metrics, config, file_meta, txt_path)
    export_json(metrics, config, file_meta, json_path)

    # Print final summary banner
    print("\n" + "=" * 70)
    print("           ASTRO HYDROGEN LINE ANALYSIS COMPLETE")
    print("=" * 70)
    print(f"  Target Line:            Neutral Hydrogen (H I, 21 cm)")
    print(f"  Rest Frequency:         {HYDROGEN_REST_FREQ_MHZ:.6f} MHz")
    print(f"  Observed Peak:          {metrics['peak_freq_mhz']:.6f} MHz")
    print(f"  Doppler Velocity:       {metrics['peak_vel_kms']:+.2f} km/s (Topocentric)")
    print(f"  Peak Brightness:        {metrics['peak_temp_k']:.2f} K  ({metrics['peak_power_db']:+.3f} dB)")
    print(f"  Noise Floor RMS:        {metrics['noise_std_k']:.3f} K")
    print(f"  Signal-to-Noise Ratio:  {metrics['snr']:.1f} sigma")
    print(f"  Line Width (FWHM):      {metrics['fwhm_kms']:.2f} km/s")
    print(f"  Integrated Line Flux:   {metrics['integrated_flux_k_kms']:.2f} K*km/s")
    print(f"  HI Column Density:      {metrics['column_density_cm2']:.2e} atoms/cm^2")
    print(f"  DC Spike Separation:    {abs(metrics['lo_offset_khz']):.1f} kHz ({metrics['bin_separation']} FFT bins)")
    print("-" * 70)
    print("  Generated Artifacts:")
    print(f"    * Calibrated HI Spectrum:   {os.path.basename(p1)}")
    print(f"    * Raw & Baseline Bandpass:  {os.path.basename(p2)}")
    print(f"    * Doppler Velocity Profile: {os.path.basename(p3)}")
    print(f"    * Time-Frequency Waterfall: {os.path.basename(p4)}")
    print(f"    * Master Analysis Board:    {os.path.basename(p5)}")
    print(f"    * Calibrated Data CSV:      {os.path.basename(csv_path)}")
    print(f"    * Executive Report TXT:     {os.path.basename(txt_path)}")
    print(f"    * Structured JSON Metrics:  {os.path.basename(json_path)}")
    print(f"  All outputs saved to: {os.path.abspath(config.output_dir)}")
    print("=" * 70 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Astro: High-Speed Radio Telescope Hydrogen Line (21cm) Analysis Pipeline",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "file",
        nargs="?",
        default=None,
        help="Path to the raw CS16 I/Q recording file. If omitted, launches interactive wizard.",
    )
    parser.add_argument(
        "--fc",
        "--center-freq",
        dest="center_freq",
        type=str,
        default=None,
        help="Actual SDR center LO frequency (e.g. 1420.5, 1420.5MHz, or 1420500000). Combats DC offset spikes.",
    )
    parser.add_argument(
        "--fs",
        "--sample-rate",
        dest="sample_rate",
        type=str,
        default="2.4MSPS",
        help="SDR sample rate in Hz or MSPS (e.g. 2.4MSPS, 2400000).",
    )
    parser.add_argument(
        "--fft-size",
        "-n",
        dest="fft_size",
        type=int,
        default=8192,
        help="FFT channel size (e.g. 4096, 8192, 16384).",
    )
    parser.add_argument(
        "--duration",
        "-t",
        dest="duration",
        type=float,
        default=None,
        help="Max observation duration to process in seconds (e.g. 120 for 2 minutes). Default: entire file.",
    )
    parser.add_argument(
        "--baseline",
        dest="baseline_method",
        type=str,
        default="ripple_filter",
        choices=["ripple_filter", "als", "poly"],
        help="Baseline model algorithm.",
    )
    parser.add_argument(
        "--tsys",
        dest="system_temp_k",
        type=float,
        default=100.0,
        help="Assumed system noise temperature in Kelvin for antenna temperature scaling.",
    )
    parser.add_argument(
        "--output-dir",
        "-o",
        dest="output_dir",
        type=str,
        default=None,
        help="Output directory path. Defaults to 'output' inside Astro.",
    )

    args = parser.parse_args()

    # If no file is provided via CLI and stdin is a TTY, launch interactive wizard
    if args.file is None:
        filepath, config = interactive_wizard()
    else:
        filepath = args.file.strip('"').strip("'")
        if not os.path.exists(filepath):
            # Check if it's relative to workspace
            parent_dir = os.path.abspath(os.path.join(current_dir, ".."))
            alt_path = os.path.join(parent_dir, filepath)
            if os.path.exists(alt_path):
                filepath = alt_path
            else:
                print(f"Error: Specified file not found: {filepath}")
                sys.exit(1)

        fc_hz = parse_freq_or_rate(args.center_freq, default_is_mhz=True) if args.center_freq else 1420.5e6
        fs_hz = parse_freq_or_rate(args.sample_rate, default_is_mhz=True)
        out_dir = args.output_dir if args.output_dir else os.path.join(current_dir, "output")

        config = AstroConfig(
            center_freq_hz=fc_hz,
            sample_rate_hz=fs_hz,
            fft_size=args.fft_size,
            baseline_method=args.baseline_method,
            system_temp_k=args.system_temp_k,
            max_duration_s=args.duration,
            output_dir=out_dir,
        )

    run_pipeline(filepath, config)


if __name__ == "__main__":
    main()
