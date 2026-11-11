#!/usr/bin/env python3
"""
Quick 3-Panel Hydrogen Line Viewer (Lightweight Matplotlib Inspector)
Plots:
  1. Full passband & ALS baseline fit
  2. Zoomed raw HI window (1420.00 - 1420.60 MHz)
  3. Baseline-subtracted spectrum vs LSR Doppler Velocity (km/s)
"""

import os
import sys
import argparse
import numpy as np
import matplotlib.pyplot as plt

try:
    from scipy import sparse
    from scipy.sparse.linalg import spsolve
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

HI_REST_FREQ_HZ = 1420405751.786
C_KMS = 299792.458


def baseline_als(y: np.ndarray, lam: float = 1e5, p: float = 0.01, niter: int = 10) -> np.ndarray:
    """Asymmetric Least Squares baseline estimator."""
    if not HAS_SCIPY:
        poly = np.polyfit(np.arange(len(y)), y, 3)
        return np.polyval(poly, np.arange(len(y)))
    
    L = len(y)
    D = sparse.diags([1, -2, 1], [0, -1, -2], shape=(L, L - 2))
    w = np.ones(L)
    for _ in range(niter):
        W = sparse.spdiags(w, 0, L, L)
        Z = W + lam * D.dot(D.transpose())
        z = spsolve(Z, w * y)
        w = p * (y > z) + (1 - p) * (y <= z)
    return z


def process_file(
    filename: str,
    center_freq_hz: float = 1420.5e6,
    sample_rate_hz: float = 2.4e6,
    fft_size: int = 4096,
    chunk_samples: int = 1048576,
    max_duration_s: float = None,
    als_lambda: float = 1e5,
    als_p: float = 0.01
):
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found.")
        return

    file_size = os.path.getsize(filename)
    bytes_per_sample = 4  # CS16 = 2B I + 2B Q
    total_samples = file_size // bytes_per_sample
    duration_s = total_samples / sample_rate_hz

    if max_duration_s and max_duration_s < duration_s:
        samples_to_read = int(max_duration_s * sample_rate_hz)
    else:
        samples_to_read = total_samples

    print(f"Processing '{filename}': {file_size / (1024**2):.1f} MB (~{duration_s:.1f}s at {sample_rate_hz/1e6:.2f} MSPS)...")

    window = np.hanning(fft_size)
    window_norm = np.sum(window**2)
    accumulated_power = np.zeros(fft_size, dtype=np.float64)
    total_ffts = 0

    chunk_bytes = chunk_samples * bytes_per_sample
    read_samples = 0

    with open(filename, "rb") as f:
        while read_samples < samples_to_read:
            to_read = min(chunk_bytes, (samples_to_read - read_samples) * bytes_per_sample)
            raw_bytes = f.read(to_read)
            if not raw_bytes:
                break
            
            raw16 = np.frombuffer(raw_bytes, dtype=np.int16)
            n_pairs = len(raw16) // 2
            if n_pairs < fft_size:
                break

            i_samples = raw16[0 : 2 * n_pairs : 2].astype(np.float32)
            q_samples = raw16[1 : 2 * n_pairs : 2].astype(np.float32)
            iq = i_samples + 1j * q_samples

            num_blocks = len(iq) // fft_size
            iq_matrix = iq[: num_blocks * fft_size].reshape(num_blocks, fft_size)

            fft_matrix = np.fft.fft(iq_matrix * window, axis=1)
            power_matrix = np.abs(fft_matrix) ** 2
            accumulated_power += np.sum(power_matrix, axis=0) / window_norm
            total_ffts += num_blocks
            read_samples += num_blocks * fft_size

    if total_ffts == 0:
        print("Error: No FFT blocks processed.")
        return

    avg_power = np.fft.fftshift(accumulated_power / total_ffts)
    freq_offsets = np.fft.fftshift(np.fft.fftfreq(fft_size, 1 / sample_rate_hz))
    freqs_mhz = (center_freq_hz + freq_offsets) / 1e6
    velocities_kms = C_KMS * (HI_REST_FREQ_HZ - (center_freq_hz + freq_offsets)) / HI_REST_FREQ_HZ

    # Crop outer 6.5% filter skirts
    crop = int(fft_size * 0.065)
    f_crop = freqs_mhz[crop:-crop]
    v_crop = velocities_kms[crop:-crop]
    p_crop = 10 * np.log10(np.maximum(avg_power[crop:-crop], 1e-12))

    # Baseline fit
    baseline = baseline_als(p_crop, lam=als_lambda, p=als_p)
    calibrated = p_crop - baseline

    # Sort velocities
    v_sort_idx = np.argsort(v_crop)
    v_sorted = v_crop[v_sort_idx]
    cal_sorted = calibrated[v_sort_idx]

    # Plot
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(11, 10))

    # Ax1: Full Passband
    ax1.plot(f_crop, p_crop, color="#1f77b4", label="Observed Raw Power")
    ax1.plot(f_crop, baseline, "--", color="orange", label=f"ALS Baseline (λ={als_lambda:.0e})")
    ax1.axvline(HI_REST_FREQ_HZ / 1e6, color="red", linestyle=":", label="HI Rest (1420.406 MHz)")
    ax1.axvline(center_freq_hz / 1e6, color="gray", linestyle="--", alpha=0.5, label="LO Center")
    ax1.set_ylabel("Power (dB)")
    ax1.set_title(f"Full Passband & Baseline Fit (Center LO: {center_freq_hz/1e6:.3f} MHz)")
    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.3)

    # Ax2: Zoomed HI Region
    z_min, z_max = 1420.00, 1420.60
    z_mask = (f_crop >= z_min) & (f_crop <= z_max)
    if np.any(z_mask):
        ax2.plot(f_crop[z_mask], p_crop[z_mask], color="#1f77b4", label="Zoomed Raw Power")
        ax2.axvline(HI_REST_FREQ_HZ / 1e6, color="red", linestyle=":", label="HI Rest Line")
        ax2.axvline(center_freq_hz / 1e6, color="gray", linestyle="--", alpha=0.5, label="LO Center")
        ax2.set_xlim(z_min, z_max)
        ax2.set_ylabel("Power (dB)")
        ax2.set_title("Zoomed View (1420.00 - 1420.60 MHz)")
        ax2.legend(loc="upper right")
        ax2.grid(True, alpha=0.3)

    # Ax3: Doppler Velocity
    ax3.plot(v_sorted, cal_sorted, color="#2ca02c", label="Calibrated Spectrum (dB)")
    ax3.axvline(0, color="red", linestyle=":", label="0 km/s LSR Rest")
    ax3.set_xlabel("Topocentric Radial Doppler Velocity (km/s)")
    ax3.set_ylabel("Relative Power (dB)")
    ax3.set_title("Calibrated Hydrogen Line Emission vs. Doppler Radial Velocity")
    ax3.set_xlim(np.min(v_sorted), np.max(v_sorted))
    ax3.legend(loc="upper right")
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()


