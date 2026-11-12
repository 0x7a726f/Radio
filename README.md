# Radio

A complete high-performance Software-Defined Radio (SDR) and Digital Signal Processing (DSP) suite for **21cm Neutral Hydrogen (H I) Line** radio astronomy ($\sim 1420.40575\text{ MHz}$).

Built specifically for amateur and professional radio astronomers using SDR receivers (e.g., RTL-SDR Blog V4, V3, Airspy, HackRF, SDRplay). The repository provides an integrated end-to-end workflow: real-time RF acquisition, spectrum and waterfall monitoring, lossless **CS16** binary recording, and high-throughput offline astrophysical analysis.

---

## Architecture Overview

```text
Radio/
├── cpp_suite/                       # UNIFIED C++ Radio Astronomy Desktop & CLI Suite (Record + Analyze)
│   ├── bin/                         # Standalone binaries (radio_suite.exe, radio_suite_cli.exe, DLLs)
│   ├── include/                     # Headers (ui_record_panel.h, ui_analyze_panel.h, app_state.h...)
│   ├── src/                         # Implementations (unified GUI with bridge, CLI, DSP...)
│   ├── tests/                       # Automated test harness & 115 assertions (run_tests.bat)
│   ├── CMakeLists.txt               # Unified CMake configuration
│   ├── build_mingw.bat              # 1-click build script (MinGW GCC 16.2.0 -O3 -mavx2)
│   ├── build_msvc.bat               # 1-click build script (MSVC)
│   └── README.md                    # Detailed Unified Suite documentation
│
├── cpp_recorder/                    # High-Performance C++ Live SDR Receiver & CS16 Capture (SatDump UI & SIMD)
│   ├── bin/                         # Standalone binaries (sdr_recorder_gui.exe, sdr_recorder_cli.exe, DLLs)
│   ├── include/                     # Headers (simd_dsp.h, lockfree_ringbuffer.h, sdr_controller.h, app_gui.h...)
│   ├── src/                         # Implementations (sdr_controller.cpp, dsp_pipeline.cpp, app_gui.cpp...)
│   ├── tests/                       # Automated test harness & 27 unit tests (run_tests.bat)
│   ├── CMakeLists.txt               # CMake configuration
│   ├── build_mingw.bat              # 1-click build script (MinGW GCC 16.2.0 -O3 -mavx2)
│   ├── build_msvc.bat               # 1-click build script (MSVC)
│   └── README.md                    # Detailed C++ architecture & build documentation
│
├── cpp_analyzer/                    # Blazing Fast C++ Offline 21cm H I Analysis Pipeline & SatDump GUI
│   ├── bin/                         # Standalone binaries (astro_analyzer_gui.exe, astro_analyzer_cli.exe)
│   ├── include/                     # Headers (baseline_fitter.h, astrophysics.h, streaming_processor.h...)
│   ├── src/                         # Implementations (baseline_fitter.cpp, plot_renderer.cpp, app_gui.cpp...)
│   ├── tests/                       # Automated test harness & 19 unit tests (run_tests.bat)
│   ├── CMakeLists.txt               # CMake configuration
│   ├── build_mingw.bat              # 1-click build script (MinGW GCC 16.2.0 -O3 -mavx2)
│   ├── build_msvc.bat               # 1-click build script (MSVC)
│   └── README.md                    # Detailed C++ Analyzer documentation
│
├── Recorder/                        # Python Live SDR Receiver & CS16 Capture App
│   ├── app.py                       # Desktop GUI (Device, FFT controls, Recording, Waterfall)
│   ├── sdr_stream.py                # Hardware RTL-SDR driver via ctypes (with Virtual SDR fallback)
│   ├── cs16_writer.py               # Asynchronous high-speed CS16 binary stream recorder
│   ├── run.py                       # Launcher for Desktop GUI and Headless CLI captures
│   ├── launch.bat                   # 1-click Windows portable desktop launcher
│   ├── rtlsdr.dll                   # Bundled 64-bit RTL-SDR hardware driver
│   ├── libusb-1.0.dll               # Bundled 64-bit LibUSB backend
│   └── README.md                    # Detailed Recorder documentation
│
├── Analyzer/                        # Offline 21cm Hydrogen Line Analysis Pipeline
│   ├── astro_analyzer.py            # Core analysis runner & CLI engine
│   ├── hydro_quick.py               # Lightweight 3-panel Matplotlib inspector
│   ├── config.py                    # Pipeline configuration & astrophysical physical constants
│   ├── dsp.py                       # Streaming FFT, PSD, DC excision, & ALS/Chebyshev baseline fitting
│   ├── visualizer.py                # Publication-grade spectral plots, waterfalls, & dashboard
│   ├── exporter.py                  # Structured exports (CSV, JSON, ASCII reports)
│   ├── run.py                       # Interactive CLI analysis wizard launcher
│   ├── output/                      # Analysis reports, plots, and calibrated data
│   │   ├── 1_hi_spectrum_calibrated.png
│   │   ├── 2_hi_raw_and_baseline.png
│   │   ├── 3_hi_doppler_velocity.png
│   │   ├── 4_hi_waterfall_time_frequency.png
│   │   ├── 5_hi_master_dashboard.png
│   │   ├── analysis_report.json
│   │   ├── analysis_report.txt
│   │   └── calibrated_spectrum.csv
│   └── README.md                    # Detailed Analyzer documentation
│
├── recordings/                      # Default capture folder for recorded .cs16 files (gitignored)
└── .gitignore                       # Git rules ignoring local SDR installations & large recordings
```

