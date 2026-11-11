"""
Astro Radio Telescope Hydrogen Line Analysis Pipeline
Scientific Data Visualization Engine - High-DPI Publication Figures
"""

import os
import time
from typing import Dict, Any, Optional

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.gridspec import GridSpec

from config import AstroConfig, HYDROGEN_REST_FREQ_MHZ, SPEED_OF_LIGHT_KMS


def safe_savefig(fig, output_path: str, **kwargs):
    """
    Robust savefig wrapper for Windows to prevent transient [Errno 22] file lock collisions
    caused by Windows Explorer thumbnail caching or active image viewers.
    """
    for attempt in range(5):
        try:
            fig.savefig(output_path, **kwargs)
            return
        except OSError:
            time.sleep(0.4)
    # Fallback to temp file if persistent lock
    temp_path = f"{output_path}.tmp.png"
    fig.savefig(temp_path, **kwargs)
    try:
        os.replace(temp_path, output_path)
    except Exception:
        pass


# Set modern dark scientific styling
plt.rcParams.update({
    "font.sans-serif": ["Segoe UI", "DejaVu Sans", "Arial", "Helvetica"],
    "font.family": "sans-serif",
    "figure.facecolor": "#0d1117",
    "axes.facecolor": "#161b22",
    "axes.edgecolor": "#30363d",
    "axes.labelcolor": "#c9d1d9",
    "xtick.color": "#8b949e",
    "ytick.color": "#8b949e",
    "grid.color": "#30363d",
    "grid.linestyle": ":",
    "grid.alpha": 0.6,
    "legend.facecolor": "#161b22",
    "legend.edgecolor": "#30363d",
    "text.color": "#c9d1d9",
})


def format_frequency(val, pos):
    return f"{val:.4f}"


def format_velocity(val, pos):
    return f"{val:+.1f}"


