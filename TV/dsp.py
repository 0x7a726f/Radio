"""
Digital Signal Processing (DSP) & RF Spectral Analysis module for TV SDR tuner.
Calculates real-time Fast Fourier Transforms (FFT), Power Spectral Density (PSD),
Received Signal Strength Indicator (RSSI), and Signal-to-Noise Ratio (SNR).
"""

from dataclasses import dataclass
from typing import Tuple
import numpy as np

@dataclass
class RfMetrics:
    center_freq_mhz: float
    sample_rate_mhz: float
    rssi_db: float          # Average received signal power (dBFS)
    peak_power_db: float    # Peak power spectral density bin (dBFS)
    noise_floor_db: float   # Estimated out-of-band noise floor (dBFS)
    snr_db: float           # Signal-to-Noise Ratio (dB)
    is_locked: bool         # Whether an active DVB-T carrier is detected
    quality_pct: float      # Signal quality score 0-100%

def compute_psd(
    iq: np.ndarray,
    sample_rate_hz: float,
    center_freq_hz: float,
    fft_size: int = 2048,
    window: str = "hann"
) -> Tuple[np.ndarray, np.ndarray, RfMetrics]:
    """
    Computes averaged Power Spectral Density (PSD) and RF reception metrics.
    
    Returns:
        freqs_mhz: Array of frequency bins (MHz)
        psd_db: Array of spectral power density values (dBFS)
        metrics: RfMetrics summary
    """
    if len(iq) < fft_size:
        # Pad if short
        iq = np.pad(iq, (0, fft_size - len(iq)))

    # Windowing
    if window == "blackman":
        win = np.blackman(fft_size)
    else:
        win = np.hanning(fft_size)
    win_norm = np.sum(win**2)

    # Number of segments to average
    n_seg = len(iq) // fft_size
    accum_power = np.zeros(fft_size, dtype=np.float64)

    for i in range(n_seg):
        chunk = iq[i * fft_size : (i + 1) * fft_size] * win
        fft_res = np.fft.fftshift(np.fft.fft(chunk))
        power = (np.abs(fft_res)**2) / (fft_size * win_norm + 1e-12)
        accum_power += power

    avg_power = accum_power / max(1, n_seg)
    psd_db = 10.0 * np.log10(avg_power + 1e-12)

    # Frequency axis in MHz
    freqs = np.fft.fftshift(np.fft.fftfreq(fft_size, d=1.0 / sample_rate_hz))
    freqs_mhz = (freqs + center_freq_hz) / 1e6

    # RF Metrics computation
    total_power = np.mean(np.abs(iq)**2) + 1e-12
    rssi_db = float(10.0 * np.log10(total_power))
    peak_power_db = float(np.max(psd_db))

    # Noise floor estimate using 15th percentile of spectrum
    noise_floor_db = float(np.percentile(psd_db, 15))
    # Median in-band power
    in_band_median = float(np.median(psd_db))
    snr_db = max(0.0, in_band_median - noise_floor_db)

    # Carrier detection: DVB-T produces a pronounced elevated table-top spectrum
    # typically 8-25 dB above thermal noise floor
    is_locked = (snr_db >= 5.0) and (rssi_db > -60.0)
    
    # Quality percentage calculation (clamped 0 - 100%)
    # 0 dB SNR -> 0%, 25 dB SNR -> 100%
    quality_pct = min(100.0, max(0.0, (snr_db / 22.0) * 100.0))

    metrics = RfMetrics(
        center_freq_mhz=center_freq_hz / 1e6,
        sample_rate_mhz=sample_rate_hz / 1e6,
        rssi_db=rssi_db,
        peak_power_db=peak_power_db,
        noise_floor_db=noise_floor_db,
        snr_db=snr_db,
        is_locked=is_locked,
        quality_pct=quality_pct
    )

    return freqs_mhz, psd_db, metrics
