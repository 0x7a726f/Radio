#!/usr/bin/env python3
"""
Hydrogen Line Live SDR Receiver & CS16 Recorder
A high-performance SatDump-styled application for real-time SDR acquisition,
FFT spectrum & waterfall visualization, and lossless CS16 recording.
"""

import os
import sys
import time
import subprocess
import threading
from typing import Optional, List
import numpy as np

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from PIL import Image, ImageTk
import matplotlib

# Local imports
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from sdr_stream import SdrStream, detect_blocking_processes
from cs16_writer import Cs16Writer

# Astronomical Constants
HI_REST_FREQ_HZ = 1420405751.786  # 1420.40575 MHz


def build_classic_palette() -> np.ndarray:
    """Build the classic SDR# / SatDump / Airspy palette."""
    anchors = [
        (0.00, [10, 15, 45]),
        (0.20, [0, 50, 160]),
        (0.38, [0, 160, 210]),
        (0.55, [30, 200, 70]),
        (0.72, [250, 220, 0]),
        (0.88, [240, 50, 0]),
        (1.00, [255, 255, 255]),
    ]
    x_in = [a[0] for a in anchors]
    r_in = [a[1][0] for a in anchors]
    g_in = [a[1][1] for a in anchors]
    b_in = [a[1][2] for a in anchors]
    x_out = np.linspace(0, 1, 256)
    r = np.interp(x_out, x_in, r_in).astype(np.uint8)
    g = np.interp(x_out, x_in, g_in).astype(np.uint8)
    b = np.interp(x_out, x_in, b_in).astype(np.uint8)
    return np.dstack((r, g, b))[0]


def get_palette_lut(name: str) -> np.ndarray:
    """Return 256x3 RGB lookup table for waterfall coloring."""
    if name.startswith("Classic"):
        return build_classic_palette()
    cmap_name = name.lower()
    if cmap_name in matplotlib.colormaps:
        cmap = matplotlib.colormaps[cmap_name]
        return (cmap(np.linspace(0, 1, 256))[:, :3] * 255).astype(np.uint8)
    return build_classic_palette()