def plot_calibrated_spectrum(
    freqs_mhz: np.ndarray,
    vel_kms: np.ndarray,
    calibrated_k: np.ndarray,
    calibrated_db: np.ndarray,
    metrics: Dict[str, Any],
    config: AstroConfig,
    output_path: str,
):
    """
    Plot 1: High-clarity zoomed Hydrogen Line Spectrum with dual axes
    (Doppler Velocity & Sky Frequency) and Gaussian fit.
    """
    fig, ax1 = plt.subplots(figsize=(12, 6), dpi=200)

    # Window around HI: +/- 180 km/s
    zoom_mask = (vel_kms >= -200) & (vel_kms <= 200)
    v_zoom = vel_kms[zoom_mask]
    k_zoom = calibrated_k[zoom_mask]
    f_zoom = freqs_mhz[zoom_mask]

    # Primary spectrum line (Calibrated Antenna Temperature)
    ax1.plot(v_zoom, k_zoom, color="#00f2fe", lw=2.2, label=r"Calibrated HI Emission $T_A(v)$", zorder=3)
    ax1.fill_between(v_zoom, 0, np.clip(k_zoom, 0, None), color="#00f2fe", alpha=0.18, zorder=2)
    ax1.axhline(0, color="#8b949e", lw=1.0, ls="--", alpha=0.5)

    # Gaussian fit overlay
    gauss_fit = metrics.get("gauss_fit", {})
    if gauss_fit.get("success", False):
        fit_curve = gauss_fit["fitted_curve"][zoom_mask]
        ax1.plot(v_zoom, fit_curve, color="#ff7675", lw=2.0, ls="--", label=f"Gaussian Model (FWHM = {metrics['fwhm_kms']:.1f} km/s)", zorder=4)

    # Rest frequency marker (v = 0 km/s)
    ax1.axvline(0, color="#74b9ff", lw=1.5, ls=":", label="HI Rest Frame ($v = 0$ km/s)", zorder=3)

    # Identified Peak marker
    peak_v = metrics["peak_vel_kms"]
    peak_k = metrics["peak_temp_k"]
    ax1.plot([peak_v], [peak_k], marker="o", markersize=8, color="#ff4757", markeredgecolor="#ffffff", markeredgewidth=1.5, zorder=5, label=f"Galactic Peak: {peak_v:+.1f} km/s")

    # Labels and limits
    ax1.set_xlabel("Doppler Radial Velocity [Radio Definition] (km/s)", fontsize=11, fontweight="bold", labelpad=8)
    ax1.set_ylabel(r"Antenna Temperature $T_A$ (Kelvin)", fontsize=11, fontweight="bold", color="#00f2fe", labelpad=8)
    ax1.set_xlim(-200, 200)
    
    y_min = max(-1.0, np.min(k_zoom) - 0.5)
    y_max = np.max(k_zoom) * 1.35 + 1.0
    ax1.set_ylim(y_min, y_max)
    ax1.grid(True)

    # Secondary x-axis for Sky Frequency (MHz)
    ax_top = ax1.twiny()
    ax_top.set_xlim(ax1.get_xlim())
    # Map velocity ticks to corresponding frequency ticks
    v_ticks = np.array([-180, -120, -60, 0, 60, 120, 180])
    f_ticks = HYDROGEN_REST_FREQ_MHZ * (1.0 - v_ticks / SPEED_OF_LIGHT_KMS)
    ax_top.set_xticks(v_ticks)
    ax_top.set_xticklabels([f"{f:.3f}" for f in f_ticks])
    ax_top.set_xlabel("Topocentric Sky Frequency (MHz)", fontsize=11, fontweight="bold", color="#a4b0be", labelpad=8)

    # Info HUD Card
    hud_lines = [
        f"Target Line: Neutral Hydrogen (H I, 21 cm)",
        f"Rest Frequency: {HYDROGEN_REST_FREQ_MHZ:.4f} MHz",
        f"Observed Peak:  {metrics['peak_freq_mhz']:.4f} MHz",
        f"Recession Vel:  {metrics['peak_vel_kms']:+.2f} km/s",
        f"Peak Brightness:{metrics['peak_temp_k']:.2f} K  ({metrics['peak_power_db']:+.3f} dB)",
        f"Noise Floor σ:  {metrics['noise_std_k']:.3f} K",
        f"Signal SNR:     {metrics['snr']:.1f} σ",
        f"Line FWHM:      {metrics['fwhm_kms']:.1f} km/s",
        f"Integrated W:   {metrics['integrated_flux_k_kms']:.1f} K·km/s",
        f"N_HI Column:    {metrics['column_density_cm2']:.2e} cm⁻²",
    ]
    hud_text = "\n".join(hud_lines)
    ax1.text(
        0.02, 0.96, hud_text, transform=ax1.transAxes,
        fontsize=9, fontfamily="monospace", va="top", ha="left",
        color="#f1f2f6",
        bbox=dict(boxstyle="round,pad=0.6", facecolor="#1e272e", edgecolor="#00d2d3", alpha=0.9, lw=1.2)
    )

    ax1.legend(loc="upper right", framealpha=0.9, fontsize=9.5)
    plt.title("Calibrated Hydrogen Line ($H_I$) Spectrum - Galactic 21cm Analysis", fontsize=13, fontweight="bold", pad=28, color="#ffffff")

    plt.tight_layout()
    safe_savefig(fig, output_path, dpi=200, bbox_inches="tight")
    plt.close()


