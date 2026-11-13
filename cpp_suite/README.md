# Hydrogen Line SDR & Radio Astronomy Scientific Analysis Suite (C++)

A unified, high-performance, real-time SDR receiver, lossless 16-bit CS16 data recorder, and scientific radio astronomy offline analysis workstation engineered specifically for the **21cm (1420.405751768 MHz) Neutral Hydrogen (H I) Line**.

Built natively in modern C++17 with AVX2/FMA/SSE4.1 SIMD vectorized DSP kernels (including an 8-block batched SoA AVX2 Stockham FFT accumulator and AVX2 double-precision `log10`/`exp10`), lock-free SPSC ring buffers, and a high-contrast **Observatory Slate** DirectX 11 + Dear ImGui desktop interface (`radio_suite.exe`).

---

## Architecture Overview

```
                      +---------------------------------------+
                      |     RTL-SDR / Virtual SDR Device      |
                      +---------------------------------------+
                                          |
                      +---------------------------------------+
                      |       Lock-Free Circular Buffer       |
                      |          (32 Chunks x 256 KB)         |
                      +---------------------------------------+
                                   /             \
                                  /               \
        +--------------------------------+   +--------------------------------+
        |     Real-Time DSP Pipeline     |   |    Lossless CS16 Disk Writer   |
        |  - AVX2 Magnitude Squared      |   |  - Pre-pooled 128-slot Queue   |
        |  - KissFFT Complex Spectrum    |   |  - Batched Win32 Sequential I/O|
        |  - Zero-Alloc Telemetry EMA    |   |  - JSON & TXT Sidecars (.768)  |
        +--------------------------------+   +--------------------------------+
                        |                                    |
                        |                    +--------------------------------+
                        |                    |  Analyze Capture -> Bridge     |
                        |                    +--------------------------------+
                        |                                    |
                        v                                    v
+------------------------------------+       +------------------------------------+
|  1. Live SDR & Record              |       |  2. H I Scientific Analysis        |
|  - Scrollable Mono LCD LO Tuner    |       |  - Sidecar / Native File Browse  |
|  - Pixel-column Min/Max Decimation |       |  - DC excision & MAD RFI filter    |
|  - Dynamic History Waterfall + LUT |       |  - AVX2 RippleFilter, ALS, Poly    |
|  - Discrete Snapped LNA & AGC      |       |  - Levenberg-Marquardt Gauss Fit   |
|  - Bias-Tee +4.5V Safety Modal     |       |  - Pinned 6-KPI Strip & 4 Tabs     |
|  - Timed / Manual CS16 Capture     |       |  - Parallel 5-PNG + CSV/JSON Export|
+------------------------------------+       +------------------------------------+
```

---

## Unified User Guide

The suite provides a single DPI-aware desktop interface (`radio_suite.exe`) and a companion headless CLI (`radio_suite_cli.exe`) unifying two operational modes (switchable via the segmented top pill or `F1` / `F2`).

### Section 1: Live SDR & Record (`AppSection::Record`)
Designed for real-time sky observation, antenna alignment, and raw data logging:
- **Interactive Monospace LCD Frequency Tuner**:
  - Direct MHz text entry, 1-click frequency step controls (`±1M`, `±100k`, `±10k`), mouse-wheel tuning (`±10 kHz`), and automatic `"Custom LO"` vs Band Preset synchronization.
  - Pre-calibrated default LO frequency: `1420.500 MHz` (`+94.248 kHz` offset placing the `1420.405751768 MHz` H I line safely clear of the DC tuner spike).
- **Hardware Source & RF Front-End Controls**:
  - Runtime hardware RTL-SDR vs internal `Virtual 21cm H I Simulator` switcher (`scan`, `initialize`, and active driver sync).
  - Discrete LNA gain slider snapped cleanly to hardware-valid tuner gain steps (auto-disabled under AGC).
  - **Bias-Tee `+4.5V` Safety Confirmation Modal** and persistent amber `BIAS 4.5V` header badge when active.
- **Zero-Occlusion Spectrum & History-Preserving Waterfall**:
  - `PlotViewport2D` real-time spectrum with pixel-column min/max envelope decimation, H I rest reference line, and hover crosshair readout (`MHz`, `V_LSR km/s`, `dBFS`).
  - Dynamic DirectX 11 waterfall using `resize_preserving_history()` and a synchronized right-aligned dBFS colorbar (`Viridis`, `Turbo`, `Inferno`, `Magma`, `ClassicYoussef`).