---

## 1. [Recorder: Live SDR Receiver & CS16 Capture App](./Recorder)

Styled after **SatDump**'s live receiver and recording panel, providing a lightweight, low-latency interface to tune, monitor, and record astronomical signals:

- **Hardware Control**: Auto-detects connected RTL-SDR devices (e.g. `RTLSDRBlog Blog V4 #00000001`), configures sample rates (`1.024` to `3.2 Msps`), tunes center frequency with off-center LO offsets, sets manual/automatic gain (`0.0` to `49.6 dB`), and toggles **4.5V Bias-Tee** to power external LNAs (like Sawbird+ H1).
- **Process Conflict Detection**: Automatically warns if an external SDR application (`satdump`, `sdrpp`, `sdrangel`) is holding the USB dongle and provides instant fallback to **Virtual SDR Simulation Mode**.
- **Lossless CS16 Recording**: Writes raw I/Q samples to disk in SatDump-compatible **CS16** format (`int16` little-endian, 4 bytes/sample) on a dedicated background thread without dropped samples.
- **Dynamic Stats**: Live monitoring of file size, remaining partition disk space, and estimated recording time left.
- **Live Waterfall & Spectrum**: Real-time RF power curve and smooth 30–60 FPS scrolling waterfall with customizable palettes (`Classic [Youssef Touil]`, `Viridis`, `Turbo`, `Inferno`).
- **Direct Integration**: Single-click button (`⚡ Open in Offline Analyzer`) to immediately analyze recorded files.

### Launching the Recorder

- **Desktop GUI**: Double-click `Recorder/launch.bat` or run:
  ```bash
  python Recorder/run.py
  ```
- **Automated Headless Recording**:
  ```bash
  python Recorder/run.py --cli --duration 120 --fc 1420.5M --fs 2.4M --gain 25.4 --bias-tee
  ```

---

## 2. [Analyzer: Offline 21cm Hydrogen Line Analysis Pipeline](./Analyzer)

A scientific-grade analysis suite engineered to process large raw **CS16** observations:

- **High-Throughput Streaming Engine**: Processes multi-gigabyte recordings at **> 100–180 MB/s** (25–45 MSPS) while keeping RAM consumption strictly under **100 MB**.
- **Off-Center DC Offset Combating**: Automatically detects the SDR LO offset, verifies separation from the H I rest line, and smoothly excises the residual DC spike.
- **Two-Stage Baseline Modeling**: Neutralizes the $\sim 700\text{ kHz}$ coaxial standing wave ripples (impedance reflections between LNA and SDR) using Whittaker Asymmetric Least Squares (ALS) and Chebyshev polynomials.
- **Astrophysical Kinematics**: Computes topocentric Doppler velocities ($v = c \cdot \frac{f_0 - f}{f_0}$), non-linear Gaussian line profile fits ($v_\text{peak}$, $\Delta v_\text{FWHM}$, $\sigma_v$), integrated line flux ($W$), and total neutral hydrogen column density ($N_{HI}$).
- **Automated Visualizations**: Generates calibrated spectra, Doppler velocity profiles, time-frequency waterfalls, and executive multi-panel dashboards.

### Running the Analyzer

- **Interactive Wizard**:
  ```bash
  python Analyzer/run.py
  ```
- **Direct Command-Line (CLI)**:
  ```bash
  python Analyzer/astro_analyzer.py recordings/hi_capture.cs16 --fc 1420.5MHz --fs 2.4MSPS
  ```
- **Lightweight 3-Panel Matplotlib Viewer**:
  ```bash
  python Analyzer/hydro_quick.py recordings/hi_capture.cs16 --fc 1420.5M --fs 2.4M
  ```

---

## Quick Installation & Requirements

Ensure you have Python 3.8+ installed:

```bash
pip install numpy scipy matplotlib pillow psutil
```

Bundled 64-bit `rtlsdr.dll` and `libusb-1.0.dll` drivers are included in `Recorder/` for Windows out-of-the-box operation.