def plot_raw_and_baseline(
    freqs_mhz: np.ndarray,
    raw_power: np.ndarray,
    baseline_power: np.ndarray,
    metrics: Dict[str, Any],
    config: AstroConfig,
    output_path: str,
):
    """
    Plot 2: Full SDR Bandpass with Raw Spectrum, Fitted Baseline,
    DC Spike Excision Point, and Protected Astronomical Window.
    """
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), dpi=200, sharex=True, gridspec_kw={"height_ratios": [2.2, 1.2]})

    raw_db = 10.0 * np.log10(np.maximum(raw_power, 1e-12))
    base_db = 10.0 * np.log10(np.maximum(baseline_power, 1e-12))
    flat_db = raw_db - base_db

    # Identify usable passband edges (inner 87%)
    edge_bins = int(len(freqs_mhz) * config.passband_edge_fraction)
    f_pb_min = freqs_mhz[edge_bins]
    f_pb_max = freqs_mhz[-edge_bins]

    # Top panel: Raw vs Baseline
    ax1.plot(freqs_mhz, raw_db, color="#54a0ff", lw=1.0, alpha=0.75, label="Raw Averaged Spectrum")
    ax1.plot(freqs_mhz, base_db, color="#ff9f43", lw=2.2, label=f"Instrumental Baseline ({config.baseline_method.upper()} Standing-Wave Fit)")

    # Mark DC spike location (SDR LO Center Frequency)
    fc_mhz = config.center_freq_hz / 1e6
    ax1.axvline(fc_mhz, color="#ff4757", lw=1.8, ls="--", label=f"SDR Center LO: {fc_mhz:.4f} MHz (DC Notch Excised)")

    # Mark HI rest frequency
    ax1.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00d2d3", lw=1.5, ls=":", label=f"HI Rest Freq: {HYDROGEN_REST_FREQ_MHZ:.4f} MHz")

    # Shaded protected HI window
    hi_half_mhz = config.hi_protection_half_width_khz / 1e3
    ax1.axvspan(HYDROGEN_REST_FREQ_MHZ - hi_half_mhz, HYDROGEN_REST_FREQ_MHZ + hi_half_mhz, color="#10ac84", alpha=0.18, label="Protected HI Window")

    # Shade the outer SDR filter skirts in both panels
    ax1.axvspan(freqs_mhz[0], f_pb_min, color="#57606f", alpha=0.35, label="Filter Roll-Off Skirts (Excluded)")
    ax1.axvspan(f_pb_max, freqs_mhz[-1], color="#57606f", alpha=0.35)

    ax1.set_ylabel("Power Spectrum (dB)", fontsize=11, fontweight="bold", labelpad=8)
    ax1.set_title("Full SDR Bandpass & Standing-Wave Baseline Model", fontsize=13, fontweight="bold", pad=12, color="#ffffff")
    ax1.grid(True)
    ax1.legend(loc="upper right", framealpha=0.9, fontsize=8.5)

    # Annotate LO DC spike avoidance
    lo_offset_khz = metrics["lo_offset_khz"]
    dc_note = (
        f"SDR LO Offset Strategy:\n"
        f"• LO Tuned: {fc_mhz:.4f} MHz\n"
        f"• HI Rest: {HYDROGEN_REST_FREQ_MHZ:.4f} MHz\n"
        f"• Offset: {lo_offset_khz:+.1f} kHz\n"
        f"• DC spike safely avoided!"
    )
    ax1.text(0.02, 0.94, dc_note, transform=ax1.transAxes, fontsize=8.5, fontfamily="monospace", va="top", ha="left",
             bbox=dict(boxstyle="round,pad=0.5", facecolor="#1e272e", edgecolor="#ff4757", alpha=0.9, lw=1.0))

    # Bottom panel: Flattened Residual Spectrum
    ax2.plot(freqs_mhz, flat_db, color="#1dd1a1", lw=1.2, label=r"Flattened Passband: $P_{raw} - P_{base}$ (dB)")
    ax2.axhline(0, color="#8b949e", lw=1.0, ls="--", alpha=0.7)
    ax2.axvline(fc_mhz, color="#ff4757", lw=1.5, ls="--", alpha=0.7)
    ax2.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00d2d3", lw=1.5, ls=":", alpha=0.8)
    ax2.axvspan(HYDROGEN_REST_FREQ_MHZ - hi_half_mhz, HYDROGEN_REST_FREQ_MHZ + hi_half_mhz, color="#10ac84", alpha=0.15)
    ax2.axvspan(freqs_mhz[0], f_pb_min, color="#57606f", alpha=0.35)
    ax2.axvspan(f_pb_max, freqs_mhz[-1], color="#57606f", alpha=0.35)

    # Focus bottom y-limits strictly on the usable passband residuals
    pb_flat = flat_db[edge_bins : -edge_bins]
    pb_min = float(np.min(pb_flat))
    pb_max = float(np.max(pb_flat))
    y_low = min(-0.15, pb_min * 1.35)
    y_high = max(0.25, pb_max * 1.35)
    ax2.set_ylim(max(-0.4, y_low), min(0.6, y_high))

    ax2.set_xlabel("Frequency (MHz)", fontsize=11, fontweight="bold", labelpad=8)
    ax2.set_ylabel("Residual (dB)", fontsize=11, fontweight="bold", labelpad=8)
    ax2.grid(True)
    ax2.legend(loc="upper right", framealpha=0.9, fontsize=8.5)

    plt.tight_layout()
    safe_savefig(fig, output_path, dpi=200, bbox_inches="tight")
    plt.close()


