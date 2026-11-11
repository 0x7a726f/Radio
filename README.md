# Radio

A high-performance Software-Defined Radio (SDR) and Digital Signal Processing (DSP) observational pipeline for **21cm Neutral Hydrogen (H I) Line** radio astronomy ($\sim 1420.40575\text{ MHz}$).

Optimized for high-throughput streaming analysis of raw complex signed 16-bit integer (**CS16**) I/Q observations collected with amateur and professional radio telescopes using SDR receivers (e.g., RTL-SDR Blog V4/V3, Airspy, HackRF, SDRplay).

---

## Key Features

- **Blazing Fast Multithreaded Streaming Engine**:
  - Leverages multithreaded `scipy.fft` with zero-copy NumPy buffers.
  - Processes multi-gigabyte recordings at **> 100–180 MB/s** (25–45 MSamples/s), completing typical multi-minute observations in seconds.
  - Streamed chunk-based I/O keeps RAM consumption strictly under **100 MB** regardless of recording size.
- **Off-Center DC Offset Combating**:
  - Direct conversion / Zero-IF SDRs suffer from central Local Oscillator (LO) leakage spikes.
  - Full support for off-center tuning (e.g., `--fc 1420.5MHz` for a 2.4 MSPS RTL-SDR passband).
  - The pipeline automatically detects the LO offset, validates separation from the H I rest line, and smoothly excises the residual DC spike.
- **Advanced Two-Stage Baseline Modeling (Combats Coaxial Standing Waves)**:
  - Radio telescope front-ends suffer from $\sim 700\text{ kHz}$ sinusoidal standing wave ripples caused by impedance reflections between the LNA and SDR coaxial cable.
  - **Stage 1 (Passband Harmonic Fit)**: Adaptive Whittaker smoothing (ALS) or iterative Chebyshev polynomials fit the standing wave across the passband in log-space while downweighting digital filter skirts (outer 6.5%) and protecting the narrow H I window.
  - **Stage 2 (Local Astronomical Zeroing)**: Performs local baseline detrending using line-free reference channels ($|v| > 40\text{ km/s}$), guaranteeing an exact **$0.000\text{ K}$ / $0.000\text{ dB}$ flat baseline** beneath the line with noise floor $\sigma \approx 0.5\text{ K}$.
- **Kinematic & Physical Analysis**:
  - Topocentric Doppler radial velocity mapping ($v = c \cdot \frac{f_0 - f}{f_0}$).
  - Non-linear Gaussian emission profile fitting to extract peak velocity ($v_\text{peak}$), FWHM ($\Delta v_\text{FWHM}$), and velocity dispersion ($\sigma_v$).
  - Integrated flux ($W = \int T_A dv$) and neutral hydrogen column density determination ($N_{HI} = 1.8224 \times 10^{18} W\text{ atoms/cm}^2$).
- **Publication-Grade Visualizations**:
  - Generates publication-ready figures including calibrated dual-axis spectra, raw bandpass vs. baseline models, Doppler velocity profiles, time-frequency waterfall spectrograms, and unified 4-panel executive dashboards.
- **Multi-Format Structured Exports**:
  - Calibrated CSV data tables, human-readable summary text reports, and machine-readable JSON metrics.

---

## Directory Structure

```text
Radio/
├── Astro/                           # 21cm Neutral Hydrogen Line Radio Astronomy Pipeline
│   ├── astro_analyzer.py            # Core analysis runner & CLI engine
│   ├── config.py                    # Astronomy pipeline configuration & physical constants
│   ├── dsp.py                       # Multithreaded streaming FFT, PSD, & baseline fitting
│   ├── exporter.py                  # Data export (CSV, JSON, ASCII reports)
│   ├── run.py                       # Interactive CLI wizard launcher
│   ├── visualizer.py                # Spectral plots, waterfalls, and observation dashboard
│   ├── output/                      # Sample observation reports and visualizations
│   │   ├── 1_hi_spectrum_calibrated.png
│   │   ├── 2_hi_raw_and_baseline.png
│   │   ├── 3_hi_doppler_velocity.png
│   │   ├── 4_hi_waterfall_time_frequency.png
│   │   ├── 5_hi_master_dashboard.png
│   │   ├── analysis_report.json
│   │   ├── analysis_report.txt
│   │   └── calibrated_spectrum.csv
│   └── README.md                    # Detailed astronomy pipeline documentation
│
└── .gitignore                       # Git rules ignoring local SDR suites & large recordings
```

