"""
Astro Radio Telescope Hydrogen Line Analysis Pipeline
Configuration and Astrophysical Constants
"""

import dataclasses
from typing import Optional

# Fundamental Physical and Astronomical Constants
HYDROGEN_REST_FREQ_HZ = 1420405751.768  # Exact HI 21cm line rest frequency (Hz)
HYDROGEN_REST_FREQ_MHZ = HYDROGEN_REST_FREQ_HZ / 1e6  # ~1420.405752 MHz
SPEED_OF_LIGHT_KMS = 299792.458  # Speed of light in vacuum (km/s)
SPEED_OF_LIGHT_MS = SPEED_OF_LIGHT_KMS * 1e3

# Neutral Hydrogen Column Density Conversion Factor (optically thin approximation)
# N_HI = 1.8224e18 * integral(T_B * dv) in [atoms * cm^-2]
# where T_B is brightness temperature in K, dv is velocity in km/s
HI_COLUMN_DENSITY_FACTOR = 1.8224e18


@dataclasses.dataclass
class AstroConfig:
    """Configuration parameters for HI line processing."""
    # SDR & Hardware Settings
    center_freq_hz: float = 1420.5e6       # SDR LO Tuned Frequency (e.g. 1420.5 MHz, slightly offcenter)
    sample_rate_hz: float = 2.4e6          # SDR Sampling rate (e.g. 2.4 MSPS)
    system_temp_k: float = 100.0           # Assumed system noise temperature for scaling (Kelvin)
    
    # DSP Settings
    fft_size: int = 8192                   # Number of FFT bins (gives ~293 Hz / 0.062 km/s resolution at 2.4 MSPS)
    window: str = "blackman"               # Window function ('blackman', 'hann', 'blackmanharris')
    chunk_blocks: int = 512                # Number of FFT blocks per streaming batch (~16 MB per read)
    waterfall_dt_s: float = 1.0            # Time integration per waterfall slice (seconds)
    max_duration_s: Optional[float] = None # Optional maximum seconds to process (None = entire file)
    
    # DC Excision & RFI Settings
    dc_clear_hz: float = 5000.0            # Bandwidth around 0 Hz IF to excise DC spike (+/- 2.5 kHz)
    rfi_threshold_mad: float = 4.5         # Outlier detection threshold for narrow RFI spikes (MAD units)
    
    # Baseline Estimation Settings
    baseline_method: str = "als"           # 'als' (Whittaker smoothing), 'poly' (iterative polynomial), or 'filter'
    passband_edge_fraction: float = 0.065  # Fraction of bandwidth on each edge to treat as SDR filter skirts (6.5%)
    hi_protection_half_width_khz: float = 90.0   # Protection zone around HI rest freq (+/- 90 kHz = +/- 19 km/s)
    filter_smoothing_sigma_khz: float = 35.0     # Gaussian baseline smoothing sigma in kHz
    als_lambda: float = 2000.0             # ALS smoothness parameter tuned for cable reflection standing waves
    als_p: float = 0.02                    # ALS asymmetry parameter
    poly_degree: int = 10                  # Polynomial baseline fit degree (models 700 kHz standing waves)
    local_baseline_order: int = 1          # Order of local polynomial refinement in HI window (1 = linear detrend)
    line_free_velocity_kms: float = 40.0   # Radial velocity threshold for line-free channels (|v| > 40 km/s)
    
    # Display & Output Settings
    output_dir: str = "output"
    verbose: bool = True
