# Recorder - Hydrogen Line Live SDR Receiver & CS16 Capture App

A real-time Software-Defined Radio (SDR) live receiver and lossless I/Q recorder styled after **SatDump**. Engineered for astronomical observations of the **21cm Neutral Hydrogen (H I) Line** ($\sim 1420.40575\text{ MHz}$) and general wideband RF monitoring.

Acquires raw RF from RTL-SDR dongles (e.g., RTL-SDR Blog V4, V3, and compatible RTL2832U devices) and streams directly to disk in the standardized **CS16** format (complex signed 16-bit integers, interleaved I/Q, 4 bytes per sample), accompanied by a comprehensive observation parameters `.txt` file and structured `.json` metadata for instant ingestion by the [Analyzer](../Analyzer).

---

## Key Features

- **Ultra-Fast & Responsive GUI**:
  - In-place memory-buffered waterfall rendering using `PhotoImage.paste()` (< 0.15 ms/frame).
  - Persistent canvas line coordinate updates (`canvas.coords()`) eliminating memory churn and event loop freezing.
  - Smooth 30–60 FPS spectrum and rolling waterfall display.
- **Universal Multi-Frequency Tuning**:
  - **Direct Numeric Entry**: Type any frequency into the `Tune` entry box (e.g. `89.2`, `1420.5M`, `1420405752`, `433.92`, `1090`) and press Enter.
  - **Quick Step Buttons**: Immediate delta increments/decrements: `-1M` `+1M` | `-100k` `+100k` | `-10k` `+10k`.
  - **Digital Segmented Readout**: `001.420.500.000 Hz` with interactive mouse-wheel stepping.
  - **Band & Channel Presets**:
    - `H I 1420.5M (LO +94k)`
    - `H I 1420.406M (Rest Line)`
    - `H I 1420.2M (SatDump)`
    - `FM 89.2 MHz (Broadcast)`
    - `FM 100.0 MHz (Broadcast)`
    - `Airband 121.5 MHz (ATIS)`
    - `Ham 144.8 MHz (2m APRS)`
    - `ISM 433.92 MHz (Telemetry)`
    - `ADS-B 1090.0 MHz (Aviation)`
- **▼ Device Controls**:
  - Hardware selector with auto-detection of device strings (e.g. `RTLSDRBlog Blog V4 #00000001`).
  - Refresh button (`⟳`) to re-scan USB buses and detect active/released devices.
  - Configurable sample rate (`1.024`, `2.048`, `2.4`, `2.56`, `3.2 Msps`).
  - LNA Gain slider (`0.0` to `49.6 dB`) with AGC toggle.
  - Software-switchable **Bias-Tee (4.5V)** toggle to power external LNAs (such as the Nooelec Sawbird+ H1) directly through coaxial feedlines.
- **▼ Accompanied Observation Parameters (`.txt` & `.json`)**:
  - Every capture automatically generates:
    1. `<filename>.cs16`: Raw lossless 16-bit I/Q binary stream.
    2. `<filename>.txt`: Detailed human-readable observation report containing exact center LO frequency, sample rate, calculated LO offset from the HI rest line in kHz, DC spike safety clearance, gain, duration, total samples, and copy-paste analysis commands.
    3. `<filename>.json`: Machine-readable metadata sidecar.
  - **Auto-Ingestion in Analyzer**: The [Analyzer](../Analyzer) automatically reads the accompanied parameter file and configures the pipeline without requiring manual CLI arguments!
- **▼ FFT Display**:
  - Selectable FFT size (`2048`, `4096`, `8192`, `16384`).
  - Dynamic dB range scaling (`FFT Max` and `FFT Min` sliders).
  - Adjustable exponential averaging (`0%` to `95%`).
  - Color palettes (`Classic [Youssef Touil]`, `Viridis`, `Turbo`, `Inferno`, `Magma`).
  - Toggleable waterfall and frequency grid overlay.
- **▼ Recording & Live Stats**:
  - Standardized **CS16** output format (2 bytes signed I + 2 bytes signed Q).
  - Dynamic live file size counter (`0.00 MB` -> `1.20 GB`).
  - Partition free disk space monitor and estimated recording time remaining.
  - Status indicators: `IDLE` (red) / `RECORDING` (green with elapsed timer).
  - **One-click "Open in Offline Analyzer" button** to instantly launch the offline pipeline on newly captured data.
- **Multi-Frequency Virtual SDR Mode**:
  - If no physical dongle is connected, the app provides realistic RF simulations for all bands (Hydrogen line with 710 kHz coax ripple at 1420 MHz, FM broadcast carriers with stereo modulation at 88–108 MHz, AM airband at 118–137 MHz, etc.) so the entire interface can be tested anywhere.

---

## Quick Start

### 1. Launch GUI (Desktop Application)

Double-click `Recorder/launch.bat` or run from terminal:

```bash
python Recorder/run.py
```

### 2. Headless CLI Recording (Automated Captures)

Record directly from the command line without opening the GUI:

```bash
# Record for 120 seconds at 1420.5 MHz (2.4 MSPS) with Bias-Tee enabled:
python Recorder/run.py --cli --duration 120 --fc 1420.5M --fs 2.4M --gain 25.4 --bias-tee
```

#### CLI Options Reference

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--cli` | Run in headless recording mode | Disabled (GUI mode) |
| `-t`, `--duration` | Recording duration in seconds | `60.0` |
| `--fc` | SDR center LO frequency (`1420.5M`, `1420500000`, etc.) | `1420.5M` |
| `--fs` | SDR sample rate (`2.4M`, `2.4MSPS`, `2400000`, etc.) | `2.4M` |
| `--gain` | Tuner LNA gain in dB | `25.4` |
| `--bias-tee` | Enable 4.5V software Bias-Tee | Disabled |
| `-o`, `--out` | Custom output `.cs16` file path | `recordings/hi_capture_<timestamp>_<freq>_<rate>.cs16` |

---

## CS16 Format Specification

- **Type**: Complex Signed 16-bit Integer (`int16`, little-endian `<i2`).
- **Structure**: Interleaved `[I0, Q0, I1, Q1, I2, Q2, ...]`.
- **Byte Width**: 4 bytes per complex sample pair.
- **Data Rate**: At 2.4 MSPS, recording throughput is exactly $9.6\text{ MB/s}$ ($\sim 576\text{ MB/min}$).
- **Compatibility**: 100% bit-compatible with SatDump CS16 recordings, SDR++, GNU Radio, and our [Analyzer](../Analyzer) suite.
