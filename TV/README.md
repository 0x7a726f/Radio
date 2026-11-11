# Greek Television & Radio SDR Live Tuner & Native Desktop App

A high-performance, real-time Software-Defined Radio (SDR) terrestrial television tuner and public broadcaster player designed for receiving and monitoring public TV and radio broadcasts across Greece using the RTL-SDR Blog V4 (or compatible SDR receiver).

Featuring a fast C++ Demodulator Engine (`tv_demod.dll`) and a Native Desktop Application (`tv_app.exe`) replacing the legacy WebGUI for smooth 30–60 FPS real-time spectrum analysis, zero browser overhead, and hardware-accelerated demodulation.

---

## Technical Evaluation: WebGUI vs. Native C++ Desktop App

| Capability / Metric | Legacy WebGUI (`app.py`) | Native C++ App (`tv_app.exe`) / Python Desktop (`tv_desktop_app.py`) |
| :--- | :--- | :--- |
| **Interface Architecture** | Streamlit Web Server + Browser Tab | **Standalone Native Windows Desktop Application** (Win32 / Direct2D / Tkinter) |
| **DSP & Demodulation Engine** | Pure Python + NumPy FFT | **Compiled C++ (MSVC /arch:AVX2 /fp:fast /MT)** |
| **FFT / PSD Query Throughput** | ~10–20 queries/second (high latency) | **>690,000 queries/second (~1.4 microseconds/query)** |
| **Display Framerate** | 0.5–2 FPS (stutters on Matplotlib redraw) | **30–60 FPS tear-free real-time rendering** |
| **UI Interaction Latency** | Full-page rerun on every button/slider click | **Instant (<1 ms)** |
| **Audio Demodulation** | None (Web video player only) | **Real-time C++ WFM/AM Demodulator + Windows 48 kHz WaveOut Playback** |
| **UHF Band Scan Speed** | ~30–45 seconds | **~1.2 seconds across all 28 UHF Channels** |
| **Memory & CPU Footprint** | ~350 MB RAM, heavy CPU during plot renders | **<25 MB RAM, 1–2% CPU usage** |

---

## Key Features

1. **Standalone Native Desktop App (`tv_app.exe`)**:
   - Zero browser requirement, zero web server requirement.
   - Clean, functional SDR interface with high-contrast palette.
   - Responsive, lightweight, instant startup (<50 ms).
   - Zero non-OS dependencies (statically linked `/MT`).

2. **C++ SIMD Demodulation Engine (`tv_demod.dll`)**:
   - **Radix-2 Cooley-Tukey FFT & PSD**: Precomputed bit-reversal and twiddle tables with Hann & Blackman-Harris windowing.
   - **Multi-Mode Demodulator**:
     - **DVB-T / COFDM Analyzer**: Carrier frequency offset (CFO) estimation, Guard Interval correlation, in-band flatness, and carrier lock detection.
     - **WFM Demodulator**: Digital Down Converter (DDC), polar discriminator, 50 us European de-emphasis filter, and decimation down to 48 kHz PCM.
     - **AM Demodulator**: Fast envelope detector with DC blocking filter.
   - **Direct Soundcard Output**: Plays real-time demodulated TV sound / radio directly through Windows WaveOut.

3. **Click-to-Tune Greek Station Directory**:
   - Comprehensive directory of Greek national and regional public broadcasters:
     - **ERT 1** (National Public Television)
     - **ERT 2** (Culture, Arts, Documentaries)
     - **ERT 3** (Northern Greece & Regional Communities)
     - **ERT News** (24/7 Rolling News Network)
     - **Parliament TV** (Hellenic Parliament)
     - **ERT Cosmos** (World Culture & Diaspora)
     - **Star Channel** & **Naftemporiki TV** (Free-to-Air News & Business)
     - **Regional Public Stations** (Vergina TV, Astra TV, Center TV, TRT)
     - **ERT Public Radio** (Proto, Deftero, Trito, ERA Sport, Kosmos, Voice of Greece)

4. **Greek Regional Transmitter Multiplex Mapping**:
   - Supports all primary Greek transmission centers with automatic UHF channel and center frequency switching:
     - **Attica (Athens)**: Hymettus, Parnitha, Aegina
     - **Central Macedonia**: Chortiatis, Filippeio
     - **Western Greece / Peloponnese**: Panachaiko (Patras)
     - **Thessaly**: Pelion, Dovroutsi
     - **Crete**: Rogdia (Heraklion), Malaxa (Chania)
     - **Epirus & Ionian**: Ligopsa (Ioannina), Pantokratoras (Corfu)

5. **Live RF Spectrum & Waterfall Spectrogram**:
   - 8 MHz DVB-T channel bandwidth boundary markers.
   - Live metrics: Tuned Frequency (MHz), Received Signal Strength (RSSI dBFS), Signal-to-Noise Ratio (SNR dB), Carrier Lock, and Signal Quality (%).
   - Rolling 2D color waterfall with high-contrast Turbo colormap.

