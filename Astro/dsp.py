"""
Astro Radio Telescope Hydrogen Line Analysis Pipeline
Digital Signal Processing, Streaming FFT, and Astronomical Analysis Engine
"""

import os
import sys
import time
import math
from typing import Dict, Tuple, Optional, Any

import numpy as np
import scipy.fft as fft
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import curve_fit
from scipy.sparse import diags
from scipy.sparse.linalg import spsolve

from config import (
    AstroConfig,
    HYDROGEN_REST_FREQ_HZ,
    HYDROGEN_REST_FREQ_MHZ,
    SPEED_OF_LIGHT_KMS,
    HI_COLUMN_DENSITY_FACTOR,
)


# Ensure standard encoding safety on Windows
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

def get_window(name: str, size: int) -> np.ndarray:
    """Generate normalized window function for spectral analysis."""
    name = name.lower()
    if name == "blackman":
        return np.blackman(size).astype(np.float32)
    elif name == "hann" or name == "hanning":
        return np.hanning(size).astype(np.float32)
    elif name == "blackmanharris":
        from scipy.signal.windows import blackmanharris
        return blackmanharris(size).astype(np.float32)
    else:
        return np.blackman(size).astype(np.float32)


def gaussian(v, a, v0, sigma):
    """1D Gaussian model: a * exp(-(v - v0)^2 / (2 * sigma^2))"""
    return a * np.exp(-((v - v0) ** 2) / (2 * sigma ** 2))


def double_gaussian(v, a1, v0_1, sigma1, a2, v0_2, sigma2):
    """Double Gaussian model for two galactic velocity components."""
    return gaussian(v, a1, v0_1, sigma1) + gaussian(v, a2, v0_2, sigma2)