def plot_doppler_profile(
    vel_kms: np.ndarray,
    calibrated_k: np.ndarray,
    metrics: Dict[str, Any],
    config: AstroConfig,
    output_path: str,
):
    """
    Plot 3: Doppler Radial Velocity emission profile focusing on galactic structure.
    """
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=200)

    mask = (vel_kms >= -250) & (vel_kms <= 250)
    v = vel_kms[mask]
    k = calibrated_k[mask]

    ax.plot(v, k, color="#48dbfb", lw=2.0, label="Observed HI Velocity Profile")
    ax.fill_between(v, 0, np.clip(k, 0, None), color="#48dbfb", alpha=0.2)
    ax.axhline(0, color="#8b949e", lw=1.0, ls="--", alpha=0.5)
    ax.axvline(0, color="#feca57", lw=1.5, ls="--", label="Local Standard / Rest Frame (0 km/s)")

    gauss = metrics.get("gauss_fit", {})
    if gauss.get("success", False):
        fit_v = gauss["fitted_curve"][mask]
        ax.plot(v, fit_v, color="#ff6b6b", lw=2.0, ls="-.", label=f"Gaussian Profile (v₀ = {metrics['peak_vel_kms']:+.1f} km/s)")

    # Mark Peak
    ax.plot(metrics["peak_vel_kms"], metrics["peak_temp_k"], "ro", markersize=8, markeredgecolor="white", label="Emission Centroid")

    ax.set_xlabel("Topocentric Radial Velocity $v_{LSR}$ (km/s)", fontsize=11, fontweight="bold", labelpad=8)
    ax.set_ylabel(r"Antenna Temperature $T_A$ (Kelvin)", fontsize=11, fontweight="bold", labelpad=8)
    ax.set_xlim(-250, 250)
    ax.set_ylim(max(-1.0, np.min(k) - 0.5), np.max(k) * 1.35 + 1.0)
    ax.grid(True)
    ax.legend(loc="upper right", framealpha=0.9, fontsize=9.5)

    vel_notes = (
        f"Kinematic Analysis:\n"
        f"• Peak Velocity: {metrics['peak_vel_kms']:+.2f} km/s\n"
        f"• Velocity Dispersion σ: {metrics['sigma_kms']:.2f} km/s\n"
        f"• FWHM: {metrics['fwhm_kms']:.2f} km/s\n"
        f"• Integrated Area: {metrics['integrated_flux_k_kms']:.1f} K·km/s"
    )
    ax.text(0.02, 0.94, vel_notes, transform=ax.transAxes, fontsize=9, fontfamily="monospace", va="top", ha="left",
            bbox=dict(boxstyle="round,pad=0.5", facecolor="#1e272e", edgecolor="#48dbfb", alpha=0.9, lw=1.0))

    plt.title("Galactic Neutral Hydrogen Radial Velocity Profile", fontsize=13, fontweight="bold", pad=12, color="#ffffff")
    plt.tight_layout()
    safe_savefig(fig, output_path, dpi=200, bbox_inches="tight")
    plt.close()