class SatDumpRecorderApp(tk.Tk):
    """Main SatDump-style SDR Receiver & CS16 Recording Application."""

    def __init__(self):
        super().__init__()
        self.title("Hydrogen Line SDR Receiver & CS16 Recorder")
        self.geometry("1280x820")
        self.minsize(1000, 680)
        self.configure(bg="#141419")

        # Core SDR & Writer Engines
        self.sample_rate = 2400000
        self.center_freq = 1420500000  # Default 1420.5 MHz (off-center +94 kHz from HI line)
        self.sdr = SdrStream(sample_rate_hz=self.sample_rate, center_freq_hz=self.center_freq)
        self.recordings_dir = os.path.abspath(os.path.join(current_dir, "..", "recordings"))
        self.writer = Cs16Writer(output_dir=self.recordings_dir)
        self.sdr.add_callback(self.writer.feed_samples)

        # FFT & Waterfall Processing State
        self.fft_size = 4096
        self.fft_rate = 30
        self.waterfall_rate = 30
        self.fft_max_db = -50.0
        self.fft_min_db = -105.0
        self.avg_factor = 0.65
        self.palette_name = "Classic [Youssef Touil]"
        self.palette_lut = get_palette_lut(self.palette_name)
        self.show_waterfall = True
        self.show_grid = True

        # Buffers for thread-safe display
        self.latest_iq_lock = threading.Lock()
        self.latest_iq: Optional[np.ndarray] = None
        self.sdr.add_callback(self._on_sdr_samples)

        self.power_smooth: Optional[np.ndarray] = None
        self.waterfall_height = 300
        self.waterfall_width = 800
        self.waterfall_history = np.zeros((self.waterfall_height, self.waterfall_width, 3), dtype=np.uint8)
        self.waterfall_image_tk = None

        # Build UI
        self._setup_styles()
        self._create_layout()

        # Connect SDR
        self._refresh_devices()
        self.sdr.open()
        self.sdr.start()
        self._update_device_ui_state()

        # Start periodic GUI updates
        self.after(33, self._gui_update_loop)
        self.after(250, self._stats_update_loop)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

        # Dark palette
        bg_dark = "#141419"
        panel_bg = "#1f1f26"
        fg_white = "#e0e0e8"
        accent_cyan = "#00d2d3"

        style.configure(".", background=bg_dark, foreground=fg_white, font=("Segoe UI", 9))
        style.configure("TFrame", background=bg_dark)
        style.configure("Panel.TFrame", background=panel_bg)
        style.configure("TLabel", background=bg_dark, foreground=fg_white)
        style.configure("Panel.TLabel", background=panel_bg, foreground=fg_white)
        style.configure("Header.TLabel", background=panel_bg, foreground=accent_cyan, font=("Segoe UI", 10, "bold"))
        style.configure("Muted.TLabel", background=panel_bg, foreground="#888898", font=("Segoe UI", 8))
        style.configure("TButton", background="#2a2a36", foreground=fg_white, borderwidth=1, relief="flat", padding=4)
        style.map("TButton", background=[("active", "#3a3a4c"), ("pressed", "#4a4a60")])
        style.configure("Accent.TButton", background="#0078d7", foreground="white", font=("Segoe UI", 9, "bold"))
        style.map("Accent.TButton", background=[("active", "#1084e3")])
        style.configure("Record.TButton", background="#d63031", foreground="white", font=("Segoe UI", 9, "bold"))
        style.map("Record.TButton", background=[("active", "#e17055")])
        style.configure("TCombobox", fieldbackground="#282834", background="#282834", foreground=fg_white, arrowcolor=fg_white)
        style.configure("TCheckbutton", background=panel_bg, foreground=fg_white)
        style.map("TCheckbutton", background=[("active", panel_bg)])
        style.configure("TScale", background=panel_bg, troughcolor="#282834")

    def _create_layout(self):
        main_container = ttk.Frame(self)
        main_container.pack(fill=tk.BOTH, expand=True)

        # Left Sidebar (Controls)
        sidebar_frame = tk.Frame(main_container, bg="#1a1a22", width=340, padx=8, pady=8)
        sidebar_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 2))
        sidebar_frame.pack_propagate(False)

        # Sidebar canvas for smooth scrolling if needed
        self._build_sidebar_content(sidebar_frame)

        # Right Main Display (Spectrum & Waterfall)
        display_frame = tk.Frame(main_container, bg="#0d0d12")
        display_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        self._build_display_content(display_frame)

    def _build_sidebar_content(self, parent):
        canvas = tk.Canvas(parent, bg="#1a1a22", highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg="#1a1a22")

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw", width=320)
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # --- 1. DEVICE PANEL ---
        self._create_panel_device(scrollable_frame)

        # --- 2. FFT PANEL ---
        self._create_panel_fft(scrollable_frame)

        # --- 3. RECORDING PANEL ---
        self._create_panel_recording(scrollable_frame)

    def _create_panel_device(self, parent):
        box = tk.LabelFrame(parent, text="▼ Device", bg="#1f1f26", fg="#00d2d3", font=("Segoe UI", 10, "bold"), padx=8, pady=6)
        box.pack(fill=tk.X, pady=(0, 8))

        # Device selector row
        dev_row = tk.Frame(box, bg="#1f1f26")
        dev_row.pack(fill=tk.X, pady=2)
        self.dev_var = tk.StringVar()
        self.dev_combo = ttk.Combobox(dev_row, textvariable=self.dev_var, state="readonly", width=25)
        self.dev_combo.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.dev_combo.bind("<<ComboboxSelected>>", self._on_device_selected)

        refresh_btn = tk.Button(dev_row, text="⟳", bg="#2c2c38", fg="#00d2d3", font=("Segoe UI", 10, "bold"),
                                relief="flat", command=self._refresh_devices, width=3)
        refresh_btn.pack(side=tk.RIGHT, padx=(4, 0))

        # Status note
        self.status_label = tk.Label(box, text="Searching devices...", bg="#1f1f26", fg="#a4b0be", font=("Segoe UI", 8), anchor="w")
        self.status_label.pack(fill=tk.X, pady=(2, 4))

        # Decimation & LO offset
        row_params = tk.Frame(box, bg="#1f1f26")
        row_params.pack(fill=tk.X, pady=2)
        tk.Label(row_params, text="Decimation: 1", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        tk.Label(row_params, text="0.000000 MHz (LO offset)", bg="#1f1f26", fg="#888898").pack(side=tk.RIGHT)

        # DIGITAL FREQUENCY DISPLAY
        freq_box = tk.Frame(box, bg="#0d0d12", relief="sunken", bd=1, padx=6, pady=4)
        freq_box.pack(fill=tk.X, pady=6)
        self.freq_display_var = tk.StringVar(value="001.420.500.000 Hz")
        freq_lbl = tk.Label(freq_box, textvariable=self.freq_display_var, bg="#0d0d12", fg="#00ffc4",
                            font=("Consolas", 15, "bold"))
        freq_lbl.pack()
        freq_lbl.bind("<Button-1>", self._prompt_frequency_dialog)

        # Quick H I presets
        preset_frame = tk.Frame(box, bg="#1f1f26")
        preset_frame.pack(fill=tk.X, pady=2)
        tk.Button(preset_frame, text="1420.5M (LO +94k)", bg="#2a2a36", fg="#00d2d3", font=("Segoe UI", 7),
                  relief="flat", command=lambda: self.set_frequency_hz(1420500000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(preset_frame, text="1420.4M (Rest)", bg="#2a2a36", fg="#ff4757", font=("Segoe UI", 7),
                  relief="flat", command=lambda: self.set_frequency_hz(1420405752)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(preset_frame, text="1420.2M", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7),
                  relief="flat", command=lambda: self.set_frequency_hz(1420206000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)

        # Samplerate
        sr_row = tk.Frame(box, bg="#1f1f26")
        sr_row.pack(fill=tk.X, pady=2)
        tk.Label(sr_row, text="Samplerate:", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.sr_var = tk.StringVar(value="2.4 Msps")
        sr_combo = ttk.Combobox(sr_row, textvariable=self.sr_var, state="readonly", width=12,
                                values=["1.024 Msps", "2.048 Msps", "2.4 Msps", "2.56 Msps", "3.2 Msps"])
        sr_combo.pack(side=tk.RIGHT)
        sr_combo.bind("<<ComboboxSelected>>", self._on_samplerate_changed)

        # Frequency Correction (ppm)
        ppm_row = tk.Frame(box, bg="#1f1f26")
        ppm_row.pack(fill=tk.X, pady=2)
        tk.Label(ppm_row, text="Correction (ppm):", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.ppm_var = tk.IntVar(value=0)
        ppm_spin = tk.Spinbox(ppm_row, from_=-100, to=100, textvariable=self.ppm_var, width=6, bg="#282834", fg="white",
                              command=self._on_ppm_changed)
        ppm_spin.pack(side=tk.RIGHT)

        # LNA Gain
        gain_row = tk.Frame(box, bg="#1f1f26")
        gain_row.pack(fill=tk.X, pady=2)
        tk.Label(gain_row, text="LNA Gain:", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.gain_val_lbl = tk.Label(gain_row, text="25.4 dB", bg="#1f1f26", fg="#00d2d3", font=("Segoe UI", 9, "bold"))
        self.gain_val_lbl.pack(side=tk.RIGHT)

        self.gain_scale = ttk.Scale(box, from_=0.0, to=49.6, value=25.4, command=self._on_gain_changed)
        self.gain_scale.pack(fill=tk.X, pady=2)

        # Checkboxes: AGC and Bias-Tee
        check_row = tk.Frame(box, bg="#1f1f26")
        check_row.pack(fill=tk.X, pady=4)
        self.agc_var = tk.BooleanVar(value=False)
        self.agc_chk = ttk.Checkbutton(check_row, text="AGC", variable=self.agc_var, command=self._on_agc_toggled)
        self.agc_chk.pack(side=tk.LEFT, padx=(0, 10))

        self.bias_var = tk.BooleanVar(value=False)
        self.bias_chk = ttk.Checkbutton(check_row, text="Bias-Tee (4.5V)", variable=self.bias_var, command=self._on_bias_toggled)
        self.bias_chk.pack(side=tk.LEFT)

        # SDR Start / Stop Toggle
        self.sdr_btn = tk.Button(box, text="Stop SDR", bg="#c0392b", fg="white", font=("Segoe UI", 9, "bold"),
                                 relief="flat", pady=4, command=self._toggle_sdr_streaming)
        self.sdr_btn.pack(fill=tk.X, pady=(6, 2))

    def _create_panel_fft(self, parent):
        box = tk.LabelFrame(parent, text="▼ FFT Display", bg="#1f1f26", fg="#00d2d3", font=("Segoe UI", 10, "bold"), padx=8, pady=6)
        box.pack(fill=tk.X, pady=(0, 8))

        # FFT Size
        row_fft = tk.Frame(box, bg="#1f1f26")
        row_fft.pack(fill=tk.X, pady=2)
        tk.Label(row_fft, text="FFT Size:", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.fft_size_var = tk.StringVar(value="4096")
        fft_combo = ttk.Combobox(row_fft, textvariable=self.fft_size_var, state="readonly", width=10,
                                 values=["2048", "4096", "8192", "16384"])
        fft_combo.pack(side=tk.RIGHT)
        fft_combo.bind("<<ComboboxSelected>>", self._on_fft_size_changed)

        # FFT Max & Min (dB)
        row_max = tk.Frame(box, bg="#1f1f26")
        row_max.pack(fill=tk.X, pady=1)
        tk.Label(row_max, text="FFT Max (dB):", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)
        self.fft_max_lbl = tk.Label(row_max, text="-50 dB", bg="#1f1f26", fg="#e0e0e8")
        self.fft_max_lbl.pack(side=tk.RIGHT)
        self.scale_max = ttk.Scale(box, from_=-20.0, to=-70.0, value=-50.0, command=self._on_fft_range_changed)
        self.scale_max.pack(fill=tk.X, pady=1)

        row_min = tk.Frame(box, bg="#1f1f26")
        row_min.pack(fill=tk.X, pady=1)
        tk.Label(row_min, text="FFT Min (dB):", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)
        self.fft_min_lbl = tk.Label(row_min, text="-105 dB", bg="#1f1f26", fg="#e0e0e8")
        self.fft_min_lbl.pack(side=tk.RIGHT)
        self.scale_min = ttk.Scale(box, from_=-80.0, to=-130.0, value=-105.0, command=self._on_fft_range_changed)
        self.scale_min.pack(fill=tk.X, pady=1)

        # Avg Num
        row_avg = tk.Frame(box, bg="#1f1f26")
        row_avg.pack(fill=tk.X, pady=1)
        tk.Label(row_avg, text="Averaging:", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)
        self.avg_lbl = tk.Label(row_avg, text="65%", bg="#1f1f26", fg="#e0e0e8")
        self.avg_lbl.pack(side=tk.RIGHT)
        self.scale_avg = ttk.Scale(box, from_=0.0, to=0.95, value=0.65, command=self._on_avg_changed)
        self.scale_avg.pack(fill=tk.X, pady=1)

        # Palette
        row_pal = tk.Frame(box, bg="#1f1f26")
        row_pal.pack(fill=tk.X, pady=2)
        tk.Label(row_pal, text="Palette:", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.pal_var = tk.StringVar(value=self.palette_name)
        pal_combo = ttk.Combobox(row_pal, textvariable=self.pal_var, state="readonly", width=18,
                                 values=["Classic [Youssef Touil]", "Viridis", "Turbo", "Inferno", "Magma"])
        pal_combo.pack(side=tk.RIGHT)
        pal_combo.bind("<<ComboboxSelected>>", self._on_palette_changed)

        # Checkboxes
        row_view = tk.Frame(box, bg="#1f1f26")
        row_view.pack(fill=tk.X, pady=3)
        self.chk_waterfall_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(row_view, text="Show Waterfall", variable=self.chk_waterfall_var, command=self._toggle_waterfall_view).pack(side=tk.LEFT)
        self.chk_grid_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(row_view, text="Frequency Scale", variable=self.chk_grid_var).pack(side=tk.RIGHT)

    def _create_panel_recording(self, parent):
        box = tk.LabelFrame(parent, text="▼ Recording", bg="#1f1f26", fg="#00d2d3", font=("Segoe UI", 10, "bold"), padx=8, pady=6)
        box.pack(fill=tk.X, pady=(0, 8))

        # Format row (matching screenshot: cs16 format dropdown)
        row_fmt = tk.Frame(box, bg="#1f1f26")
        row_fmt.pack(fill=tk.X, pady=2)
        self.format_var = tk.StringVar(value="cs16")
        fmt_combo = ttk.Combobox(row_fmt, textvariable=self.format_var, state="readonly", width=8, values=["cs16", "cu8"])
        fmt_combo.pack(side=tk.LEFT)
        tk.Label(row_fmt, text="Format", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.RIGHT)

        # Stats displays matching screenshot
        self.lbl_size = tk.Label(box, text="Size : 0.00 MB", bg="#1f1f26", fg="#e0e0e8", anchor="w")
        self.lbl_size.pack(fill=tk.X, pady=1)

        self.lbl_free = tk.Label(box, text="Free Space: Calculating...", bg="#1f1f26", fg="#e0e0e8", anchor="w")
        self.lbl_free.pack(fill=tk.X, pady=1)

        self.lbl_time_left = tk.Label(box, text="Time left: --:--:--", bg="#1f1f26", fg="#e0e0e8", anchor="w")
        self.lbl_time_left.pack(fill=tk.X, pady=1)

        self.lbl_file = tk.Label(box, text="File : None", bg="#1f1f26", fg="#888898", anchor="w", wraplength=300)
        self.lbl_file.pack(fill=tk.X, pady=1)

        # Status text matching screenshot (IDLE in red / RECORDING in green)
        self.lbl_rec_status = tk.Label(box, text="IDLE", bg="#1f1f26", fg="#ff4757", font=("Segoe UI", 11, "bold"), anchor="w")
        self.lbl_rec_status.pack(fill=tk.X, pady=(4, 2))

        # Start / Stop Record button
        self.rec_btn = tk.Button(box, text="Start Recording", bg="#27ae60", fg="white", font=("Segoe UI", 9, "bold"),
                                 relief="flat", pady=5, command=self._toggle_recording)
        self.rec_btn.pack(fill=tk.X, pady=4)

        # Analyze in Offline Tool button
        self.analyze_btn = tk.Button(box, text="⚡ Open in Offline Analyzer", bg="#2c3e50", fg="#00d2d3",
                                     font=("Segoe UI", 8, "bold"), relief="flat", pady=3,
                                     command=self._launch_analyzer_on_last_file)
        self.analyze_btn.pack(fill=tk.X, pady=(2, 0))

    def _build_display_content(self, parent):
        # Spectrum Canvas (Top)
        self.spectrum_canvas = tk.Canvas(parent, bg="#0d0d12", highlightthickness=0)
        self.spectrum_canvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.spectrum_canvas.bind("<Configure>", self._on_canvas_resize)

        # Waterfall Canvas (Bottom)
        self.waterfall_canvas = tk.Canvas(parent, bg="#0d0d12", highlightthickness=0, height=300)
        self.waterfall_canvas.pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True)

    def _on_canvas_resize(self, event):
        w = max(event.width, 200)
        if self.waterfall_width != w:
            self.waterfall_width = w
            self.waterfall_history = np.zeros((self.waterfall_height, self.waterfall_width, 3), dtype=np.uint8)

    def _on_sdr_samples(self, raw_u8: np.ndarray):
        """Callback from background SDR acquisition thread."""
        with self.latest_iq_lock:
            self.latest_iq = raw_u8

    def _refresh_devices(self):
        blocking = detect_blocking_processes()
        devices = self.sdr.scan_devices()

        if blocking:
            procs = ", ".join(blocking)
            self.status_label.config(text=f"Dongle held by: {procs}", fg="#ff9f43")
        else:
            self.status_label.config(text=f"Found {len(devices)} device(s)", fg="#2ed573")

        self.dev_combo["values"] = devices
        if devices:
            self.dev_combo.current(0)
            self._update_device_ui_state()

    def _on_device_selected(self, event=None):
        idx = self.dev_combo.current()
        self.sdr.open(idx)
        self._update_device_ui_state()

    def _update_device_ui_state(self):
        if self.sdr.is_simulated:
            self.status_label.config(text=self.sdr.status_detail or "Virtual SDR Active", fg="#ff9f43")
        else:
            self.status_label.config(text=f"Active: {self.sdr.device_name}", fg="#2ed573")

        # Update available gains
        gains = self.sdr.get_valid_gains()
        if gains:
            self.gain_scale.config(from_=gains[0], to=gains[-1])

    def set_frequency_hz(self, freq_hz: int):
        self.center_freq = int(freq_hz)
        self.sdr.set_frequency(self.center_freq)
        self._update_frequency_display()

    def _update_frequency_display(self):
        f = self.center_freq
        ghz = f // 1000000000
        mhz = (f % 1000000000) // 1000000
        khz = (f % 1000000) // 1000
        hz = f % 1000
        self.freq_display_var.set(f"{ghz:03d}.{mhz:03d}.{khz:03d}.{hz:03d} Hz")

    def _prompt_frequency_dialog(self, event=None):
        import tkinter.simpledialog as sd
        res = sd.askstring("Tune Frequency", "Enter Center Frequency (e.g. 1420.5M, 1420405752, 1420.2):",
                           initialvalue=f"{self.center_freq / 1e6:.3f}M")
        if res:
            res = res.strip().lower()
            try:
                if res.endswith("m") or res.endswith("mhz"):
                    val = float(res.rstrip("mhz")) * 1e6
                elif res.endswith("k") or res.endswith("khz"):
                    val = float(res.rstrip("khz")) * 1e3
                else:
                    v = float(res)
                    val = v * 1e6 if v < 10000.0 else v
                self.set_frequency_hz(int(val))
            except Exception:
                pass

    def _on_samplerate_changed(self, event=None):
        val_str = self.sr_var.get()
        rate_map = {
            "1.024 Msps": 1024000,
            "2.048 Msps": 2048000,
            "2.4 Msps": 2400000,
            "2.56 Msps": 2560000,
            "3.2 Msps": 3200000,
        }
        if val_str in rate_map:
            self.sample_rate = rate_map[val_str]
            self.sdr.set_sample_rate(self.sample_rate)

    def _on_gain_changed(self, val):
        g = round(float(val), 1)
        self.gain_val_lbl.config(text=f"{g:.1f} dB")
        if not self.agc_var.get():
            self.sdr.set_gain(g, agc=False)

    def _on_agc_toggled(self):
        auto = self.agc_var.get()
        if auto:
            self.gain_val_lbl.config(text="Auto (AGC)")
            self.sdr.set_gain(0, agc=True)
        else:
            g = round(self.gain_scale.get(), 1)
            self.gain_val_lbl.config(text=f"{g:.1f} dB")
            self.sdr.set_gain(g, agc=False)

    def _on_bias_toggled(self):
        enabled = self.bias_var.get()
        self.sdr.set_bias_tee(enabled)

    def _on_ppm_changed(self):
        ppm = self.ppm_var.get()
        self.sdr.set_ppm(ppm)

    def _toggle_sdr_streaming(self):
        if self.sdr.is_running:
            self.sdr.stop()
            self.sdr_btn.config(text="Start SDR", bg="#27ae60")
        else:
            self.sdr.start()
            self.sdr_btn.config(text="Stop SDR", bg="#c0392b")

    def _on_fft_size_changed(self, event=None):
        self.fft_size = int(self.fft_size_var.get())
        self.power_smooth = None

    def _on_fft_range_changed(self, val=None):
        self.fft_max_db = self.scale_max.get()
        self.fft_min_db = self.scale_min.get()
        self.fft_max_lbl.config(text=f"{self.fft_max_db:.0f} dB")
        self.fft_min_lbl.config(text=f"{self.fft_min_db:.0f} dB")

    def _on_avg_changed(self, val):
        self.avg_factor = float(val)
        self.avg_lbl.config(text=f"{int(self.avg_factor * 100)}%")

    def _on_palette_changed(self, event=None):
        self.palette_name = self.pal_var.get()
        self.palette_lut = get_palette_lut(self.palette_name)

    def _toggle_waterfall_view(self):
        self.show_waterfall = self.chk_waterfall_var.get()
        if self.show_waterfall:
            self.waterfall_canvas.pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True)
        else:
            self.waterfall_canvas.pack_forget()

    def _toggle_recording(self):
        if not self.writer.is_recording:
            # Start recording
            gain_val = self.gain_scale.get() if not self.agc_var.get() else 0.0
            filepath = self.writer.start_recording(
                sample_rate_hz=self.sample_rate,
                center_freq_hz=self.center_freq,
                device_name=self.sdr.device_name,
                gain_db=gain_val,
                bias_tee=self.bias_var.get(),
            )
            self.rec_btn.config(text="Stop Recording", bg="#d63031")
            self.lbl_rec_status.config(text="RECORDING", fg="#2ed573")
            self.lbl_file.config(text=f"File : {os.path.basename(filepath)}")
        else:
            # Stop recording
            meta = self.writer.stop_recording()
            self.rec_btn.config(text="Start Recording", bg="#27ae60")
            self.lbl_rec_status.config(text="IDLE", fg="#ff4757")

    def _launch_analyzer_on_last_file(self):
        target_file = self.writer.current_filepath
        if not target_file or not os.path.exists(target_file):
            # Check for any .cs16 file in recordings/
            recordings = [os.path.join(self.recordings_dir, f) for f in os.listdir(self.recordings_dir) if f.endswith(".cs16")]
            if recordings:
                recordings.sort(key=lambda p: os.path.getmtime(p), reverse=True)
                target_file = recordings[0]
            else:
                # Browse
                target_file = filedialog.askopenfilename(
                    title="Select CS16 Recording for Analysis",
                    filetypes=[("CS16 Radio Astronomy Captures", "*.cs16"), ("All Files", "*.*")]
                )

        if target_file and os.path.exists(target_file):
            analyzer_script = os.path.abspath(os.path.join(current_dir, "..", "Analyzer", "run.py"))
            cmd = [sys.executable, analyzer_script]
            try:
                subprocess.Popen(cmd, creationflags=subprocess.CREATE_NEW_CONSOLE if os.name == 'nt' else 0)
            except Exception as e:
                messagebox.showerror("Launcher Error", f"Could not launch analyzer: {e}")
        else:
            messagebox.showinfo("No Capture Found", "Record a CS16 file first or select an existing file.")

    def _stats_update_loop(self):
        """Update live recording stats every 250ms."""
        stats = self.writer.get_stats()
        self.lbl_size.config(text=f"Size : {stats['size_str']}")
        self.lbl_free.config(text=f"Free Space: {stats['free_space_str']}")
        self.lbl_time_left.config(text=f"Time left: {stats['time_left_str']}")

        if self.writer.is_recording:
            self.lbl_rec_status.config(text=f"RECORDING ({stats['elapsed_str']})", fg="#2ed573")
        else:
            self.lbl_rec_status.config(text="IDLE", fg="#ff4757")

        self.after(250, self._stats_update_loop)

    def _gui_update_loop(self):
        """High-speed rendering loop for spectrum and waterfall (~30 FPS)."""
        raw_u8 = None
        with self.latest_iq_lock:
            if self.latest_iq is not None:
                raw_u8 = self.latest_iq
                self.latest_iq = None

        if raw_u8 is not None and len(raw_u8) >= (self.fft_size * 2):
            # Convert slice to complex float
            n_samples = self.fft_size
            u8_slice = raw_u8[: n_samples * 2]
            i_samples = (u8_slice[0::2].astype(np.float32) - 127.5) / 128.0
            q_samples = (u8_slice[1::2].astype(np.float32) - 127.5) / 128.0
            iq = i_samples + 1j * q_samples

            # Windowed FFT & Power
            window = np.hanning(n_samples)
            fft_data = np.fft.fftshift(np.fft.fft(iq * window))
            power = 10 * np.log10(np.abs(fft_data)**2 / n_samples + 1e-12)

            if self.power_smooth is None or len(self.power_smooth) != len(power):
                self.power_smooth = power
            else:
                self.power_smooth = self.avg_factor * self.power_smooth + (1.0 - self.avg_factor) * power

            self._draw_spectrum(self.power_smooth)
            if self.show_waterfall:
                self._draw_waterfall_slice(self.power_smooth)

        self.after(33, self._gui_update_loop)

    def _draw_spectrum(self, power: np.ndarray):
        canvas = self.spectrum_canvas
        canvas.delete("all")

        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 100 or h < 50:
            return

        # Coordinates margins
        pad_l, pad_r, pad_t, pad_b = 60, 20, 20, 30
        plot_w = w - pad_l - pad_r
        plot_h = h - pad_t - pad_b

        db_min = self.fft_min_db
        db_max = self.fft_max_db
        db_range = max(db_max - db_min, 10.0)

        # Draw dB Horizontal Grids
        if self.chk_grid_var.get():
            step_db = 10
            curr_db = int(db_min // step_db * step_db)
            while curr_db <= db_max:
                y = pad_t + plot_h * (1.0 - (curr_db - db_min) / db_range)
                if pad_t <= y <= pad_t + plot_h:
                    canvas.create_line(pad_l, y, pad_l + plot_w, y, fill="#23232f", dash=(2, 4))
                    canvas.create_text(pad_l - 6, y, text=f"{curr_db} dB", fill="#6c7a89", font=("Segoe UI", 7), anchor="e")
                curr_db += step_db

        # Draw Frequency Vertical Grids & Labels
        start_f = self.center_freq - (self.sample_rate / 2.0)
        end_f = self.center_freq + (self.sample_rate / 2.0)
        span_f = self.sample_rate

        if self.chk_grid_var.get():
            n_divs = 6
            for d in range(n_divs + 1):
                f_cur = start_f + (span_f * d / n_divs)
                x = pad_l + (plot_w * d / n_divs)
                canvas.create_line(x, pad_t, x, pad_t + plot_h, fill="#23232f", dash=(2, 4))
                canvas.create_text(x, pad_t + plot_h + 12, text=f"{f_cur / 1e6:.3f} MHz", fill="#6c7a89", font=("Segoe UI", 7))

        # Astronomical Neutral Hydrogen Line Marker (1420.40575 MHz)
        if start_f <= HI_REST_FREQ_HZ <= end_f:
            hi_x = pad_l + plot_w * ((HI_REST_FREQ_HZ - start_f) / span_f)
            canvas.create_line(hi_x, pad_t, hi_x, pad_t + plot_h, fill="#ff4757", width=1, dash=(3, 3))
            canvas.create_text(hi_x, pad_t + 8, text="HI Rest (1420.406 MHz)", fill="#ff4757", font=("Segoe UI", 8, "bold"), anchor="s")

        # Central DC LO Center Marker
        dc_x = pad_l + plot_w * 0.5
        canvas.create_line(dc_x, pad_t, dc_x, pad_t + plot_h, fill="#576574", width=1, dash=(2, 2))

        # Downsample power array to plot_w pixels
        x_indices = np.linspace(0, len(power) - 1, int(plot_w)).astype(int)
        sampled_power = power[x_indices]

        # Convert to canvas coordinates
        y_pts = np.clip(pad_t + plot_h * (1.0 - (sampled_power - db_min) / db_range), pad_t, pad_t + plot_h)
        x_pts = np.linspace(pad_l, pad_l + plot_w, len(y_pts))

        points = []
        for x, y in zip(x_pts, y_pts):
            points.extend([float(x), float(y)])

        # Draw RF Spectrum Curve
        if len(points) >= 4:
            canvas.create_line(points, fill="#00d2d3", width=1.5)

        # Peak indicator HUD
        max_idx = np.argmax(power)
        peak_f = start_f + (span_f * max_idx / len(power))
        peak_p = power[max_idx]
        hud_text = f"Peak: {peak_f / 1e6:.4f} MHz | {peak_p:.1f} dBFS | Center LO: {self.center_freq / 1e6:.3f} MHz"
        canvas.create_text(w - pad_r, pad_t - 6, text=hud_text, fill="#00d2d3", font=("Segoe UI", 8), anchor="e")

    def _draw_waterfall_slice(self, power: np.ndarray):
        w = max(self.waterfall_canvas.winfo_width(), 100)
        h = max(self.waterfall_canvas.winfo_height(), 50)

        if self.waterfall_history.shape[1] != w or self.waterfall_history.shape[0] != h:
            self.waterfall_height = h
            self.waterfall_width = w
            self.waterfall_history = np.zeros((h, w, 3), dtype=np.uint8)

        # Roll history down by 1 row
        self.waterfall_history[1:] = self.waterfall_history[:-1]

        # Resample power to width w
        x_idx = np.linspace(0, len(power) - 1, w).astype(int)
        row_p = power[x_idx]

        # Normalize to 0..255 using dB min/max
        norm = np.clip((row_p - self.fft_min_db) / max(self.fft_max_db - self.fft_min_db, 1.0), 0.0, 1.0)
        indices = (norm * 255.0).astype(np.uint8)

        # Apply palette lookup
        self.waterfall_history[0] = self.palette_lut[indices]

        # Blit to Tkinter Canvas
        img = Image.fromarray(self.waterfall_history)
        self.waterfall_image_tk = ImageTk.PhotoImage(image=img)
        self.waterfall_canvas.delete("all")
        self.waterfall_canvas.create_image(0, 0, image=self.waterfall_image_tk, anchor="nw")

    def _on_close(self):
        """Graceful shutdown."""
        if self.writer.is_recording:
            self.writer.stop_recording()
        self.sdr.stop()
        self.sdr.close()
        self.destroy()


def main():
    app = SatDumpRecorderApp()
    app.mainloop()


if __name__ == "__main__":
    main()
