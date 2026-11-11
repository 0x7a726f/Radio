# Radio

A collection of high-performance Software-Defined Radio (SDR) and Digital Signal Processing (DSP) applications, covering terrestrial digital television broadcast reception and radio astronomy observational pipelines.

---

## Projects Overview

### 1. [TV: Greek Television and Radio SDR Live Tuner](./TV)

A real-time SDR terrestrial digital television tuner and public broadcaster player optimized for the RTL-SDR Blog V4 (and compatible receivers) with fallback RF simulation.

- **C++ SIMD Demodulator (`tv_demod.dll`)**: AVX2-accelerated Radix-2 Cooley-Tukey FFT and PSD computing over 690,000 spectral queries per second (~1.4 microseconds per query), with built-in DVB-T/COFDM carrier analysis, WFM/AM demodulators, and Windows WaveOut soundcard output.
- **Native Desktop Application (`tv_app.exe`)**: Standalone Win32 double-buffered desktop app rendering real-time RF spectrum and 2D rolling waterfall at 30–60 FPS with instant response and zero browser overhead.
- **Native Video & Audio Playback**: Plays live station streams natively without web browser dependency, supporting direct VLC (`--one-instance`) and in-window embedded playback (`tv_desktop_app.py`) via `libvlc`.
- **Pre-configured Station Directory & Frequency Maps**: Covers national Greek broadcasters (ERT 1, ERT 2, ERT 3, ERT News, Parliament TV, ERT Cosmos, Star Channel, Naftemporiki TV, regional stations, and ERT public radio) across primary regional transmitters (Athens/Attica, Central Macedonia, Western Greece, Thessaly, Crete, and Epirus).
- **Portable & Self-Contained**: Statically linked CRT (`/MT`) with bundled `rtlsdr.dll` and `libusb-1.0.dll` for immediate execution via `launch.bat` on any Windows system.

### 2. [Astro: 21cm Neutral Hydrogen (H I) Radio Astronomy Pipeline](./Astro)

A high-throughput radio astronomy pipeline engineered to process raw CS16 (complex signed 16-bit integer I/Q) radio telescope observations centered around the 21cm Neutral Hydrogen line (~1420.40575 MHz).

- **Streaming DSP Engine**: Chunked streaming architecture processing multi-gigabyte recordings at >100–180 MB/s while maintaining memory consumption strictly under 100 MB RAM.
- **Instrumental Correction**: Smooth excision of central LO leakage and DC spikes, coupled with advanced two-stage baseline modeling (adaptive Whittaker smoothing and Chebyshev polynomials) to eliminate coaxial cable standing wave reflections (~700 kHz ripples).
- **Astrophysical Analysis**: Topocentric Doppler velocity calculations, non-linear Gaussian line profile fitting (FWHM, velocity dispersion, peak velocity), integrated flux integration, and total column density ($N_{HI}$) determination.
- **Automated Output**: Generates calibrated spectra, time-frequency waterfalls proving signal continuity and RFI absence, multi-panel observation dashboards, CSV exports, and structured JSON/text reports.

---

## Directory Structure

```text
Radio/
├── Astro/                   # 21cm Neutral Hydrogen Line Radio Astronomy Pipeline
│   ├── astro_analyzer.py    # Core analysis runner & CLI
│   ├── config.py            # Astronomy pipeline configuration & physical constants
│   ├── dsp.py               # Multithreaded streaming FFT, PSD, & baseline fitting
│   ├── exporter.py          # Data export (CSV, JSON, ASCII reports)
│   ├── run.py               # Interactive CLI wizard
│   ├── visualizer.py        # Spectral plots, waterfalls, and observation dashboard
│   ├── output/              # Sample observation reports and visualizations
│   └── README.md            # Detailed astronomy pipeline documentation
│
├── TV/                      # Greek TV & Radio SDR Live Tuner & Native Desktop App
│   ├── cpp/                 # C++ Source Code & MSVC Build Tools
│   │   ├── build.bat        # Portable compiler script (/MT /arch:AVX2 /O2)
│   │   ├── tv_app.cpp       # Native Win32 desktop GUI application
│   │   ├── tv_demod.cpp     # AVX2 FFT, DVB-T analyzer, WFM/AM demod engine
│   │   └── tv_demod.h       # C++ Demodulator API header
│   ├── tv_app.exe           # Standalone native desktop executable (/MT)
│   ├── tv_demod.dll         # High-speed AVX2 SIMD demodulator dynamic library
│   ├── rtlsdr.dll           # Bundled RTL-SDR hardware driver
│   ├── libusb-1.0.dll       # Bundled USB backend library
│   ├── tv_desktop_app.py    # Python desktop application with embedded VLC video
│   ├── launch.bat           # One-click portable desktop launcher
│   ├── run.py               # Multi-mode launcher (--native, --pyapp, --web)
│   ├── channels.py          # Greek channel multiplex definitions & frequencies
│   ├── channels.m3u8        # Exported station playlist
│   ├── sdr_receiver.py      # Python RTL-SDR interface with RF simulation fallback
│   ├── dsp.py               # Python fallback DSP & DVB-T metric computation
│   └── README.md            # Comprehensive TV tuner architecture documentation
│
└── .gitignore               # Git rules ignoring local SDR installations & large recordings
```

---

## Quick Start

### Running the TV Desktop Tuner
1. Navigate to the `TV` directory:
   ```cmd
   cd TV
   ```
2. Double-click `launch.bat` or run:
   ```cmd
   tv_app.exe
   ```

### Running the Radio Astronomy Pipeline
1. Navigate to the `Astro` directory:
   ```cmd
   cd Astro
   ```
2. Launch the interactive analysis wizard:
   ```cmd
   python run.py
   ```