- **Async CS16 Recorder & Bridge**:
  - Interleaved 16-bit I/Q (`CS16`) capture backed by a 128-slot pre-allocated buffer pool and batched Win32 `WriteFile` sequential I/O.
  - Non-blocking timed duration auto-stop, automatic `.json` and `.txt` sidecar generation, and 1-click `Analyze Capture ->` handoff.

---

### Section 2: Offline Analysis Workstation (`AppSection::Analyze`)
Designed for scientific post-processing, baseline correction, and astrophysical parameter extraction:
- **Native File Discovery, Browse & Stale-Config Detection**:
  - Scans executable-relative `recordings/` and workspace folders via canonical path deduplication, with native Windows `[Browse...]` and `[Open Output Directory]` actions.
  - Parses `.json` and `.txt` sidecars automatically, validates RF inputs, and shows an amber banner if sidebar configuration changes after the active run.
- **Four Scientific Views & Pinned 6-KPI Telemetry Strip**:
  1. **Calibrated H I Spectrum (`T_A K` vs `V_LSR km/s`)**: Dual bottom `km/s` + top Sky Frequency `MHz` axes, Levenberg-Marquardt Gaussian fit overlay, and interactive hover crosshair.
  2. **Raw Passband & Baseline (`dB` vs `MHz`)**: Superimposes raw passband power against AVX2 `RippleFilter`, pentadiagonal `ALS`, or Stack-allocated `Poly` baselines with shaded H I protection band.
  3. **Time-Frequency Waterfall**: GPU-uploaded `<= 2048x1024` spectrogram normalized via `5th–99.5th` percentile contrast with instant 256-entry colormap LUT switching.
  4. **Master Executive Dashboard (`2x2`)**: Raw & Baseline (`Q1`), Calibrated H I Frequency Spectrum (`Q2`, `dB vs MHz`), Doppler Velocity Profile (`Q3`, `K vs km/s`), and Integrated Waterfall (`Q4`).
- **Parallel Publication Export (`Export All 8 Artifacts`)**:
  - Generates 5 PNG plots concurrently via `std::async` (`1_hi_spectrum_calibrated.png` through `5_hi_master_dashboard.png`), plus buffered `calibrated_spectrum.csv`, `analysis_report.json`, and `analysis_report.txt`.

---

## Seamless Record -> Analyze Bridge

The suite eliminates the manual friction of stopping SDR recording, finding the generated file on disk, and importing it into a separate tool:

```
[Stopped / Ready Capture] 
       │
       ├── 1. Finalizes CS16 stream & flushes metadata sidecars (.json, .txt)
       ├── 2. Atomically clears active recording state in AppState
       ├── 3. Handoffs target file path via switch_to_analyze()
       ├── 4. Switches top navigation pill to [2. H I Scientific Analysis]
       ├── 5. Ingests metadata sidecar (center frequency, sample rate, gain)
       └── 6. Launches streaming worker & publishes immutable AnalysisSnapshot
```

### In-Flight Collision Protection
`AppState` actively isolates recording files using `is_file_actively_recording()`. If an analyst attempts to open or process a `.cs16` file that is currently being written by the SDR capture engine, the analysis panel intercepts and safely blocks execution with a clear diagnostic badge until recording has finalized.

---

## 1-Click Laptop Build & Test Guide (Windows)

### Prerequisites
- Modern Windows laptop (Windows 10 / 11, 64-bit).
- MinGW-w64 (GCC 10+), Clang/LLVM, or MSVC.
- Recommended compiler: WinLibs MinGW-w64 UCRT (`winget install BrechtSanders.WinLibs.POSIX.UCRT`).