class StreamingProcessor:
    """
    High-speed, memory-efficient streaming IQ processor.
    Processes multi-gigabyte CS16 files with minimal RAM footprint (<100 MB).
    """

    def __init__(self, filepath: str, config: AstroConfig):
        self.filepath = filepath
        self.config = config
        
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"IQ file not found: {filepath}")
            
        self.filesize = os.path.getsize(filepath)
        # CS16 format: 16-bit signed integer I, 16-bit signed integer Q (4 bytes per complex sample)
        self.total_samples = self.filesize // 4
        self.total_duration_s = self.total_samples / config.sample_rate_hz
        
        if self.config.max_duration_s is not None and self.config.max_duration_s > 0:
            target_samples = int(self.config.max_duration_s * config.sample_rate_hz)
            self.samples_to_process = min(self.total_samples, target_samples)
        else:
            self.samples_to_process = self.total_samples

        self.bytes_to_process = self.samples_to_process * 4
        self.duration_to_process_s = self.samples_to_process / config.sample_rate_hz
        
        self.fft_size = config.fft_size
        self.bin_res_hz = config.sample_rate_hz / self.fft_size
        self.bin_res_kms = SPEED_OF_LIGHT_KMS * (self.bin_res_hz / HYDROGEN_REST_FREQ_HZ)
        self.window = get_window(config.window, self.fft_size)

    def process(self) -> Dict[str, Any]:
        """
        Stream the CS16 file, compute Welch periodograms, build decimated waterfall,
        and accumulate the overall power spectrum.
        """
        nfft = self.fft_size
        chunk_blocks = self.config.chunk_blocks
        chunk_samples = chunk_blocks * nfft
        
        # Preallocate reusable buffers for ultra-fast zero-allocation streaming
        raw_buffer = np.empty(chunk_samples * 2, dtype=np.int16)
        cx_buffer = np.empty(chunk_samples, dtype=np.complex64)
        
        # Waterfall decimation parameters
        waterfall_dt = self.config.waterfall_dt_s
        blocks_per_waterfall_row = max(1, int(waterfall_dt * self.config.sample_rate_hz / nfft))
        estimated_rows = max(1, int(math.ceil(self.samples_to_process / (blocks_per_waterfall_row * nfft))))
        
        waterfall_matrix = []
        current_row_accum = np.zeros(nfft, dtype=np.float64)
        current_row_blocks = 0
        waterfall_timestamps = []
        
        # Accumulators
        power_accumulator = np.zeros(nfft, dtype=np.float64)
        total_blocks_accumulated = 0
        bytes_read_so_far = 0
        
        t_start = time.perf_counter()
        last_progress_time = t_start
        
        print("\n" + "=" * 70)
        print("  STREAMING SPECTRAL ACCUMULATION & WATERFALL EXTRACTION")
        print("=" * 70)
        print(f"  Target File:        {os.path.basename(self.filepath)}")
        print(f"  File Size:          {self.filesize / (1024**3):.2f} GB ({self.filesize / (1024**2):.1f} MB)")
        print(f"  Total Duration:     {self.total_duration_s:.1f} s ({self.total_duration_s / 60.0:.2f} min)")
        print(f"  Processing Scope:   {self.duration_to_process_s:.1f} s ({self.duration_to_process_s / 60.0:.2f} min)")
        print(f"  Sample Rate:        {self.config.sample_rate_hz / 1e6:.3f} MSPS")
        print(f"  Center Frequency:   {self.config.center_freq_hz / 1e6:.4f} MHz")
        print(f"  FFT Size:           {nfft} bins ({self.bin_res_hz:.2f} Hz/bin, {self.bin_res_kms:.3f} km/s/bin)")
        print(f"  Window Function:    {self.config.window.capitalize()}")
        print("-" * 70)

        with open(self.filepath, "rb") as f:
            while bytes_read_so_far < self.bytes_to_process:
                # Read at most chunk_samples or remaining needed
                bytes_left = self.bytes_to_process - bytes_read_so_far
                read_bytes = min(len(raw_buffer) * 2, bytes_left)
                
                # Use slice view of buffer for exact read
                int16_to_read = read_bytes // 2
                sub_raw = raw_buffer[:int16_to_read]
                
                n_read = f.readinto(sub_raw)
                if n_read <= 0:
                    break
                
                bytes_read_so_far += n_read
                samples_in_chunk = (n_read // 2) // 2
                blocks = samples_in_chunk // nfft
                
                if blocks == 0:
                    continue
                
                valid_complex = blocks * nfft
                cx_sub = cx_buffer[:valid_complex]
                
                # Interleaved I/Q: I in even indices, Q in odd indices
                cx_sub.real = sub_raw[0 : valid_complex * 2 : 2]
                cx_sub.imag = sub_raw[1 : valid_complex * 2 : 2]
                
                # Reshape to (blocks, nfft) and apply window
                block_matrix = cx_sub.reshape(blocks, nfft)
                block_matrix *= self.window
                
                # Multithreaded FFT with workers=-1
                fft_matrix = fft.fftshift(fft.fft(block_matrix, axis=1, workers=-1), axes=1)
                
                # Power computation: real^2 + imag^2
                power_blocks = fft_matrix.real**2 + fft_matrix.imag**2
                
                # Accumulate into overall power
                power_accumulator += np.sum(power_blocks, axis=0)
                total_blocks_accumulated += blocks
                
                # Update waterfall on the fly
                for b_idx in range(blocks):
                    current_row_accum += power_blocks[b_idx]
                    current_row_blocks += 1
                    
                    if current_row_blocks >= blocks_per_waterfall_row:
                        row_avg = current_row_accum / current_row_blocks
                        waterfall_matrix.append(row_avg.astype(np.float32))
                        # Record elapsed seconds
                        waterfall_timestamps.append(len(waterfall_matrix) * (blocks_per_waterfall_row * nfft / self.config.sample_rate_hz))
                        current_row_accum.fill(0)
                        current_row_blocks = 0
                
                # Progress display update
                now = time.perf_counter()
                if now - last_progress_time >= 0.25 or bytes_read_so_far >= self.bytes_to_process:
                    last_progress_time = now
                    elapsed = now - t_start
                    pct = min(100.0, (bytes_read_so_far / self.bytes_to_process) * 100.0)
                    speed_mb = (bytes_read_so_far / (1024**2)) / max(0.001, elapsed)
                    speed_msps = (bytes_read_so_far / 4 / 1e6) / max(0.001, elapsed)
                    eta = (self.bytes_to_process - bytes_read_so_far) / max(1, speed_mb * 1024 * 1024)
                    
                    bar_len = 24
                    filled = int(bar_len * (pct / 100.0))
                    bar = "=" * filled + "-" * (bar_len - filled)
                    
                    sys.stdout.write(
                        f"\r  [{bar}] {pct:5.1f}% | {bytes_read_so_far/(1024**3):.2f}/{self.bytes_to_process/(1024**3):.2f} GB "
                        f"| {speed_mb:5.1f} MB/s ({speed_msps:4.1f} MSPS) | ETA: {eta:4.1f}s  "
                    )
                    sys.stdout.flush()

        # Flush any partial waterfall row
        if current_row_blocks > 0:
            waterfall_matrix.append((current_row_accum / current_row_blocks).astype(np.float32))
            waterfall_timestamps.append(self.duration_to_process_s)

        t_end = time.perf_counter()
        total_time = max(0.001, t_end - t_start)
        
        sys.stdout.write("\r" + " " * 85 + "\r")
        sys.stdout.flush()
        print(f"  Streaming complete in {total_time:.2f} s ({total_blocks_accumulated:,} FFT blocks accumulated)")
        print(f"  Average Throughput: {(bytes_read_so_far / (1024**2)) / total_time:.1f} MB/s "
              f"({(bytes_read_so_far / 4 / 1e6) / total_time:.1f} MSPS)")
        print("=" * 70 + "\n")

        if total_blocks_accumulated == 0:
            raise ValueError("No complete FFT blocks could be read from the file.")

        averaged_power = power_accumulator / total_blocks_accumulated
        waterfall_array = np.array(waterfall_matrix, dtype=np.float32) if waterfall_matrix else np.expand_dims(averaged_power.astype(np.float32), 0)
        
        # Frequency axes
        freq_rel = np.fft.fftshift(np.fft.fftfreq(nfft, 1.0 / self.config.sample_rate_hz))
        freq_abs_hz = freq_rel + self.config.center_freq_hz
        freq_abs_mhz = freq_abs_hz / 1e6
        
        return {
            "averaged_power": averaged_power,
            "waterfall": waterfall_array,
            "waterfall_times": np.array(waterfall_timestamps),
            "freq_hz": freq_abs_hz,
            "freq_mhz": freq_abs_mhz,
            "total_blocks": total_blocks_accumulated,
            "total_duration_s": self.duration_to_process_s,
            "elapsed_processing_time_s": total_time,
        }


def excise_dc_spike(power: np.ndarray, config: AstroConfig) -> Tuple[np.ndarray, Dict[str, Any]]:
    """
    Excise the DC offset / LO leakage spike at 0 Hz IF (center frequency).
    Smoothly interpolates across the DC notch using surrounding uncorrupted bins.
    """
    nfft = len(power)
    bin_res_hz = config.sample_rate_hz / nfft
    dc_bin = nfft // 2  # After fftshift, 0 Hz IF is precisely at center bin
    
    half_width_bins = max(1, int(math.ceil((config.dc_clear_hz / 2.0) / bin_res_hz)))
    guard_bins = max(2, half_width_bins * 2)
    
    start_dc = max(0, dc_bin - half_width_bins)
    end_dc = min(nfft - 1, dc_bin + half_width_bins)
    
    # Clean neighborhood outside the DC spike
    left_guard = power[max(0, start_dc - guard_bins) : start_dc]
    right_guard = power[end_dc + 1 : min(nfft, end_dc + 1 + guard_bins)]
    
    clean_neighbors = np.concatenate([left_guard, right_guard])
    replacement_median = np.median(clean_neighbors) if len(clean_neighbors) > 0 else np.median(power)
    
    clean_power = np.copy(power)
    # Linear interpolation across the DC zone between the left and right endpoints
    left_val = np.median(left_guard) if len(left_guard) > 0 else replacement_median
    right_val = np.median(right_guard) if len(right_guard) > 0 else replacement_median
    interp_line = np.linspace(left_val, right_val, end_dc - start_dc + 1)
    clean_power[start_dc : end_dc + 1] = interp_line
    
    dc_info = {
        "dc_bin": dc_bin,
        "dc_center_freq_mhz": config.center_freq_hz / 1e6,
        "cleared_bins": end_dc - start_dc + 1,
        "cleared_bandwidth_khz": (end_dc - start_dc + 1) * bin_res_hz / 1e3,
    }
    return clean_power, dc_info


def mitigate_rfi_spikes(power: np.ndarray, freqs_mhz: np.ndarray, config: AstroConfig) -> Tuple[np.ndarray, int]:
    """
    Detect and excise sharp narrow-band RFI comb lines.
    Protects the astronomical Hydrogen Line window so celestial emission is never touched.
    """
    nfft = len(power)
    clean_power = np.copy(power)
    
    # Astronomical HI protection window (+/- protection half width around rest freq)
    hi_half_width_mhz = config.hi_protection_half_width_khz / 1e3
    hi_mask = (freqs_mhz >= HYDROGEN_REST_FREQ_MHZ - hi_half_width_mhz) & (freqs_mhz <= HYDROGEN_REST_FREQ_MHZ + hi_half_width_mhz)
    
    # Also ignore outer 5% band edges where SDR decimation filters roll off
    edge_bins = int(nfft * 0.05)
    valid_mask = np.ones(nfft, dtype=bool)
    valid_mask[:edge_bins] = False
    valid_mask[-edge_bins:] = False
    valid_mask[hi_mask] = False  # DO NOT treat HI line as RFI!
    
    # Rolling median filter to isolate spikes
    kernel_size = 15
    pad_width = kernel_size // 2
    padded = np.pad(clean_power, pad_width, mode="edge")
    rolling_median = np.array([np.median(padded[i : i + kernel_size]) for i in range(nfft)])
    
    residuals = clean_power - rolling_median
    # Calculate Median Absolute Deviation (MAD) on clean valid regions
    residuals_valid = residuals[valid_mask]
    mad = np.median(np.abs(residuals_valid - np.median(residuals_valid)))
    sigma_est = 1.4826 * mad
    
    rfi_indices = np.where((residuals > config.rfi_threshold_mad * sigma_est) & valid_mask)[0]
    
    # Interpolate infected channels from local median
    for idx in rfi_indices:
        l_idx = max(0, idx - 4)
        r_idx = min(nfft - 1, idx + 4)
        clean_power[idx] = (clean_power[l_idx] + clean_power[r_idx]) / 2.0
        
    return clean_power, len(rfi_indices)


def fit_baseline(power: np.ndarray, freqs_mhz: np.ndarray, config: AstroConfig) -> np.ndarray:
    """
    Estimate the instrumental bandpass baseline shape.
    Operates in logarithmic (dB) scale for optimal noise modeling and standing wave tracking.
    
    Methods:
    - 'als': Adaptive Asymmetric Least Squares (Whittaker smoothing) tuned to cable reflections.
    - 'poly': Iterative sigma-clipped high-order polynomial fit across usable passband.
    - 'filter': Fast ripple-tuned smoothed background interpolation.
    """
    nfft = len(power)
    bin_res_hz = config.sample_rate_hz / nfft
    method = config.baseline_method.lower()
    
    # 1. Isolate the usable SDR passband (exclude steep anti-aliasing filter skirts at the outer edges)
    edge_bins = int(nfft * config.passband_edge_fraction)
    passband_mask = np.zeros(nfft, dtype=bool)
    passband_mask[edge_bins : nfft - edge_bins] = True
    
    # 2. Protection zone around astronomical HI rest frequency (+/- protection half-width)
    hi_half_width_mhz = config.hi_protection_half_width_khz / 1e3
    hi_window = (freqs_mhz >= HYDROGEN_REST_FREQ_MHZ - hi_half_width_mhz) & (freqs_mhz <= HYDROGEN_REST_FREQ_MHZ + hi_half_width_mhz)
    
    # Clean passband channels (usable passband excluding the target HI line)
    clean_passband = passband_mask & (~hi_window)
    
    p_db = 10.0 * np.log10(np.maximum(power, 1e-12))
    
    if method == "poly":
        # Iterative Sigma-Clipped Polynomial across the usable passband
        x_all = (freqs_mhz - np.mean(freqs_mhz[passband_mask])) / np.std(freqs_mhz[passband_mask])
        
        # Fit on passband points initially excluding HI window
        fit_indices = np.where(clean_passband)[0]
        weights = np.ones(len(fit_indices), dtype=float)
        
        deg = min(14, max(4, config.poly_degree))
        for _ in range(6):
            coeffs = np.polyfit(x_all[fit_indices], p_db[fit_indices], deg=deg, w=weights)
            poly_eval = np.poly1d(coeffs)(x_all[fit_indices])
            res = p_db[fit_indices] - poly_eval
            sigma = np.std(res[weights > 0.5]) if np.any(weights > 0.5) else 0.05
            # Reject positive outliers (> 1.5 sigma) to prevent line contamination
            weights = ((res < 1.5 * sigma) & (res > -3.0 * sigma)).astype(float)
            
        full_poly_db = np.poly1d(coeffs)(x_all)
        # Center baseline on the median of clean passband noise
        offset = np.median(p_db[clean_passband] - full_poly_db[clean_passband])
        baseline_db = full_poly_db + offset
        
    elif method == "filter":
        # Ripple-aware smoothed background
        x_all = np.arange(nfft)
        interpolated_db = np.interp(x_all, x_all[clean_passband], p_db[clean_passband])
        sigma_bins = max(3, int((config.filter_smoothing_sigma_khz * 1e3) / bin_res_hz))
        smoothed_db = gaussian_filter1d(interpolated_db, sigma=sigma_bins)
        offset = np.median(p_db[clean_passband] - smoothed_db[clean_passband])
        baseline_db = smoothed_db + offset
        
    else:  # 'als' (Default & Recommended: Adaptive Whittaker Smoothing)
        L = nfft
        D = diags([1, -2, 1], [0, 1, 2], shape=(L - 2, L), dtype=float)
        w = np.ones(L, dtype=float)
        
        # Downweight outer filter skirts so they do not warp the passband baseline
        w[~passband_mask] = 0.02
        # Heavily downweight the astronomical HI line so it is never absorbed into the baseline
        w[hi_window] = 0.0001
        
        lam = float(config.als_lambda)
        p = float(config.als_p)
        
        for _ in range(8):
            W = diags([w], [0], shape=(L, L), dtype=float)
            A = (W + lam * D.T.dot(D)).tocsc()
            z = spsolve(A, w * p_db)
            w = p * (p_db > z) + (1.0 - p) * (p_db < z)
            w[~passband_mask] = 0.02
            w[hi_window] = 0.0001
            
        # Center baseline directly on the passband noise floor median (0 dB mean residuals)
        res_db = p_db - z
        noise_offset = np.median(res_db[clean_passband])
        baseline_db = z + noise_offset

    # Return baseline in linear power units matching raw input power
    baseline_linear = 10.0 ** (baseline_db / 10.0)
    return np.maximum(baseline_linear, 1e-12)


def refine_local_baseline(
    vel_kms: np.ndarray,
    calibrated_k: np.ndarray,
    calibrated_db: np.ndarray,
    config: AstroConfig,
) -> Tuple[np.ndarray, np.ndarray, np.poly1d]:
    """
    Standard Radio Astronomy Stage 2 Calibration:
    Performs local baseline detrending in the zoomed line velocity profile (-200 to +200 km/s).
    Fits a low-order polynomial across line-free reference channels (|v| > threshold)
    to guarantee a dead-flat baseline centered at identically 0.000 Kelvin / 0.000 dB.
    """
    v_thresh = config.line_free_velocity_kms
    # Zoom velocity mask
    zoom_mask = (vel_kms >= -200.0) & (vel_kms <= 200.0)
    
    # Line-free reference channels within zoom window
    line_free = zoom_mask & (np.abs(vel_kms) >= v_thresh)
    
    if np.sum(line_free) >= 10:
        deg = max(0, min(2, config.local_baseline_order))
        p_fit = np.poly1d(np.polyfit(vel_kms[line_free], calibrated_db[line_free], deg=deg))
        local_trend_db = p_fit(vel_kms)
        
        refined_db = calibrated_db - local_trend_db
        # Recalculate Kelvin from refined dB relative power: T_A = T_sys * (10^(dB/10) - 1)
        refined_k = config.system_temp_k * (10.0 ** (refined_db / 10.0) - 1.0)
        return refined_k, refined_db, p_fit
    else:
        return calibrated_k, calibrated_db, np.poly1d([0.0])


def compute_doppler_velocity(freqs_hz: np.ndarray) -> np.ndarray:
    """
    Convert frequency axis to Topocentric Doppler Radial Velocity in km/s:
    v = c * (f_rest - f) / f_rest
    Radio astronomy sign convention:
    v > 0: Receding (Redshifted, lower frequency)
    v < 0: Approaching (Blueshifted, higher frequency)
    """
    return SPEED_OF_LIGHT_KMS * ((HYDROGEN_REST_FREQ_HZ - freqs_hz) / HYDROGEN_REST_FREQ_HZ)


def fit_gaussian_profile(vel_kms: np.ndarray, temp_k: np.ndarray, hi_window: np.ndarray) -> Dict[str, Any]:
    """
    Fit a Gaussian emission profile to the calibrated Hydrogen Line within the HI window.
    Extracts peak velocity, peak brightness, FWHM, and velocity dispersion.
    """
    v_sub = vel_kms[hi_window]
    t_sub = temp_k[hi_window]
    
    # Non-parametric initial estimate
    max_idx = np.argmax(t_sub)
    a_init = float(t_sub[max_idx])
    v0_init = float(v_sub[max_idx])
    
    # Estimate line width roughly
    half_max = a_init / 2.0
    above_half = v_sub[t_sub >= half_max]
    if len(above_half) >= 2:
        fwhm_init = float(np.ptp(above_half))
    else:
        fwhm_init = 25.0  # Typical Milky Way HI line FWHM ~25 km/s
        
    sigma_init = max(2.0, fwhm_init / 2.35482)
    
    fit_success = False
    try:
        popt, pcov = curve_fit(
            gaussian,
            v_sub,
            t_sub,
            p0=[a_init, v0_init, sigma_init],
            bounds=([0.1, -250.0, 1.0], [a_init * 2.5 + 5.0, 250.0, 100.0]),
            maxfev=3000,
        )
        a_fit, v0_fit, sigma_fit = popt
        fwhm_fit = 2.35482 * abs(sigma_fit)
        fit_curve = gaussian(vel_kms, a_fit, v0_fit, sigma_fit)
        fit_success = True
    except Exception:
        a_fit = a_init
        v0_fit = v0_init
        sigma_fit = sigma_init
        fwhm_fit = fwhm_init
        fit_curve = np.zeros_like(vel_kms)
        fit_success = False

    return {
        "success": fit_success,
        "amplitude_k": a_fit,
        "v_center_kms": v0_fit,
        "sigma_kms": abs(sigma_fit),
        "fwhm_kms": fwhm_fit,
        "fitted_curve": fit_curve,
    }


def calculate_hi_metrics(
    freqs_mhz: np.ndarray,
    vel_kms: np.ndarray,
    calibrated_k: np.ndarray,
    calibrated_db: np.ndarray,
    config: AstroConfig,
) -> Dict[str, Any]:
    """
    Calculate comprehensive radio astronomy metrics for the detected Hydrogen Line:
    - Peak frequency & velocity
    - Peak Antenna Temperature & dB
    - Local noise floor & SNR
    - Integrated line area / flux W (K * km/s)
    - Neutral Hydrogen Column Density N_HI (cm^-2)
    - FWHM and velocity dispersion
    """
    nfft = len(freqs_mhz)
    
    # 1. HI Window (+/- protection half-width around 1420.406 MHz)
    hi_half_width_mhz = config.hi_protection_half_width_khz / 1e3
    hi_window = (freqs_mhz >= HYDROGEN_REST_FREQ_MHZ - hi_half_width_mhz) & (freqs_mhz <= HYDROGEN_REST_FREQ_MHZ + hi_half_width_mhz)
    
    # 2. Line-free channels in the zoom window (|v| between line_free_velocity_kms and 200 km/s)
    zoom_mask = (vel_kms >= -200.0) & (vel_kms <= 200.0)
    line_free_mask = zoom_mask & (np.abs(vel_kms) >= config.line_free_velocity_kms)
    
    if np.sum(line_free_mask) >= 10:
        noise_std_k = float(np.std(calibrated_k[line_free_mask]))
        noise_std_db = float(np.std(calibrated_db[line_free_mask]))
    else:
        edge_bins = int(nfft * config.passband_edge_fraction)
        clean_mask = np.ones(nfft, dtype=bool)
        clean_mask[:edge_bins] = False
        clean_mask[-edge_bins:] = False
        clean_mask[hi_window] = False
        noise_std_k = float(np.std(calibrated_k[clean_mask]))
        noise_std_db = float(np.std(calibrated_db[clean_mask]))
    
    # Peak detection in HI window
    hi_indices = np.where(hi_window)[0]
    if len(hi_indices) > 0:
        local_peak_idx = np.argmax(calibrated_k[hi_window])
        peak_idx = hi_indices[local_peak_idx]
        peak_freq_mhz = float(freqs_mhz[peak_idx])
        peak_vel_kms = float(vel_kms[peak_idx])
        peak_temp_k = float(calibrated_k[peak_idx])
        peak_power_db = float(calibrated_db[peak_idx])
    else:
        peak_idx = nfft // 2
        peak_freq_mhz = HYDROGEN_REST_FREQ_MHZ
        peak_vel_kms = 0.0
        peak_temp_k = 0.0
        peak_power_db = 0.0

    snr_k = peak_temp_k / max(1e-6, noise_std_k)
    snr_db = peak_power_db / max(1e-6, noise_std_db)

    # Integrated line flux: W = integral(T_A * dv) in [K * km/s]
    # dv is constant for uniform frequency bins
    dv = abs(float(vel_kms[1] - vel_kms[0])) if len(vel_kms) > 1 else 1.0
    line_integral_k_kms = float(np.sum(np.clip(calibrated_k[hi_window], 0, None)) * dv)
    
    # Neutral hydrogen column density N_HI = 1.8224e18 * W (cm^-2)
    column_density_cm2 = HI_COLUMN_DENSITY_FACTOR * line_integral_k_kms

    # Gaussian profile fitting
    gauss_fit = fit_gaussian_profile(vel_kms, calibrated_k, hi_window)

    # Calculate offset between SDR LO center freq and HI line
    lo_offset_khz = (config.center_freq_hz - HYDROGEN_REST_FREQ_HZ) / 1e3
    dc_bin = nfft // 2
    hi_bin = np.argmin(np.abs(freqs_mhz - HYDROGEN_REST_FREQ_MHZ))
    bin_separation = abs(dc_bin - hi_bin)

    return {
        "peak_freq_mhz": peak_freq_mhz,
        "peak_vel_kms": peak_vel_kms,
        "peak_temp_k": peak_temp_k,
        "peak_power_db": peak_power_db,
        "noise_std_k": noise_std_k,
        "noise_std_db": noise_std_db,
        "snr": snr_k,
        "integrated_flux_k_kms": line_integral_k_kms,
        "column_density_cm2": column_density_cm2,
        "fwhm_kms": gauss_fit["fwhm_kms"],
        "sigma_kms": gauss_fit["sigma_kms"],
        "gauss_fit": gauss_fit,
        "lo_offset_khz": lo_offset_khz,
        "bin_separation": bin_separation,
        "dc_bin": dc_bin,
        "hi_bin": hi_bin,
    }
