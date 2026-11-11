#!/usr/bin/env python3
"""
Hydrogen Line Live SDR Receiver & CS16 Recorder (SatDump-Style)
High-performance, ultra-responsive real-time spectrum and waterfall display,
universal multi-frequency tuning, and lossless CS16 recording with accompanied parameters.
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

# Astronomical Rest Frequency
HI_REST_FREQ_HZ = 1420405751.786  # 1420.405752 MHz


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
    """High-Performance SatDump-Style SDR Receiver & CS16 Recording Application."""

    def __init__(self):
        super().__init__()
        self.title("Hydrogen Line SDR Live Receiver & CS16 Recorder")
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
        self.fft_max_db = -50.0
        self.fft_min_db = -105.0
        self.avg_factor = 0.65
        self.palette_name = "Classic [Youssef Touil]"
        self.palette_lut = get_palette_lut(self.palette_name)
        self.show_waterfall = True
        self.show_grid = True

        # Thread-safe sample transfer
        self.latest_iq_lock = threading.Lock()
        self.latest_iq: Optional[np.ndarray] = None
        self.sdr.add_callback(self._on_sdr_samples)

        self.power_smooth: Optional[np.ndarray] = None
        self.waterfall_height = 260
        self.waterfall_width = 800
        self.waterfall_history = np.zeros((self.waterfall_height, self.waterfall_width, 3), dtype=np.uint8)
        self.waterfall_image_tk: Optional[ImageTk.PhotoImage] = None
        self.waterfall_img_id = None
        self.last_waterfall_time = 0.0

        # Build UI
        self._setup_styles()
        self._create_layout()

        # Connect SDR
        self._refresh_devices()
        self.sdr.open()
        self.sdr.start()
        self._update_device_ui_state()

        # Responsive loops:
        # Spectrum update loop: ~30-40 FPS
        self.after(28, self._gui_update_loop)
        # Stats update loop: 4 FPS (250 ms)
        self.after(250, self._stats_update_loop)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

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
        style.configure("TButton", background="#2a2a36", foreground=fg_white, borderwidth=1, relief="flat", padding=3)
        style.map("TButton", background=[("active", "#3a3a4c"), ("pressed", "#4a4a60")])
        style.configure("TCombobox", fieldbackground="#282834", background="#282834", foreground=fg_white, arrowcolor=fg_white)
        style.configure("TCheckbutton", background=panel_bg, foreground=fg_white)
        style.map("TCheckbutton", background=[("active", panel_bg)])
        style.configure("TScale", background=panel_bg, troughcolor="#282834")

    def _create_layout(self):
        main_container = ttk.Frame(self)
        main_container.pack(fill=tk.BOTH, expand=True)

        # Left Sidebar (Controls)
        sidebar_frame = tk.Frame(main_container, bg="#1a1a22", width=345, padx=8, pady=8)
        sidebar_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 2))
        sidebar_frame.pack_propagate(False)

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
        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw", width=325)
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
        self.lbl_lo_offset = tk.Label(row_params, text="+0.094 MHz (LO offset)", bg="#1f1f26", fg="#00d2d3", font=("Segoe UI", 8))
        self.lbl_lo_offset.pack(side=tk.RIGHT)

        # DIGITAL FREQUENCY DISPLAY (Bold segmented format: 001.420.500.000 Hz)
        freq_box = tk.Frame(box, bg="#0d0d12", relief="sunken", bd=1, padx=4, pady=3)
        freq_box.pack(fill=tk.X, pady=(4, 2))
        self.freq_display_var = tk.StringVar(value="001.420.500.000 Hz")
        freq_lbl = tk.Label(freq_box, textvariable=self.freq_display_var, bg="#0d0d12", fg="#00ffc4",
                            font=("Consolas", 14, "bold"), cursor="hand2")
        freq_lbl.pack()
        freq_lbl.bind("<MouseWheel>", self._on_freq_scroll)
        freq_lbl.bind("<Button-1>", lambda e: self.entry_tune.focus_set())

        # DIRECT FREQUENCY TUNING INPUT BOX
        tune_row = tk.Frame(box, bg="#1f1f26")
        tune_row.pack(fill=tk.X, pady=(2, 4))
        tk.Label(tune_row, text="Tune:", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)
        self.entry_tune = tk.Entry(tune_row, bg="#282834", fg="#00ffc4", insertbackground="white",
                                   font=("Consolas", 9, "bold"), width=12)
        self.entry_tune.pack(side=tk.LEFT, padx=(4, 4))
        self.entry_tune.insert(0, f"{self.center_freq / 1e6:.3f}")
        self.entry_tune.bind("<Return>", lambda e: self._on_direct_tune_entered())
        tk.Label(tune_row, text="MHz", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)

        btn_tune = tk.Button(tune_row, text="Set", bg="#0078d7", fg="white", font=("Segoe UI", 8, "bold"),
                             relief="flat", padx=6, command=self._on_direct_tune_entered)
        btn_tune.pack(side=tk.RIGHT)

        # STEP BUTTONS (-1M, +1M, -100k, +100k, -10k, +10k)
        step_row = tk.Frame(box, bg="#1f1f26")
        step_row.pack(fill=tk.X, pady=2)
        tk.Button(step_row, text="-1M", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(-1000000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(step_row, text="+1M", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(1000000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(step_row, text="-100k", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(-100000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(step_row, text="+100k", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(100000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(step_row, text="-10k", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(-10000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)
        tk.Button(step_row, text="+10k", bg="#2a2a36", fg="#e0e0e8", font=("Segoe UI", 7), relief="flat",
                  command=lambda: self.step_frequency_hz(10000)).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1)

        # BAND PRESETS DROPDOWN
        preset_row = tk.Frame(box, bg="#1f1f26")
        preset_row.pack(fill=tk.X, pady=2)
        tk.Label(preset_row, text="Presets:", bg="#1f1f26", fg="#888898").pack(side=tk.LEFT)
        self.preset_combo_var = tk.StringVar(value="H I 1420.5M (LO +94k)")
        presets_list = [
            "H I 1420.5M (LO +94k)",
            "H I 1420.406M (Rest Line)",
            "H I 1420.2M (SatDump)",
            "FM 89.2 MHz (Broadcast)",
            "FM 100.0 MHz (Broadcast)",
            "Airband 121.5 MHz (ATIS)",
            "Ham 144.8 MHz (2m APRS)",
            "ISM 433.92 MHz (Telemetry)",
            "ADS-B 1090.0 MHz (Aviation)",
        ]
        preset_combo = ttk.Combobox(preset_row, textvariable=self.preset_combo_var, state="readonly", width=22,
                                    values=presets_list)
        preset_combo.pack(side=tk.RIGHT)
        preset_combo.bind("<<ComboboxSelected>>", self._on_preset_selected)

        # Samplerate
        sr_row = tk.Frame(box, bg="#1f1f26")
        sr_row.pack(fill=tk.X, pady=2)
        tk.Label(sr_row, text="Samplerate:", bg="#1f1f26", fg="#e0e0e8").pack(side=tk.LEFT)
        self.sr_var = tk.StringVar(value="2.4 Msps")
        sr_combo = ttk.Combobox(sr_row, textvariable=self.sr_var, state="readonly", width=12,
                                values=["1.024 Msps", "2.048 Msps", "2.4 Msps", "2.56 Msps", "3.2 Msps"])
        sr_combo.pack(side=tk.RIGHT)
        sr_combo.bind("<<ComboboxSelected>>", self._on_samplerate_changed)

        # Correction (ppm)
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
        check_row.pack(fill=tk.X, pady=3)
        self.agc_var = tk.BooleanVar(value=False)
        self.agc_chk = ttk.Checkbutton(check_row, text="AGC", variable=self.agc_var, command=self._on_agc_toggled)
        self.agc_chk.pack(side=tk.LEFT, padx=(0, 10))

        self.bias_var = tk.BooleanVar(value=False)
        self.bias_chk = ttk.Checkbutton(check_row, text="Bias-Tee (4.5V)", variable=self.bias_var, command=self._on_bias_toggled)
        self.bias_chk.pack(side=tk.LEFT)

        # SDR Start / Stop Toggle
        self.sdr_btn = tk.Button(box, text="Stop SDR", bg="#c0392b", fg="white", font=("Segoe UI", 9, "bold"),
                                 relief="flat", pady=4, command=self._toggle_sdr_streaming)
        self.sdr_btn.pack(fill=tk.X, pady=(5, 2))

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
        ttk.Checkbutton(row_view, text="Frequency Scale", variable=self.chk_grid_var, command=self._redraw_grid_lines).pack(side=tk.RIGHT)

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

        # Accompanied file note
        self.lbl_txt_sidecar = tk.Label(box, text="伴 Saves: .cs16 + .txt parameters", bg="#1f1f26", fg="#00d2d3",
                                        font=("Segoe UI", 7), anchor="w")
        self.lbl_txt_sidecar.pack(fill=tk.X, pady=1)

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
        self.spectrum_canvas.bind("<Configure>", self._on_spectrum_resize)

        # Create persistent items on spectrum canvas
        self.spec_line_id = self.spectrum_canvas.create_line(0, 0, 0, 0, fill="#00d2d3", width=1.5, tags="spec_line")
        self.hi_marker_id = self.spectrum_canvas.create_line(0, 0, 0, 0, fill="#ff4757", width=1, dash=(3, 3), tags="hi_marker")
        self.hi_text_id = self.spectrum_canvas.create_text(0, 0, text="HI Rest (1420.406 MHz)", fill="#ff4757",
                                                           font=("Segoe UI", 8, "bold"), tags="hi_text")
        self.dc_marker_id = self.spectrum_canvas.create_line(0, 0, 0, 0, fill="#576574", width=1, dash=(2, 2), tags="dc_marker")
        self.hud_text_id = self.spectrum_canvas.create_text(0, 0, text="", fill="#00d2d3", font=("Segoe UI", 8), anchor="e", tags="hud")

        # Waterfall Canvas (Bottom)
        self.waterfall_canvas = tk.Canvas(parent, bg="#0d0d12", highlightthickness=0, height=260)
        self.waterfall_canvas.pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True)
        self.waterfall_canvas.bind("<Configure>", self._on_waterfall_resize)

        # Initial persistent PhotoImage for waterfall
        initial_img = Image.fromarray(self.waterfall_history)
        self.waterfall_image_tk = ImageTk.PhotoImage(image=initial_img)
        self.waterfall_img_id = self.waterfall_canvas.create_image(0, 0, image=self.waterfall_image_tk, anchor="nw")

    def _on_spectrum_resize(self, event):
        self._redraw_grid_lines()

    def _on_waterfall_resize(self, event):
        w = max(event.width, 100)
        h = max(event.height, 50)
        if self.waterfall_width != w or self.waterfall_height != h:
            self.waterfall_width = w
            self.waterfall_height = h
            self.waterfall_history = np.zeros((h, w, 3), dtype=np.uint8)
            new_img = Image.fromarray(self.waterfall_history)
            self.waterfall_image_tk = ImageTk.PhotoImage(image=new_img)
            self.waterfall_canvas.itemconfig(self.waterfall_img_id, image=self.waterfall_image_tk)

    def _redraw_grid_lines(self):
        canvas = self.spectrum_canvas
        canvas.delete("grid_line")
        canvas.delete("grid_text")

        if not self.chk_grid_var.get():
            return

        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 100 or h < 50:
            return

        pad_l, pad_r, pad_t, pad_b = 60, 20, 20, 30
        plot_w = w - pad_l - pad_r
        plot_h = h - pad_t - pad_b

        db_min = self.fft_min_db
        db_max = self.fft_max_db
        db_range = max(db_max - db_min, 10.0)

        # dB horizontal lines
        step_db = 10
        curr_db = int(db_min // step_db * step_db)
        while curr_db <= db_max:
            y = pad_t + plot_h * (1.0 - (curr_db - db_min) / db_range)
            if pad_t <= y <= pad_t + plot_h:
                canvas.create_line(pad_l, y, pad_l + plot_w, y, fill="#23232f", dash=(2, 4), tags="grid_line")
                canvas.create_text(pad_l - 6, y, text=f"{curr_db} dB", fill="#6c7a89", font=("Segoe UI", 7),
                                   anchor="e", tags="grid_text")
            curr_db += step_db

        # Frequency vertical lines
        start_f = self.center_freq - (self.sample_rate / 2.0)
        span_f = self.sample_rate
        n_divs = 6
        for d in range(n_divs + 1):
            f_cur = start_f + (span_f * d / n_divs)
            x = pad_l + (plot_w * d / n_divs)
            canvas.create_line(x, pad_t, x, pad_t + plot_h, fill="#23232f", dash=(2, 4), tags="grid_line")
            canvas.create_text(x, pad_t + plot_h + 12, text=f"{f_cur / 1e6:.3f} MHz", fill="#6c7a89",
                               font=("Segoe UI", 7), tags="grid_text")

        canvas.tag_lower("grid_line")
        canvas.tag_lower("grid_text")

    def _on_sdr_samples(self, raw_u8: np.ndarray):
        with self.latest_iq_lock:
            self.latest_iq = raw_u8

    def _refresh_devices(self):
        blocking = detect_blocking_processes(force=True)
        devices = self.sdr.scan_devices()

        if blocking:
            procs = ", ".join(blocking)
            self.status_label.config(text=f"Device held by {procs} (Virtual mode)", fg="#ff9f43")
        elif self.sdr.dll and self.sdr.dll.rtlsdr_get_device_count() > 0:
            self.status_label.config(text=f"RTL-SDR dongle ready ({len(devices)} found)", fg="#2ed573")
        else:
            self.status_label.config(text="No RTL-SDR dongle plugged in (Virtual SDR active)", fg="#ff9f43")

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
            self.status_label.config(text=f"Hardware: {self.sdr.device_name}", fg="#2ed573")

        gains = self.sdr.get_valid_gains()
        if gains:
            self.gain_scale.config(from_=gains[0], to=gains[-1])

    def set_frequency_hz(self, freq_hz: int):
        """Set frequency in Hz and update all UI labels."""
        self.center_freq = max(1000000, int(freq_hz))
        self.sdr.set_frequency(self.center_freq)
        self._update_frequency_display()
        self.entry_tune.delete(0, tk.END)
        self.entry_tune.insert(0, f"{self.center_freq / 1e6:.3f}")
        self._redraw_grid_lines()

    def step_frequency_hz(self, delta_hz: int):
        self.set_frequency_hz(self.center_freq + delta_hz)

    def _on_freq_scroll(self, event):
        delta = 100000 if event.delta > 0 else -100000
        self.step_frequency_hz(delta)

    def _on_direct_tune_entered(self):
        val_str = self.entry_tune.get().strip().lower()
        if not val_str:
            return
        try:
            # Parse units
            if val_str.endswith("m") or val_str.endswith("mhz"):
                f = float(val_str.rstrip("mhzsp")) * 1e6
            elif val_str.endswith("k") or val_str.endswith("khz"):
                f = float(val_str.rstrip("khzsp")) * 1e3
            else:
                v = float(val_str)
                # If entered as e.g. 1420.5 or 89.2, it's MHz
                f = v * 1e6 if v < 10000.0 else v
            self.set_frequency_hz(int(f))
        except ValueError:
            messagebox.showwarning("Invalid Frequency", "Please enter a valid frequency (e.g. 1420.5, 89.2M, 1420405752).")

    def _on_preset_selected(self, event=None):
        val = self.preset_combo_var.get()
        if "1420.5M" in val:
            self.set_frequency_hz(1420500000)
        elif "1420.406M" in val:
            self.set_frequency_hz(1420405752)
        elif "1420.2M" in val:
            self.set_frequency_hz(1420206000)
        elif "89.2 MHz" in val:
            self.set_frequency_hz(89200000)
        elif "100.0 MHz" in val:
            self.set_frequency_hz(100000000)
        elif "121.5 MHz" in val:
            self.set_frequency_hz(121500000)
        elif "144.8 MHz" in val:
            self.set_frequency_hz(144800000)
        elif "433.92 MHz" in val:
            self.set_frequency_hz(433920000)
        elif "1090.0 MHz" in val:
            self.set_frequency_hz(1090000000)

    def _update_frequency_display(self):
        f = self.center_freq
        ghz = f // 1000000000
        mhz = (f % 1000000000) // 1000000
        khz = (f % 1000000) // 1000
        hz = f % 1000
        self.freq_display_var.set(f"{ghz:03d}.{mhz:03d}.{khz:03d}.{hz:03d} Hz")

        # Update LO offset text
        offset_hz = self.center_freq - HI_REST_FREQ_HZ
        offset_khz = offset_hz / 1e3
        offset_mhz = offset_hz / 1e6
        if abs(offset_mhz) < 10.0:
            self.lbl_lo_offset.config(text=f"{offset_khz:+.1f} kHz (LO offset)", fg="#00d2d3")
        else:
            self.lbl_lo_offset.config(text=f"{self.center_freq / 1e6:.3f} MHz tuned", fg="#888898")

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
            self._redraw_grid_lines()

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
        self._redraw_grid_lines()

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
            if meta:
                txt_name = os.path.basename(meta.get("file_path", "")).replace(".cs16", ".txt")
                self.lbl_file.config(text=f"Saved: {txt_name} & .cs16")

    def _launch_analyzer_on_last_file(self):
        target_file = self.writer.current_filepath
        if not target_file or not os.path.exists(target_file):
            recordings = [os.path.join(self.recordings_dir, f) for f in os.listdir(self.recordings_dir) if f.endswith(".cs16")]
            if recordings:
                recordings.sort(key=lambda p: os.path.getmtime(p), reverse=True)
                target_file = recordings[0]
            else:
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
        """Update live recording stats at 4 FPS."""
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
        """High-speed lightweight update loop (~35 FPS) updating persistent canvas items."""
        raw_u8 = None
        with self.latest_iq_lock:
            if self.latest_iq is not None:
                raw_u8 = self.latest_iq
                self.latest_iq = None

        if raw_u8 is not None and len(raw_u8) >= (self.fft_size * 2):
            n_samples = self.fft_size
            u8_slice = raw_u8[: n_samples * 2]
            i_samples = (u8_slice[0::2].astype(np.float32) - 127.5) / 128.0
            q_samples = (u8_slice[1::2].astype(np.float32) - 127.5) / 128.0
            iq = i_samples + 1j * q_samples

            window = np.hanning(n_samples)
            fft_data = np.fft.fftshift(np.fft.fft(iq * window))
            power = 10 * np.log10(np.abs(fft_data)**2 / n_samples + 1e-12)

            if self.power_smooth is None or len(self.power_smooth) != len(power):
                self.power_smooth = power
            else:
                self.power_smooth = self.avg_factor * self.power_smooth + (1.0 - self.avg_factor) * power

            self._update_spectrum_coords(self.power_smooth)

            # Throttled waterfall update (every 50ms = 20 FPS)
            now = time.time()
            if self.show_waterfall and (now - self.last_waterfall_time) >= 0.050:
                self._update_waterfall_fast(self.power_smooth)
                self.last_waterfall_time = now

        self.after(28, self._gui_update_loop)

    def _update_spectrum_coords(self, power: np.ndarray):
        """Update spectrum line coordinates without allocating new canvas objects."""
        canvas = self.spectrum_canvas
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 100 or h < 50:
            return

        pad_l, pad_r, pad_t, pad_b = 60, 20, 20, 30
        plot_w = w - pad_l - pad_r
        plot_h = h - pad_t - pad_b

        db_min = self.fft_min_db
        db_max = self.fft_max_db
        db_range = max(db_max - db_min, 10.0)

        start_f = self.center_freq - (self.sample_rate / 2.0)
        end_f = self.center_freq + (self.sample_rate / 2.0)
        span_f = self.sample_rate

        # 1. Update HI Rest line marker (1420.40575 MHz)
        if start_f <= HI_REST_FREQ_HZ <= end_f:
            hi_x = pad_l + plot_w * ((HI_REST_FREQ_HZ - start_f) / span_f)
            canvas.coords(self.hi_marker_id, hi_x, pad_t, hi_x, pad_t + plot_h)
            canvas.coords(self.hi_text_id, hi_x, pad_t + 8)
            canvas.itemconfig(self.hi_marker_id, state="normal")
            canvas.itemconfig(self.hi_text_id, state="normal")
        else:
            canvas.itemconfig(self.hi_marker_id, state="hidden")
            canvas.itemconfig(self.hi_text_id, state="hidden")

        # 2. Update LO Center marker
        dc_x = pad_l + plot_w * 0.5
        canvas.coords(self.dc_marker_id, dc_x, pad_t, dc_x, pad_t + plot_h)

        # 3. Downsample and update spectrum curve coordinates
        num_pts = min(int(plot_w), len(power))
        x_indices = np.linspace(0, len(power) - 1, num_pts).astype(int)
        sampled_power = power[x_indices]

        y_pts = np.clip(pad_t + plot_h * (1.0 - (sampled_power - db_min) / db_range), pad_t, pad_t + plot_h)
        x_pts = np.linspace(pad_l, pad_l + plot_w, num_pts)

        coords = np.empty(num_pts * 2, dtype=np.float32)
        coords[0::2] = x_pts
        coords[1::2] = y_pts

        canvas.coords(self.spec_line_id, *coords.tolist())

        # 4. HUD Readout text
        max_idx = np.argmax(power)
        peak_f = start_f + (span_f * max_idx / len(power))
        peak_p = power[max_idx]
        canvas.coords(self.hud_text_id, w - pad_r, pad_t - 6)
        canvas.itemconfig(self.hud_text_id, text=f"Peak: {peak_f / 1e6:.4f} MHz | {peak_p:.1f} dBFS | Center: {self.center_freq / 1e6:.3f} MHz")

    def _update_waterfall_fast(self, power: np.ndarray):
        """In-place fast waterfall blit using PhotoImage.paste (0.1ms per frame)."""
        w = self.waterfall_width
        h = self.waterfall_height
        if w < 50 or h < 30:
            return

        # Roll array down in memory
        self.waterfall_history[1:] = self.waterfall_history[:-1]

        # Resample power to width w
        x_idx = np.linspace(0, len(power) - 1, w).astype(int)
        row_p = power[x_idx]

        norm = np.clip((row_p - self.fft_min_db) / max(self.fft_max_db - self.fft_min_db, 1.0), 0.0, 1.0)
        indices = (norm * 255.0).astype(np.uint8)

        # Palette lookup
        self.waterfall_history[0] = self.palette_lut[indices]

        # In-place paste to existing Tk PhotoImage
        img = Image.fromarray(self.waterfall_history)
        if self.waterfall_image_tk:
            self.waterfall_image_tk.paste(img)

    def _on_close(self):
        """Graceful application shutdown."""
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
