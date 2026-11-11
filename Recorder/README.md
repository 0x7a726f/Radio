# Recorder - Hydrogen Line Live SDR Receiver & CS16 Capture App

A real-time Software-Defined Radio (SDR) live receiver and lossless I/Q recorder styled after **SatDump**. Engineered for astronomical observations of the **21cm Neutral Hydrogen (H I) Line** ($\sim 1420.40575\text{ MHz}$).

Acquires raw RF from RTL-SDR dongles (e.g., RTL-SDR Blog V4, V3, and compatible RTL2832U devices) and streams directly to disk in the standardized **CS16** format (complex signed 16-bit integers, interleaved I/Q, 4 bytes per sample), ready for immediate ingestion by the [Analyzer](../Analyzer).

---

## Key Features

- **SatDump-Style Control Panels**:
  - **▼ Device**:
    - Hardware selector with auto-detection of device strings (e.g. `RTLSDRBlog Blog V4 #00000001`).
    - Refresh button (`⟳`) to re-scan USB buses and detect active/released devices.
    - Large digital frequency display (`001.420.500.000 Hz`) with click-to-edit frequency prompt.
    - Quick presets for Hydrogen Line observations (`1420.5 MHz` LO offset +94k, `1420.406 MHz` rest center, `1420.2 MHz`).
    - Configurable sample rate (`1.024`, `2.048`, `2.4`, `2.56`, `3.2 Msps`).
    - LNA Gain slider (`0.0` to `49.6 dB`) with AGC toggle.
    - Software-switchable **Bias-Tee (4.5V)** toggle to power external LNAs (such as the Nooelec Sawbird+ H1) directly through coaxial feedlines.
    - Automatic process conflict detection (notifies if SatDump, SDR++, or SDRangel is currently holding the USB interface).
  - **▼ FFT Display**:
    - Selectable FFT size (`2048`, `4096`, `8192`, `16384`).
    - Dynamic dB range scaling (`FFT Max` and `FFT Min` sliders).
    - Adjustable exponential averaging (`0%` to `95%`).
    - Color palettes (`Classic [Youssef Touil]`, `Viridis`, `Turbo`, `Inferno`, `Magma`).
    - Toggleable waterfall and frequency grid overlay.
  - **▼ Recording**:
    - Standardized **CS16** output format (2 bytes signed I + 2 bytes signed Q).
    - Dynamic live file size counter (`0.00 MB` -> `1.20 GB`).
    - Partition free disk space monitor and estimated recording time remaining.
    - Status indicators: `IDLE` (red) / `RECORDING` (green with elapsed timer).
    - Writes companion `.json` metadata sidecar with observation parameters (center LO, sample rate, timestamp, gain, bias-tee, duration).
    - **One-click "Open in Offline Analyzer" button** to instantly launch the offline pipeline on newly captured data.
- **Spectrum & Waterfall Visualizer**:
  - Top: Live RF power spectrum curve with dB axis, peak HUD readout, DC LO center marker, and a prominent red marker at the exact astronomical Hydrogen line rest frequency ($1420.40575\text{ MHz}$).
  - Bottom: 2D rolling waterfall spectrogram rendered at up to 60 FPS using Pillow blitting.
- **Virtual SDR Simulation Mode**:
  - If no hardware dongle is connected or the USB port is temporarily occupied by an external application, the recorder seamlessly falls back to a realistic simulated RF passband featuring background thermal noise, coaxial standing wave ripples, and a synthetic 21cm Hydrogen Line peak.

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