### 1. Build the Unified Binaries (1-Click)
Run the automated build script from `cpp_suite\`:
```cmd
build_mingw.bat
```
This builds both executables into `cpp_suite\bin\`:
- `bin\radio_suite.exe` (Unified Desktop GUI)
- `bin\radio_suite_cli.exe` (Unified Headless CLI)

*(Alternatively, MSVC users can run `build_msvc.bat`, and CMake users can run `cmake -B build -G Ninja && cmake --build build`).*

### 2. Run the Automated Test Suite (1-Click)
Run the dedicated automated test runner:
```cmd
tests\run_tests.bat
```
*(To run pre-compiled test binaries without rebuilding, use: `tests\run_tests.bat --skip-build`).*

---

## Automated Test Suite Architecture

The test suite is located in `cpp_suite\tests\` and covers 100% of the core state management, bridge functionality, CLI commands, and performance limits:

| Test Executable | Target File | Description | Assertions | Status |
| :--- | :--- | :--- | :---: | :---: |
| `test_suite_state.exe` | `test_suite_state.cpp` | Tests `AppState` mode transitions, in-flight collision prevention, thread safety, and auto-analyze bridge handoff | 26 / 26 | **PASS (100%)** |
| `test_end_to_end_bridge.exe` | `test_end_to_end_bridge.cpp` | Full integration test: SDR virtual capture -> CS16 write -> sidecar creation -> bridge handoff -> offline pipeline -> 21cm line extraction & artifact verification | 40 / 40 | **PASS (100%)** |
| `test_cli_commands.exe` | `test_cli_commands.cpp` | Validates `radio_suite_cli.exe` subcommands (`record`, `analyze`), flag parsing, error reporting, and mode auto-detection | 44 / 44 | **PASS (100%)** |
| `test_performance_stress.exe` | `test_performance_stress.cpp` | Benchmarks disk write speed, streaming FFT throughput, astrophysical pass latency, and audits RSS memory | 5 / 5 | **PASS (100%)** |
| `test_dsp_optimizations.exe` | `test_dsp_optimizations.cpp` | Unit tests for AVX2 `log10`/`exp10`, CS16 window conversion, max-pool waterfall, ring-buffer wraparound, history resize, baseline solvers, Gaussian fit, AVX2 Stockham FFT parity, sidecar dedup & auto-stop writer | 33 / 33 | **PASS (100%)** |
| **Overall Suite** | `run_tests.bat` | Complete automated 1-click test suite runner | **148 / 148** | **PASS (100%)** |

---

## Performance Benchmark Summary

Audited on Windows 11 with AVX2 + FMA hardware acceleration:

| Benchmark Metric | Measured Result | Specification Requirement | Verification Status |
| :--- | :--- | :--- | :---: |
| **Sustained CS16 Disk Write Throughput** | **187.70 MB/s** (49.20 MSPS) | $\ge 20.0\text{ MB/s}$ | **PASS** (Zero dropped blocks) |
| **Streaming DSP Analysis Throughput** | **945.62 MB/s** (247.89 MSPS) | $\ge 40.0\text{ MB/s}$ (Real-time) | **PASS** (>23x faster than target) |
| **Baseline Working Set (RSS Memory)** | **4.12 MB** | $< 50\text{ MB}$ | **PASS** |
| **Peak Working Set (RSS Memory)** | **8.86 MB** | $< 50\text{ MB}$ | **PASS** (>5x below limit) |
| **Memory Growth (50 Stress Passes)** | **0.51 MB** | $< 5.0\text{ MB}$ | **PASS** (Zero unmanaged leaks) |
| **Scientific Pipeline Latency** | **2.92 ms** / full pass | $< 50\text{ ms}$ | **PASS** |

---

## Headless CLI Usage Guide (`radio_suite_cli.exe`)

### 1. Recording Mode
Record real-time or synthetic observations to disk with automatic sidecars:
```cmd
# Record 60 seconds of live SDR data at 1420.5 MHz with 2.4 MSPS:
bin\radio_suite_cli.exe record --fc 1420.5M --fs 2.4M --duration 60 -g 25.4 -o recordings/observation_001.cs16

# Record using virtual SDR simulator mode (no hardware required):
bin\radio_suite_cli.exe record --virtual --fc 1420.5M --fs 2.4M --duration 5 -o recordings/unified_test.cs16
```

### 2. Analysis Mode
Run scientific analysis on any raw CS16 capture:
```cmd
# Run analysis with automatic sidecar detection and output artifacts:
bin\radio_suite_cli.exe analyze recordings/unified_test.cs16 -o output/

# Run analysis with custom baseline filter and assumed system temperature:
bin\radio_suite_cli.exe analyze recordings/unified_test.cs16 --baseline ripple_filter --tsys 100.0 -o output/
```

### 3. Mode Auto-Detection
The CLI automatically resolves intended modes without explicit subcommands:
```cmd
# Passing record flags automatically selects Record mode:
bin\radio_suite_cli.exe --virtual --gain 25.4 --duration 10 -o recordings/auto_rec.cs16