def plot_waterfall(
    waterfall_matrix: np.ndarray,
    waterfall_times: np.ndarray,
    freqs_mhz: np.ndarray,
    baseline_power: np.ndarray,
    config: AstroConfig,
    output_path: str,
):
    """
    Plot 4: Calibrated Waterfall Spectrogram (Spectrogram over observation time).
    Proves temporal continuity of the Hydrogen line and reveals transient RFI.
    """
    fig, ax = plt.subplots(figsize=(12, 6), dpi=200)

    # Flatten each row using the common baseline model
    base_safe = np.maximum(baseline_power, 1e-12)
    # Ratio in dB: 10 * log10(waterfall / baseline)
    norm_waterfall_db = 10 * np.log10(np.maximum(waterfall_matrix / base_safe, 1e-6))

    # Zoom frequency span to +/- 600 kHz around HI line
    span_khz = 600.0
    f_min = HYDROGEN_REST_FREQ_MHZ - (span_khz / 1e3)
    f_max = HYDROGEN_REST_FREQ_MHZ + (span_khz / 1e3)
    
    col_mask = (freqs_mhz >= f_min) & (freqs_mhz <= f_max)
    waterfall_sub = norm_waterfall_db[:, col_mask]
    freqs_sub = freqs_mhz[col_mask]

    t_start = 0.0
    t_end = waterfall_times[-1] if len(waterfall_times) > 0 else 60.0

    # Color scaling based on percentiles
    vmin = np.percentile(waterfall_sub, 5)
    vmax = np.percentile(waterfall_sub, 99.5) + 0.3

    im = ax.imshow(
        waterfall_sub,
        aspect="auto",
        origin="lower",
        extent=[freqs_sub[0], freqs_sub[-1], t_start, t_end],
        cmap="magma",
        vmin=vmin,
        vmax=vmax,
    )

    # Rest frequency marker line
    ax.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00f2fe", lw=1.5, ls="--", alpha=0.8, label="HI Rest Freq (1420.406 MHz)")

    ax.set_xlabel("Frequency (MHz)", fontsize=11, fontweight="bold", labelpad=8)
    ax.set_ylabel("Elapsed Observation Time (seconds)", fontsize=11, fontweight="bold", labelpad=8)
    ax.set_title("Time-Frequency Waterfall (Calibrated Spectrogram)", fontsize=13, fontweight="bold", pad=12, color="#ffffff")

    cbar = plt.colorbar(im, ax=ax, pad=0.02)
    cbar.set_label("Relative Power above Baseline (dB)", fontsize=10, fontweight="bold")
    ax.legend(loc="upper right", framealpha=0.9, fontsize=9)

    plt.tight_layout()
    safe_savefig(fig, output_path, dpi=200, bbox_inches="tight")
    plt.close()


