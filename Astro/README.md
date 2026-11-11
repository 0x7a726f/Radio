# Astro - High-Speed Radio Telescope Hydrogen Line (21cm) Analysis Pipeline

A high-performance radio astronomy analysis suite designed to process raw **CS16** (complex signed 16-bit integer I/Q) observations from amateur and professional radio telescopes centered around the **21cm Neutral Hydrogen (H I) Line** ($\sim 1420.40575\text{ MHz}$).

---

## Key Features

- **Blazing Fast Multithreaded Streaming Engine**:
  - Leverages multithreaded `scipy.fft` with zero-copy NumPy buffers.
  - Processes multi-gigabyte recordings at **> 100-180 MB/s** (25–45 MSamples/s), completing typical multi-minute observations in seconds.
  - Streamed chunk-based I/O keeps RAM usage strictly under **100 MB** regardless of file size.
- **Off-Center DC Offset Combating**:
  - Direct conversion / Zero-IF SDRs suffer from central LO leakage spikes.
  - Easily configure the actual SDR center frequency via CLI (`--fc 1420.5MHz`) or the interactive prompt.
  - The pipeline automatically checks LO offset, confirms separation from the HI line, and smoothly excises the residual DC spike.
- **Advanced Two-Stage Baseline Modeling (Combats Coaxial Cable Standing Waves)**:
  - Radio telescope front-ends suffer from $\sim 700\text{ kHz}$ sinusoidal standing wave ripples due to impedance reflections between the LNA and SDR coax cable.
  - **Stage 1 (Passband Harmonic Fit)**: Adaptive Whittaker smoothing (ALS) or iterative Chebyshev polynomials fit the standing wave across the passband in log-space while downweighting digital filter skirts (outer 6.5%) and protecting the narrow HI window.
  - **Stage 2 (Local Astronomical Zeroing)**: Performs local baseline detrending using line-free reference channels ($|v| > 40\text{ km/s}$), guaranteeing an exact **$0.000\text{ K}$ / $0.000\text{ dB}$ flat baseline** beneath the line with noise floor $\sigma \approx 0.5\text{ K}$.
- **Kinematic & Physical Analysis**:
  - Topocentric Doppler radial velocity mapping ($v = c \cdot \frac{f_0 - f}{f_0}$).
  - Gaussian emission profile fitting to determine peak velocity ($v_\text{peak}$), FWHM ($\Delta v_\text{FWHM}$), and velocity dispersion ($\sigma_v$).
  - Integrated flux ($W = \int T_A dv$) and neutral hydrogen column density ($N_{HI} = 1.8224 \times 10^{18} W\text{ atoms/cm}^2$).
- **Publication-Grade Visualizations (in `output/`)**:
  - `1_hi_spectrum_calibrated.png`: Zoomed, clear calibrated HI spectrum with dual axes (Doppler velocity & sky frequency), Gaussian fit, and HUD info box.
  - `2_hi_raw_and_baseline.png`: Full SDR bandpass showing raw power, modeled baseline, DC excision marker, and protected HI region.
  - `3_hi_doppler_velocity.png`: Detailed Doppler velocity emission profile.
  - `4_hi_waterfall_time_frequency.png`: Time-frequency spectrogram proving signal continuity over the entire observation and confirming the absence of transient RFI.
  - `5_hi_master_dashboard.png`: Unified 4-panel executive observation dashboard.
- **Exported Formats**:
  - Tabular calibrated CSV (`calibrated_spectrum.csv`).
  - Formatted text summary report (`analysis_report.txt`).
  - Structured JSON metrics (`analysis_report.json`).

---

## Usage

### 1. Interactive Mode (Wizard)
Simply run the script with no arguments:
```bash
python Astro/run.py
```
Or inside the `Astro` folder:
```bash
cd Astro
python run.py
```
The wizard will:
1. Scan the repository workspace for available `.cs16` files and let you select one.
2. Prompt for your SDR Center LO frequency (default: `1420.5 MHz`).
3. Prompt for Sample Rate (default: `2.4 MSPS`).
4. Prompt for analysis duration (e.g. 120 seconds or full observation).

---

### 2. Command-Line (CLI) Mode
Run fully automated with flags:
```bash
python Astro/astro_analyzer.py <path_to_file.cs16> [options]
```

#### Example Commands:

- **Analyze a 2.4 MSPS file centered at 1420.5 MHz**:
  ```bash
  python Astro/astro_analyzer.py SatDump/Astro/file5.cs16 --fc 1420.5MHz --fs 2.4MSPS
  ```

- **Analyze the first 2 minutes (120 seconds) of an observation**:
  ```bash
  python Astro/astro_analyzer.py file5.cs16 --fc 1420.5MHz --fs 2.4MSPS --duration 120
  ```

- **Use Asymmetric Least Squares (ALS) baseline modeling**:
  ```bash
  python Astro/astro_analyzer.py file5.cs16 --fc 1420.5MHz --baseline als
  ```

- **High-resolution 16384 FFT channels**:
  ```bash
  python Astro/astro_analyzer.py file5.cs16 --fc 1420.5MHz --fft-size 16384
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

## Generated Files in `Astro/output/`

| Filename | Description |
| :--- | :--- |
| `1_hi_spectrum_calibrated.png` | Primary calibrated Hydrogen Line spectrum (velocity & frequency axes, Gaussian fit, HUD card) |
| `2_hi_raw_and_baseline.png` | Raw SDR bandpass, baseline curve, DC spike excision marker, and protected bandpass zone |
| `3_hi_doppler_velocity.png` | Doppler radial velocity profile showing galactic kinematics |
| `4_hi_waterfall_time_frequency.png`| Time-frequency waterfall spectrogram demonstrating signal stability |
| `5_hi_master_dashboard.png` | Unified 4-panel executive observation dashboard |
| `calibrated_spectrum.csv` | Full CSV dataset of frequency, velocity, raw dB, baseline dB, calibrated dB, and Antenna Temp (K) |
| `analysis_report.txt` | Human-readable scientific summary report |
| `analysis_report.json` | Machine-readable JSON metrics and metadata |