# Passing a .cs16 file automatically selects Analyze mode:
bin\radio_suite_cli.exe recordings/auto_rec.cs16 -o output/
```

---

## Complete Directory Structure

```
cpp_suite/
├── bin/                             # Compiled executables and runtime DLLs
│   ├── libusb-1.0.dll               # Hardware USB interface driver
│   ├── rtlsdr.dll                   # Hardware RTL-SDR driver
│   ├── radio_suite.exe              # Unified Desktop GUI application
│   ├── radio_suite_cli.exe          # Unified Headless CLI application
│   ├── test_suite_state.exe         # AppState unit test executable
│   ├── test_end_to_end_bridge.exe   # Integration bridge test executable
│   ├── test_cli_commands.exe        # CLI subcommand test executable
│   ├── test_performance_stress.exe  # Throughput and memory benchmark
│   └── test_dsp_optimizations.exe   # SIMD, FFT parity, solver & ring-buffer unit tests
├── include/                         # Public Header Files
│   ├── app_gui.h                    # Unified Observatory Slate GUI coordinator
│   ├── app_state.h                  # Global AppState, mode switcher & collision guards
│   ├── astrophysics.h               # Kinematics, Doppler velocity & Levenberg-Marquardt
│   ├── baseline_fitter.h            # Whittaker ALS & ripple baseline solvers
│   ├── config.h                     # Analysis configuration structure
│   ├── constants.h                  # Physical, astronomical & Observatory Slate constants
│   ├── cs16_writer.h                # Lossless CS16 binary writer & sidecar generator
│   ├── dc_excision.h                # LO center DC spike excision
│   ├── dsp_pipeline.h               # Real-time KissFFT & power averaging
│   ├── exporter.h                   # Parallel publication PNG and CSV artifact exporter
│   ├── lockfree_ringbuffer.h        # SPSC atomic circular buffer
│   ├── plot_renderer.h              # Anti-aliased software chart renderer (PNG)
│   ├── rfi_filter.h                 # Modified Z-score median MAD filter
│   ├── sdr_controller.h             # Hardware tuning coalescing, dispatcher & device worker
│   ├── sdr_device.h                 # RTL-SDR hardware interface
│   ├── sidecar_reader.h             # Multi-format metadata sidecar parser
│   ├── simd_dsp.h                   # AVX2/FMA/SSE4.1 vectorized kernels
│   ├── streaming_processor.h        # Multi-threaded AVX2 Stockham FFT accumulator
│   ├── ui_analyze_panel.h           # Offline Analysis modular panel
│   ├── ui_observatory.h             # Observatory Slate theme, vector icons & PlotViewport2D
│   ├── ui_record_panel.h            # Live SDR / Record modular panel
│   ├── virtual_sdr.h                # Astronomical signal simulator
│   └── waterfall.h                  # Rolling RGBA waterfall texture generator
├── lib/                             # Embedded Third-Party Dependencies
│   ├── imgui/                       # Dear ImGui with DirectX 11 / Win32 backends
│   ├── kissfft/                     # KissFFT complex Fourier transform
│   ├── rtlsdr/                      # RTL-SDR hardware driver headers
│   └── stb/                         # stb_image_write header-only image encoder
├── src/                             # Source Implementations
│   ├── app_gui.cpp
│   ├── astrophysics.cpp
│   ├── baseline_fitter.cpp
│   ├── cs16_writer.cpp
│   ├── dc_excision.cpp
│   ├── dsp_pipeline.cpp
│   ├── exporter.cpp
│   ├── main_cli.cpp
│   ├── main_gui.cpp
│   ├── plot_renderer.cpp
│   ├── rfi_filter.cpp
│   ├── sdr_controller.cpp
│   ├── sdr_device.cpp
│   ├── sidecar_reader.cpp
│   ├── streaming_processor.cpp
│   ├── ui_analyze_panel.cpp
│   ├── ui_record_panel.cpp
│   ├── virtual_sdr.cpp
│   └── waterfall.cpp
├── tests/                           # Dedicated Automated Test Suite
│   ├── run_tests.bat                # 1-click test runner script
│   ├── test_suite_state.cpp         # AppState and bridge unit test
│   ├── test_end_to_end_bridge.cpp   # End-to-end integration test
│   ├── test_cli_commands.cpp        # CLI validation and auto-detect test
│   ├── test_performance_stress.cpp  # SIMD, throughput & memory stress test
│   └── test_dsp_optimizations.cpp   # SIMD, FFT parity, solver & ring-buffer unit tests
├── recordings/                      # CS16 captures and sidecar metadata
├── output/                          # Scientific analysis plots and reports
├── build_mingw.bat                  # 1-click GCC/MinGW build script
├── build_msvc.bat                   # 1-click Visual Studio build script
├── build_clang.bat                  # 1-click Clang build script
├── CMakeLists.txt                   # CMake build configuration with test targets
└── README.md                        # Project documentation and guide
```