def plot_master_dashboard(
    freqs_mhz: np.ndarray,
    vel_kms: np.ndarray,
    raw_power: np.ndarray,
    baseline_power: np.ndarray,
    calibrated_k: np.ndarray,
    calibrated_db: np.ndarray,
    waterfall_matrix: np.ndarray,
    waterfall_times: np.ndarray,
    metrics: Dict[str, Any],
    config: AstroConfig,
    filename: str,
    output_path: str,
):
    """
    Plot 5: Master 4-Panel Executive Dashboard summarizing the complete
    radiotelescope observation and astrophysical results in one image.
    """
    fig = plt.figure(figsize=(16, 10), dpi=220)
    gs = GridSpec(2, 2, figure=fig, hspace=0.28, wspace=0.22, top=0.90, bottom=0.08, left=0.07, right=0.95)

    # Title Banner
    fc_mhz = config.center_freq_hz / 1e6
    fs_msps = config.sample_rate_hz / 1e6
    dur_s = waterfall_times[-1] if len(waterfall_times) > 0 else 0.0
    title_text = (
        f"RADIOTELESCOPE HYDROGEN LINE ANALYSIS DASHBOARD\n"
        f"File: {os.path.basename(filename)}  |  Duration: {dur_s:.1f}s ({dur_s/60:.2f} min)  |  "
        f"LO Freq: {fc_mhz:.4f} MHz  |  Rate: {fs_msps:.2f} MSPS  |  SNR: {metrics['snr']:.1f}σ"
    )
    fig.suptitle(title_text, fontsize=13, fontweight="bold", color="#ffffff")

    # Panel 1: Raw Bandpass & Baseline Model (Full Bandwidth)
    ax1 = fig.add_subplot(gs[0, 0])
    raw_db = 10 * np.log10(np.maximum(raw_power, 1e-12))
    base_db = 10 * np.log10(np.maximum(baseline_power, 1e-12))
    ax1.plot(freqs_mhz, raw_db, color="#54a0ff", lw=1.0, alpha=0.7, label="Raw Spectrum")
    ax1.plot(freqs_mhz, base_db, color="#ff9f43", lw=2.0, label="Baseline Model")
    ax1.axvline(fc_mhz, color="#ff4757", lw=1.5, ls="--", label="DC Notch (Excised)")
    ax1.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00d2d3", lw=1.2, ls=":", label="HI 21cm Line")
    ax1.set_title("(A) Full Bandpass & Instrumental Baseline", fontsize=11, fontweight="bold")
    ax1.set_xlabel("Frequency (MHz)", fontsize=9.5)
    ax1.set_ylabel("Power (dB)", fontsize=9.5)
    ax1.grid(True)
    ax1.legend(loc="upper right", fontsize=8, framealpha=0.85)

    # Panel 2: Calibrated Spectrum vs Frequency
    ax2 = fig.add_subplot(gs[0, 1])
    flat_db = raw_db - base_db
    hi_span = (freqs_mhz >= HYDROGEN_REST_FREQ_MHZ - 0.6) & (freqs_mhz <= HYDROGEN_REST_FREQ_MHZ + 0.6)
    f_hi_span = freqs_mhz[hi_span]
    db_hi_span = flat_db[hi_span]
    
    ax2.plot(f_hi_span, db_hi_span, color="#1dd1a1", lw=1.2, label="Calibrated Residuals (dB)")
    ax2.axhline(0, color="#8b949e", lw=0.8, ls="--", alpha=0.6)
    ax2.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00d2d3", lw=1.2, ls=":", label="HI 21cm Line")
    ax2.set_xlim(HYDROGEN_REST_FREQ_MHZ - 0.6, HYDROGEN_REST_FREQ_MHZ + 0.6)
    y_hi_min = min(-0.15, float(np.min(db_hi_span)) - 0.05)
    y_hi_max = max(0.25, float(np.max(db_hi_span)) * 1.35)
    ax2.set_ylim(y_hi_min, y_hi_max)
    ax2.set_title("(B) Calibrated Frequency Spectrum (HI Region)", fontsize=11, fontweight="bold")
    ax2.set_xlabel("Frequency (MHz)", fontsize=9.5)
    ax2.set_ylabel("Power above Baseline (dB)", fontsize=9.5)
    ax2.grid(True)
    ax2.legend(loc="upper right", fontsize=8, framealpha=0.85)

    # Panel 3: Zoomed Doppler Velocity Profile with Gaussian Fit
    ax3 = fig.add_subplot(gs[1, 0])
    v_mask = (vel_kms >= -180) & (vel_kms <= 180)
    v_z = vel_kms[v_mask]
    k_z = calibrated_k[v_mask]
    ax3.plot(v_z, k_z, color="#00f2fe", lw=2.0, label=r"$T_A(v)$")
    ax3.fill_between(v_z, 0, np.clip(k_z, 0, None), color="#00f2fe", alpha=0.18)
    ax3.axhline(0, color="#8b949e", lw=0.8, ls="--", alpha=0.5)
    ax3.axvline(0, color="#74b9ff", lw=1.2, ls=":", label="Rest Frame")

    gauss = metrics.get("gauss_fit", {})
    if gauss.get("success", False):
        ax3.plot(v_z, gauss["fitted_curve"][v_mask], color="#ff7675", lw=1.8, ls="--", label="Gaussian Model")

    ax3.plot(metrics["peak_vel_kms"], metrics["peak_temp_k"], "ro", markersize=6, label=f"Peak: {metrics['peak_vel_kms']:+.1f} km/s")
    ax3.set_xlim(-180, 180)
    ax3.set_ylim(min(-0.8, float(np.min(k_z)) - 0.5), max(2.5, float(np.max(k_z)) * 1.35))
    ax3.set_title("(C) Doppler Velocity & Line Fit", fontsize=11, fontweight="bold")
    ax3.set_xlabel("Radial Velocity (km/s)", fontsize=9.5)
    ax3.set_ylabel(r"Antenna Temp $T_A$ (K)", fontsize=9.5)
    ax3.grid(True)
    ax3.legend(loc="upper right", fontsize=8, framealpha=0.85)

    # Key metrics text box inside panel C
    box_txt = (
        f"Peak Freq: {metrics['peak_freq_mhz']:.4f} MHz\n"
        f"Recession: {metrics['peak_vel_kms']:+.1f} km/s\n"
        f"FWHM:      {metrics['fwhm_kms']:.1f} km/s\n"
        f"Flux W:    {metrics['integrated_flux_k_kms']:.1f} K·km/s\n"
        f"N_HI:      {metrics['column_density_cm2']:.2e} cm⁻²"
    )
    ax3.text(0.03, 0.94, box_txt, transform=ax3.transAxes, fontsize=8, fontfamily="monospace", va="top",
             bbox=dict(boxstyle="round,pad=0.4", facecolor="#1e272e", edgecolor="#00f2fe", alpha=0.9, lw=0.8))

    # Panel 4: Calibrated Waterfall Spectrogram
    ax4 = fig.add_subplot(gs[1, 1])
    base_safe = np.maximum(baseline_power, 1e-12)
    norm_wf = 10 * np.log10(np.maximum(waterfall_matrix / base_safe, 1e-6))
    
    col_mask = (freqs_mhz >= HYDROGEN_REST_FREQ_MHZ - 0.5) & (freqs_mhz <= HYDROGEN_REST_FREQ_MHZ + 0.5)
    wf_sub = norm_wf[:, col_mask]
    f_sub = freqs_mhz[col_mask]
    
    t_end = waterfall_times[-1] if len(waterfall_times) > 0 else 60.0
    vmin = np.percentile(wf_sub, 5)
    vmax = np.percentile(wf_sub, 99.5) + 0.3
    
    im = ax4.imshow(
        wf_sub, aspect="auto", origin="lower",
        extent=[f_sub[0], f_sub[-1], 0, t_end],
        cmap="magma", vmin=vmin, vmax=vmax,
    )
    ax4.axvline(HYDROGEN_REST_FREQ_MHZ, color="#00f2fe", lw=1.2, ls="--", alpha=0.7)
    ax4.set_title("(D) Time-Frequency Waterfall Spectrogram", fontsize=11, fontweight="bold")
    ax4.set_xlabel("Frequency (MHz)", fontsize=9.5)
    ax4.set_ylabel("Elapsed Time (s)", fontsize=9.5)
    cbar = plt.colorbar(im, ax=ax4, pad=0.02)
    cbar.set_label("Relative dB", fontsize=8.5)

    safe_savefig(fig, output_path, dpi=220, bbox_inches="tight")
    plt.close()