---

## Quick Start

### Prerequisites

Ensure you have Python 3.8+ installed with the following packages:

```bash
pip install numpy scipy matplotlib
```

### 1. Interactive Mode (Wizard)

Run the wizard launcher directly without arguments:

```bash
python Astro/run.py
```

The interactive wizard will:
1. Scan the repository workspace for available `.cs16` recordings.
2. Prompt for your SDR Center LO frequency (default: `1420.5 MHz`).
3. Prompt for SDR Sample Rate (default: `2.4 MSPS`, ideal for standard RTL-SDR bandwidth).
4. Prompt for analysis duration (e.g. 120 seconds or full observation).

---

### 2. Command-Line (CLI) Mode

Run non-interactively with CLI arguments:

```bash
python Astro/astro_analyzer.py <path_to_file.cs16> [options]
```

#### Example Commands

- **Analyze a 2.4 MSPS RTL-SDR recording centered at 1420.5 MHz**:
  ```bash
  python Astro/astro_analyzer.py observation.cs16 --fc 1420.5MHz --fs 2.4MSPS
  ```

- **Process the first 2 minutes (120 seconds) of an observation**:
  ```bash
  python Astro/astro_analyzer.py observation.cs16 --fc 1420.5MHz --fs 2.4MSPS --duration 120
  ```

- **Use Asymmetric Least Squares (ALS) baseline modeling**:
  ```bash
  python Astro/astro_analyzer.py observation.cs16 --fc 1420.5MHz --baseline als
  ```

- **High-resolution 16384-channel FFT**:
  ```bash
  python Astro/astro_analyzer.py observation.cs16 --fc 1420.5MHz --fft-size 16384
  ```

---

## Command-Line Arguments Reference

| Option | Flag | Description | Default |
| :--- | :--- | :--- | :--- |
| `file` | Positional | Path to the raw CS16 file | Interactive Wizard |
| `--fc` | `--center-freq` | SDR center LO frequency (`1420.5`, `1420.5MHz`, etc.) | `1420.5 MHz` |
| `--fs` | `--sample-rate` | SDR sampling rate (`2.4`, `2.4MSPS`, `2400000`, etc.) | `2.4 MSPS` |
| `-n` | `--fft-size` | Number of FFT bins (frequency channels) | `8192` |
| `-t` | `--duration` | Seconds to process (omit to process entire recording) | Full file |
| `--baseline` | | Baseline fit algorithm (`ripple_filter`, `als`, `poly`) | `ripple_filter` |
| `--tsys` | | System noise temperature in Kelvin for $T_A$ scaling | `100.0 K` |
| `-o` | `--output-dir`| Custom directory to save output plots and reports | `Astro/output` |

---

## Output Artifacts

Upon completion, all generated plots and data files are saved into `Astro/output/`:

| Filename | Description |
| :--- | :--- |
| `1_hi_spectrum_calibrated.png` | Primary calibrated Hydrogen Line spectrum (velocity & frequency axes, Gaussian fit, HUD card) |
| `2_hi_raw_and_baseline.png` | Raw SDR bandpass, baseline curve, DC spike excision marker, and protected bandpass zone |
| `3_hi_doppler_velocity.png` | Doppler radial velocity profile showing galactic kinematics |
| `4_hi_waterfall_time_frequency.png` | Time-frequency waterfall spectrogram demonstrating signal stability |
| `5_hi_master_dashboard.png` | Unified 4-panel executive observation dashboard |
| `calibrated_spectrum.csv` | Full CSV dataset of frequency, velocity, raw dB, baseline dB, calibrated dB, and Antenna Temp (K) |
| `analysis_report.txt` | Human-readable scientific summary report |
| `analysis_report.json` | Machine-readable JSON metrics and metadata |