def load_accompanied_parameters(cs16_path: str):
    base = os.path.splitext(cs16_path)[0]
    json_path = base + ".json"
    if os.path.exists(json_path):
        try:
            import json
            with open(json_path, "r", encoding="utf-8") as f:
                d = json.load(f)
                return float(d.get("center_freq_hz", 1420.5e6)), float(d.get("sample_rate_hz", 2.4e6)), os.path.basename(json_path)
        except Exception:
            pass

    txt_path = base + ".txt"
    if os.path.exists(txt_path):
        try:
            fc, fs = None, None
            with open(txt_path, "r", encoding="utf-8") as f:
                for line in f:
                    if "Tuned SDR Center LO:" in line:
                        parts = line.split(":")
                        if len(parts) > 1:
                            fc = float(parts[1].strip().split()[0].replace(",", "")) * 1e6
                    elif "Sampling Rate (Fs):" in line:
                        parts = line.split(":")
                        if len(parts) > 1:
                            fs = float(parts[1].strip().split()[0].replace(",", "")) * 1e6
            if fc and fs:
                return fc, fs, os.path.basename(txt_path)
        except Exception:
            pass

    return 1420.5e6, 2.4e6, None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Quick 3-Panel Hydrogen Line Viewer")
    parser.add_argument("file", help="Path to raw .cs16 file")
    parser.add_argument("--fc", type=float, default=None, help="Center LO frequency in Hz (default: from .txt/.json or 1420.5e6)")
    parser.add_argument("--fs", type=float, default=None, help="Sample rate in Hz (default: from .txt/.json or 2.4e6)")
    parser.add_argument("--duration", type=float, default=None, help="Max duration in seconds")
    args = parser.parse_args()

    auto_fc, auto_fs, src = load_accompanied_parameters(args.file)
    fc = args.fc if args.fc is not None else auto_fc
    fs = args.fs if args.fs is not None else auto_fs
    if src and (args.fc is None or args.fs is None):
        print(f"Auto-configured from accompanied file '{src}': LO={fc/1e6:.4f} MHz, Fs={fs/1e6:.3f} MSPS")

    process_file(args.file, center_freq_hz=fc, sample_rate_hz=fs, max_duration_s=args.duration)