6. **Ultra-Fast UHF TV Band Scanner**:
   - Sweeps European UHF Channels 21 through 48 (470 MHz to 694 MHz) in 8 MHz channel spacing.
   - Sweeps the entire band in ~1.2 seconds in C++ and displays signal occupancy and multiplex lock status.

7. **Native Video & Audio Playback (No Web Browser)**:
   - Video playback runs natively:
     - In **`tv_desktop_app.py`**, video is embedded directly inside the application window using `libvlc` with GPU hardware decoding (DXVA2/D3D11).
     - In **`tv_app.exe`**, video launches directly in a dedicated native VLC player window (`--one-instance`) without touching a web browser.
   - Live TV audio can also be demodulated and monitored in real time directly from the SDR via the C++ demodulator engine into the Windows soundcard.

---

## Greek Regional Frequency Reference (European UHF 8 MHz Band)

$$\text{Center Frequency (MHz)} = 470.0 + 8.0 \times (\text{Channel} - 21) + 4.0\text{ MHz}$$

| Region | Primary Transmitters | ERT MUX 1 (4 Channels + Radio) | ERT MUX 2 (HD Multiplex) | Digea MUX 1 / 2 | Regional MUX |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Attica (Athens)** | Hymettus, Parnitha, Aegina | **CH 21 (474.0 MHz)** | **CH 28 (530.0 MHz)** | CH 22 / 27 | CH 29 (538.0 MHz) |
| **Central Macedonia** | Chortiatis, Filippeio | **CH 24 (498.0 MHz)** | **CH 28 (530.0 MHz)** | CH 29 / 31 | CH 30 (546.0 MHz) |
| **Western Greece** | Panachaiko (Patras) | **CH 24 (498.0 MHz)** | **CH 31 (554.0 MHz)** | CH 22 / 27 | CH 30 (546.0 MHz) |
| **Thessaly** | Pelion, Dovroutsi | **CH 21 (474.0 MHz)** | **CH 28 (530.0 MHz)** | CH 24 / 26 | CH 30 (546.0 MHz) |
| **Crete** | Rogdia, Malaxa | **CH 21 (474.0 MHz)** | **CH 24 (498.0 MHz)** | CH 23 / 25 | CH 29 (538.0 MHz) |
| **Epirus & Ionian** | Ligopsa, Pantokratoras | **CH 21 (474.0 MHz)** | **CH 28 (530.0 MHz)** | CH 22 / 25 | CH 29 (538.0 MHz) |

---

## How to Run

### Method 1: One-Click Desktop Launch (Windows Explorer)
Double-click `launch.bat` in the `TV/` folder. It will immediately launch the native C++ desktop application `tv_app.exe`.

### Method 2: Command Line (Python Launcher)
```powershell
python TV/run.py
```
Options:
- `python TV/run.py --native` : Runs the native C++ executable (`tv_app.exe`).
- `python TV/run.py --pyapp`  : Runs the native Tkinter desktop app (`tv_desktop_app.py`).
- `python TV/run.py --web`    : Runs the legacy Streamlit WebGUI.

### Method 3: Direct Executable Launch
```powershell
.\TV\tv_app.exe
```

---

## Rebuilding C++ Binaries

To recompile `tv_demod.dll` and `tv_app.exe` with MSVC:
```powershell
TV\cpp\build.bat
```
This discovers Visual Studio 2022/2019 automatically via `vswhere.exe` or standard directories and compiles with `/O2 /arch:AVX2 /fp:fast /MT`.

---

## Portability & RTL-SDR Driver Notes

1. **Fully Self-Contained Folder**: The `TV/` directory includes portable `rtlsdr.dll` and `libusb-1.0.dll` alongside `tv_app.exe` and `tv_demod.dll`. The C runtime is statically linked (`/MT`), so no Visual C++ Redistributable or SDR software installation is required when copy-pasting to another Windows PC.
2. **Driver Setup (WinUSB via Zadig)**: The RTL2832U chip requires the generic **WinUSB** driver to be installed on **Bulk-In, Interface (Interface 0)** using [Zadig](https://zadig.akeo.ie/). If Windows assigned its default digital TV driver, `rtlsdr.dll` cannot open raw I/Q samples and will fall back to simulation mode.
3. **Exclusive USB Claim & Quick Reconnect**: The RTL2832U chip allows only one application at a time to open the WinUSB interface. If another SDR application (SatDump, SDR++, SDRangel) was running, close it and click the status banner in the application (or press **`R`**) to reconnect without restarting the program.
4. **Antenna Match**: Terrestrial UHF broadcasts in Greece (470–694 MHz) resonate with dipole elements extended to ~12–15 cm per side. Orient your antenna towards your regional broadcast mast for optimal SNR (>15 dB).
