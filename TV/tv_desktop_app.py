#!/usr/bin/env python3
"""
Greek TV & Radio SDR Live Tuner - Native Desktop Application
Embedded native video & audio playback powered by libvlc and C++ Demodulator Engine (tv_demod.dll).
Strictly native: zero web browser dependency.
"""

import os
import sys
import time
import ctypes
import subprocess
import tkinter as tk
from tkinter import ttk, messagebox
import numpy as np

current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

# Clean English Channel Database
CHANNELS = [
    { "id": "ert1", "name": "ERT 1", "category": "Public TV", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT1/default/index.m3u8", "radio": False },
    { "id": "ert2", "name": "ERT 2", "category": "Public TV", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT2/default/index.m3u8", "radio": False },
    { "id": "ert3", "name": "ERT 3", "category": "Public TV", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERT3/default/index.m3u8", "radio": False },
    { "id": "ert_news", "name": "ERT News", "category": "Public TV", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTNews/default/index.m3u8", "radio": False },
    { "id": "vouli", "name": "Parliament TV", "category": "Parliament TV", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/VOULITV/default/index.m3u8", "radio": False },
    { "id": "ert_cosmos", "name": "ERT Cosmos", "category": "Public TV", "mux": "ERT MUX 2", "uhf": 28, "freq": 530.0, "url": "https://ert-ucdn.broadpeak-aas.com/bpk-tv/ERTCosmos/default/index.m3u8", "radio": False },
    { "id": "star", "name": "Star Channel", "category": "Commercial TV", "mux": "Digea MUX 2", "uhf": 27, "freq": 522.0, "url": "http://livestar.siliconweb.com/starvod/star4/star4.m3u8", "radio": False },
    { "id": "naftemporiki", "name": "Naftemporiki TV", "category": "Business TV", "mux": "Regional MUX", "uhf": 29, "freq": 538.0, "url": "https://telmaco.ascdn.broadpeak.io/nafteboriki/default/index.m3u8", "radio": False },
    { "id": "vergina", "name": "Vergina TV", "category": "Regional TV", "mux": "Regional MUX", "uhf": 29, "freq": 538.0, "url": "https://verginanews.gr:8443/hls_live/stream1.m3u8", "radio": False },
    { "id": "astra", "name": "Astra TV", "category": "Regional TV", "mux": "Regional MUX", "uhf": 30, "freq": 546.0, "url": "https://server.gointernet.gr/live/livestream.m3u8", "radio": False },
    { "id": "center", "name": "Center TV", "category": "Regional TV", "mux": "Regional MUX", "uhf": 29, "freq": 538.0, "url": "https://eu1.streams.gr:8081/centertv/index.m3u8", "radio": False },
    { "id": "trt", "name": "TRT Thessaly", "category": "Regional TV", "mux": "Regional MUX", "uhf": 30, "freq": 546.0, "url": "https://av.hellasnet.tv/rst/trt/main_stream.m3u8", "radio": False },
    { "id": "proto", "name": "ERT Radio 1 (News)", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-proto", "radio": True },
    { "id": "deftero", "name": "ERT Radio 2 (Music)", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-deftero", "radio": True },
    { "id": "trito", "name": "ERT Radio 3 (Culture)", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-trito", "radio": True },
    { "id": "erasport", "name": "ERT ERA Sport", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-erasport", "radio": True },
    { "id": "kosmos", "name": "ERT Kosmos", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-kosmos", "radio": True },
    { "id": "voice_of_greece", "name": "ERT Voice of Greece", "category": "Radio", "mux": "ERT MUX 1", "uhf": 21, "freq": 474.0, "url": "https://radiostreaming.ert.gr/ert-voiceofgreece", "radio": True }
]

REGIONS = [
    { "id": "attica", "name": "Athens / Attica", "ert1": 21, "ert2": 28, "digea1": 22, "digea2": 27, "reg": 29 },
    { "id": "thessaloniki", "name": "Thessaloniki / Central Macedonia", "ert1": 24, "ert2": 28, "digea1": 29, "digea2": 31, "reg": 30 },
    { "id": "patras", "name": "Patras / Western Greece", "ert1": 24, "ert2": 31, "digea1": 22, "digea2": 27, "reg": 30 },
    { "id": "thessaly", "name": "Larissa / Thessaly", "ert1": 21, "ert2": 28, "digea1": 24, "digea2": 26, "reg": 30 },
    { "id": "crete", "name": "Heraklion / Crete", "ert1": 21, "ert2": 24, "digea1": 23, "digea2": 25, "reg": 29 },
    { "id": "epirus", "name": "Ioannina / Epirus", "ert1": 21, "ert2": 28, "digea1": 22, "digea2": 25, "reg": 29 }
]

def get_channel_freq(ch, reg):
    uhf = ch["uhf"]
    mux = ch["mux"]
    if "ERT MUX 1" in mux: uhf = reg["ert1"]
    elif "ERT MUX 2" in mux: uhf = reg["ert2"]
    elif "Digea MUX 1" in mux: uhf = reg["digea1"]
    elif "Digea MUX 2" in mux: uhf = reg["digea2"]
    elif "Regional" in mux: uhf = reg["reg"]
    return 470.0 + (uhf - 21) * 8.0 + 4.0

# ---------------------------------------------------------------------------
# NATIVE VLC EMBEDDING & EXECUTABLE DETECTION (PORTABLE)
# ---------------------------------------------------------------------------

def get_vlc_search_dirs():
    dirs = []
    local_dir = os.path.dirname(os.path.abspath(__file__))
    local_vlc = os.path.join(local_dir, "vlc")
    if os.path.exists(local_vlc):
        dirs.append(local_vlc)

    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\VideoLAN\VLC")
        val, _ = winreg.QueryValueEx(key, "InstallDir")
        if os.path.exists(val):
            dirs.append(val)
    except Exception:
        pass

    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, r"VLC.m3u8\shell\Open\command")
        val, _ = winreg.QueryValueEx(key, "")
        if val.startswith('"'):
            exe_p = val.split('"')[1]
            p_dir = os.path.dirname(exe_p)
            if os.path.exists(p_dir):
                dirs.append(p_dir)
    except Exception:
        pass

    prog_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    prog_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    dirs.extend([
        os.path.join(prog_files, "VideoLAN", "VLC"),
        os.path.join(prog_files_x86, "VideoLAN", "VLC")
    ])

    try:
        import shutil
        vlc_in_path = shutil.which("vlc")
        if vlc_in_path:
            dirs.append(os.path.dirname(vlc_in_path))
    except Exception:
        pass

    return [d for d in dirs if os.path.exists(d)]

HAS_VLC_LIB = False
vlc_module = None

for vp in get_vlc_search_dirs():
    try:
        os.add_dll_directory(vp)
        import vlc
        vlc_module = vlc
        HAS_VLC_LIB = True
        break
    except Exception:
        pass

def find_native_player_exe():
    for d in get_vlc_search_dirs():
        exe = os.path.join(d, "vlc.exe")
        if os.path.exists(exe):
            return exe

    import shutil
    for name in ["vlc", "mpv", "wmplayer"]:
        p = shutil.which(name)
        if p and os.path.exists(p):
            return p

    wmplayers = [
        r"C:\Program Files\Windows Media Player\wmplayer.exe",
        r"C:\Program Files (x86)\Windows Media Player\wmplayer.exe"
    ]
    for w in wmplayers:
        if os.path.exists(w):
            return w
    return None

def launch_native_player_exe(url):
    exe_path = find_native_player_exe()
    if exe_path:
        if "vlc.exe" in exe_path.lower():
            subprocess.Popen([exe_path, "--one-instance", "--no-video-title-show", url])
        else:
            subprocess.Popen([exe_path, url])
        return True
    return False

# ---------------------------------------------------------------------------
# C++ DEMODULATOR DLL BINDINGS
# ---------------------------------------------------------------------------

class DemodMetrics(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('center_freq_hz', ctypes.c_double),
        ('sample_rate_hz', ctypes.c_double),
        ('rssi_dbfs', ctypes.c_float),
        ('peak_power_dbfs', ctypes.c_float),
        ('noise_floor_dbfs', ctypes.c_float),
        ('snr_db', ctypes.c_float),
        ('is_carrier_locked', ctypes.c_int32),
        ('quality_pct', ctypes.c_float),
        ('cfo_est_khz', ctypes.c_float),
        ('guard_interval_idx', ctypes.c_int32),
        ('is_hardware', ctypes.c_int32),
        ('sdr_status', ctypes.c_int32),
        ('status_message', ctypes.c_char * 128)
    ]

class ScanChannelResult(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('uhf_channel', ctypes.c_int32),
        ('freq_mhz', ctypes.c_double),
        ('rssi_dbfs', ctypes.c_float),
        ('snr_db', ctypes.c_float),
        ('is_locked', ctypes.c_int32),
        ('multiplex_name', ctypes.c_char * 32)
    ]

dll_path = os.path.join(current_dir, "tv_demod.dll")
if not os.path.exists(dll_path):
    dll_path = os.path.join(current_dir, "..", "tv_demod.dll")

demod_dll = None
try:
    demod_dll = ctypes.CDLL(dll_path)
    demod_dll.tv_demod_init.argtypes = [ctypes.c_uint32]
    demod_dll.tv_demod_init.restype = ctypes.c_int
    demod_dll.tv_demod_shutdown.argtypes = []
    demod_dll.tv_demod_shutdown.restype = None
    demod_dll.tv_demod_connect.argtypes = []
    demod_dll.tv_demod_connect.restype = ctypes.c_int
    demod_dll.tv_demod_disconnect.argtypes = []
    demod_dll.tv_demod_disconnect.restype = None
    demod_dll.tv_demod_set_frequency.argtypes = [ctypes.c_uint32]
    demod_dll.tv_demod_set_frequency.restype = ctypes.c_int
    demod_dll.tv_demod_set_gain.argtypes = [ctypes.c_float, ctypes.c_int]
    demod_dll.tv_demod_set_gain.restype = ctypes.c_int
    demod_dll.tv_demod_set_mode.argtypes = [ctypes.c_int]
    demod_dll.tv_demod_set_mode.restype = None
    demod_dll.tv_demod_enable_audio_playback.argtypes = [ctypes.c_int]
    demod_dll.tv_demod_enable_audio_playback.restype = None
    demod_dll.tv_demod_get_psd.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.c_int]
    demod_dll.tv_demod_get_psd.restype = ctypes.c_int
    demod_dll.tv_demod_get_metrics.argtypes = [ctypes.POINTER(DemodMetrics)]
    demod_dll.tv_demod_get_metrics.restype = ctypes.c_int
    demod_dll.tv_demod_run_uhf_scan.argtypes = [ctypes.POINTER(ScanChannelResult), ctypes.c_int]
    demod_dll.tv_demod_run_uhf_scan.restype = ctypes.c_int
except Exception:
    pass

# ---------------------------------------------------------------------------
# NATIVE DESKTOP GUI WITH EMBEDDED NATIVE VIDEO
# ---------------------------------------------------------------------------

class GreekTvDesktopApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Greek TV SDR Tuner")
        self.geometry("1240x760")
        self.minsize(1050, 680)
        self.configure(bg="#141416")

        if demod_dll:
            demod_dll.tv_demod_init(2400000)
            demod_dll.tv_demod_connect()

        self.region_idx = 0
        self.ch_idx = 0
        self.demod_audio_enabled = False
        self.vlc_playing = False
        self.demod_mode = 0

        # Embedded VLC Media Player
        self.vlc_instance = None
        self.vlc_player = None
        if HAS_VLC_LIB:
            try:
                self.vlc_instance = vlc_module.Instance("--quiet --no-video-title-show")
                self.vlc_player = self.vlc_instance.media_player_new()
            except Exception:
                self.vlc_player = None

        self.psd_buf = (ctypes.c_float * 2048)()
        self.freq_buf = (ctypes.c_float * 2048)()

        self.wf_rows = 40
        self.wf_cols = 100
        self.wf_history = np.zeros((self.wf_rows, self.wf_cols), dtype=np.float32)

        self._apply_theme()
        self._build_ui()
        self._tune_current_channel()

        # Start 30 FPS update loop
        self._timer_id = self.after(33, self._dsp_update_loop)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _apply_theme(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background="#141416", foreground="#d8d8dc", font=("Segoe UI", 9))
        style.configure("TFrame", background="#141416")
        style.configure("Sidebar.TFrame", background="#1a1a1e")
        style.configure("Card.TFrame", background="#202026", relief="solid", borderwidth=1)
        style.configure("TLabel", background="#141416", foreground="#d8d8dc")
        style.configure("Header.TLabel", font=("Segoe UI", 12, "bold"), foreground="#e4e4e8")
        style.configure("Sub.TLabel", font=("Segoe UI", 8), foreground="#808088")
        style.configure("TButton", background="#2a2a32", foreground="#ffffff", font=("Segoe UI", 9))
        style.map("TButton", background=[("active", "#3a3a46"), ("pressed", "#202028")])
        style.configure("TCombobox", fieldbackground="#24242c", background="#24242c", foreground="#ffffff")

    def _build_ui(self):
        self.paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        self.paned.pack(fill=tk.BOTH, expand=True)

        # -------------------------------------------------------------
        # SIDEBAR (Width 290)
        # -------------------------------------------------------------
        self.side_frame = ttk.Frame(self.paned, style="Sidebar.TFrame", width=290, padding=12)
        self.paned.add(self.side_frame, weight=0)

        title_lbl = ttk.Label(self.side_frame, text="TV SDR Receiver", style="Header.TLabel", background="#1a1a1e")
        title_lbl.pack(anchor="w")
        sub_lbl = ttk.Label(self.side_frame, text="Terrestrial Broadcast Tuner", style="Sub.TLabel", background="#1a1a1e")
        sub_lbl.pack(anchor="w", pady=(0, 8))

        self.status_var = tk.StringVar(value="Hardware: Initializing...")
        self.status_lbl = tk.Label(
            self.side_frame, textvariable=self.status_var,
            bg="#24242c", fg="#a0a4b0", font=("Segoe UI", 8), padx=8, pady=4, relief="solid", bd=1,
            cursor="hand2"
        )
        self.status_lbl.pack(fill=tk.X, pady=(0, 10))
        self.status_lbl.bind("<Button-1>", lambda e: self._reconnect_sdr())

        ttk.Label(self.side_frame, text="Region:", font=("Segoe UI", 8, "bold"), background="#1a1a1e").pack(anchor="w")
        reg_names = [r["name"] for r in REGIONS]
        self.region_combo = ttk.Combobox(self.side_frame, values=reg_names, state="readonly")
        self.region_combo.current(0)
        self.region_combo.pack(fill=tk.X, pady=(2, 10))
        self.region_combo.bind("<<ComboboxSelected>>", self._on_region_changed)

        ttk.Label(self.side_frame, text="Stations:", font=("Segoe UI", 8, "bold"), background="#1a1a1e").pack(anchor="w")
        self.ch_listbox = tk.Listbox(
            self.side_frame, bg="#202026", fg="#dcdce0", selectbackground="#344865", selectforeground="#ffffff",
            font=("Segoe UI", 9), bd=1, relief="solid", highlightthickness=0
        )
        self.ch_listbox.pack(fill=tk.BOTH, expand=True, pady=(2, 10))
        self.ch_listbox.bind("<<ListboxSelect>>", self._on_channel_selected)
        self._populate_channels()

        btn_box = ttk.Frame(self.side_frame, style="Sidebar.TFrame")
        btn_box.pack(fill=tk.X, pady=(0, 4))
        self.stream_btn = ttk.Button(btn_box, text="Play Stream", command=self._toggle_stream)
        self.stream_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 2))
        self.popout_btn = ttk.Button(btn_box, text="Pop-out", command=self._popout_stream)
        self.popout_btn.pack(side=tk.RIGHT, fill=tk.X, expand=True, padx=(2, 0))

        btn_box2 = ttk.Frame(self.side_frame, style="Sidebar.TFrame")
        btn_box2.pack(fill=tk.X, pady=(0, 4))
        self.scan_btn = ttk.Button(btn_box2, text="Scan Band", command=self._open_scanner)
        self.scan_btn.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 2))
        m3u_btn = ttk.Button(btn_box2, text="Export M3U", command=self._export_m3u)
        m3u_btn.pack(side=tk.RIGHT, fill=tk.X, expand=True, padx=(2, 0))

        # -------------------------------------------------------------
        # MAIN DISPLAY (Embedded Video Player + RF Spectrum + Waterfall)
        # -------------------------------------------------------------
        self.main_frame = ttk.Frame(self.paned, padding=12)
        self.paned.add(self.main_frame, weight=1)

        # Header
        top_header = ttk.Frame(self.main_frame)
        top_header.pack(fill=tk.X, pady=(0, 6))

        self.stn_title_var = tk.StringVar(value="ERT 1")
        ttk.Label(top_header, textvariable=self.stn_title_var, font=("Segoe UI", 13, "bold"), foreground="#ffffff").pack(side=tk.LEFT)

        self.stn_meta_var = tk.StringVar(value="Public TV  |  ERT MUX 1  |  474.000 MHz")
        ttk.Label(top_header, textvariable=self.stn_meta_var, font=("Segoe UI", 8), foreground="#808590").pack(side=tk.RIGHT, padx=6)

        # Metrics Row
        m_frame = ttk.Frame(self.main_frame)
        m_frame.pack(fill=tk.X, pady=(0, 8))

        self.m_freq = tk.StringVar(value="474.000 MHz")
        self.m_rssi = tk.StringVar(value="-18.5 dBFS")
        self.m_snr = tk.StringVar(value="+14.2 dB")
        self.m_lock = tk.StringVar(value="Locked")
        self.m_qual = tk.StringVar(value="85%")

        self._add_card(m_frame, "Frequency", self.m_freq, "#d0d4dc")
        self._add_card(m_frame, "Signal Power", self.m_rssi, "#d0d4dc")
        self._add_card(m_frame, "SNR", self.m_snr, "#45b078")
        self._add_card(m_frame, "Carrier Lock", self.m_lock, "#45b078")
        self._add_card(m_frame, "Quality", self.m_qual, "#d0d4dc")

        # Split Container: Native Video Player (Left) + Spectrum Analyzer (Right)
        mid_container = ttk.Frame(self.main_frame)
        mid_container.pack(fill=tk.BOTH, expand=True, pady=(0, 6))

        # Video Player Frame (Native HWND container for VLC)
        self.video_box = ttk.Frame(mid_container, style="Card.TFrame", width=460)
        self.video_box.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 4))
        self.video_box.pack_propagate(False)

        # Embedded Video Screen
        self.video_screen = tk.Frame(self.video_box, bg="#08080a")
        self.video_screen.pack(fill=tk.BOTH, expand=True)

        self.video_overlay = tk.Label(
            self.video_screen, text="Select Station or Click 'Play Stream'",
            bg="#08080a", fg="#606575", font=("Segoe UI", 9)
        )
        self.video_overlay.place(relx=0.5, rely=0.5, anchor="center")

        # Attach VLC player to video screen HWND
        self.update_idletasks()
        if self.vlc_player:
            self.vlc_player.set_hwnd(self.video_screen.winfo_id())

        # Spectrum Analyzer Frame (Right)
        spec_box = ttk.Frame(mid_container, style="Card.TFrame")
        spec_box.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(4, 0))

        ttk.Label(spec_box, text="RF Power Spectrum (C++ AVX2):", font=("Segoe UI", 8, "bold"), foreground="#9095a4", background="#202026").pack(anchor="w", padx=6, pady=2)
        self.spec_canvas = tk.Canvas(spec_box, bg="#111114", bd=0, highlightthickness=0, height=220)
        self.spec_canvas.pack(fill=tk.BOTH, expand=True, padx=4, pady=(0, 4))

        # Spectrogram Waterfall (Full width below)
        wf_box = ttk.Frame(self.main_frame, style="Card.TFrame")
        wf_box.pack(fill=tk.X, pady=(0, 6))
        ttk.Label(wf_box, text="Live Spectrogram Waterfall:", font=("Segoe UI", 8, "bold"), foreground="#9095a4", background="#202026").pack(anchor="w", padx=6, pady=2)
        self.wf_canvas = tk.Canvas(wf_box, bg="#0d0d10", bd=0, highlightthickness=0, height=110)
        self.wf_canvas.pack(fill=tk.X, padx=4, pady=(0, 4))

        # Bottom Bar
        b_bar = ttk.Frame(self.main_frame, style="Card.TFrame", padding=6)
        b_bar.pack(fill=tk.X)

        ttk.Label(b_bar, text="C++ Demodulator Audio:", background="#202026", foreground="#a0a5b0").pack(side=tk.LEFT, padx=(0, 6))
        self.demod_audio_btn = tk.Button(
            b_bar, text="Audio: Muted", bg="#2c2c34", fg="#ffffff", font=("Segoe UI", 8),
            padx=8, pady=2, relief="flat", command=self._toggle_demod_audio
        )
        self.demod_audio_btn.pack(side=tk.LEFT, padx=(0, 12))

        ttk.Label(b_bar, text="Demod:", background="#202026", foreground="#a0a5b0").pack(side=tk.LEFT, padx=(0, 4))
        self.mode_combo = ttk.Combobox(b_bar, values=["DVB-T Analyzer", "WFM Audio", "AM Audio"], state="readonly", width=14)
        self.mode_combo.current(0)
        self.mode_combo.pack(side=tk.LEFT)
        self.mode_combo.bind("<<ComboboxSelected>>", self._on_mode_changed)

        ttk.Label(b_bar, text="Native Video: VLC Engine  |  Rate: 2.4 MSPS", font=("Segoe UI", 8), foreground="#707580", background="#202026").pack(side=tk.RIGHT)

    def _add_card(self, parent, label, var, color):
        c = ttk.Frame(parent, style="Card.TFrame", padding=(8, 4))
        c.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)
        ttk.Label(c, text=label, font=("Segoe UI", 7), foreground="#808590", background="#202026").pack(anchor="w")
        tk.Label(c, textvariable=var, font=("Consolas", 11, "bold"), fg=color, bg="#202026").pack(anchor="w")

    def _populate_channels(self):
        self.ch_listbox.delete(0, tk.END)
        reg = REGIONS[self.region_idx]
        for ch in CHANNELS:
            freq = get_channel_freq(ch, reg)
            self.ch_listbox.insert(tk.END, f"{ch['name']:<22} {freq:.1f} MHz")
        self.ch_listbox.select_set(self.ch_idx)

    def _on_channel_selected(self, event=None):
        sel = self.ch_listbox.curselection()
        if not sel: return
        self.ch_idx = sel[0]
        self._tune_current_channel()
        if self.vlc_playing:
            self._start_stream()

    def _on_region_changed(self, event=None):
        self.region_idx = self.region_combo.current()
        self._populate_channels()
        self._tune_current_channel()
        if self.vlc_playing:
            self._start_stream()

    def _reconnect_sdr(self):
        if demod_dll:
            self.status_var.set("Hardware: Reconnecting...")
            self.update_idletasks()
            demod_dll.tv_demod_connect()
            self._tune_current_channel()

    def _tune_current_channel(self):
        ch = CHANNELS[self.ch_idx]
        reg = REGIONS[self.region_idx]
        freq = get_channel_freq(ch, reg)
        self.stn_title_var.set(ch["name"])
        self.stn_meta_var.set(f"{ch['category']}  |  {ch['mux']}  |  Tuned LO: {freq:.3f} MHz")

        if ch["radio"]:
            self.demod_mode = 1
            self.mode_combo.current(1)
        else:
            self.demod_mode = 0
            self.mode_combo.current(0)

        if demod_dll:
            demod_dll.tv_demod_set_mode(ctypes.c_int(self.demod_mode))
            demod_dll.tv_demod_set_frequency(ctypes.c_uint32(int(freq * 1e6)))

    def _on_mode_changed(self, event=None):
        self.demod_mode = self.mode_combo.current()
        if demod_dll:
            demod_dll.tv_demod_set_mode(ctypes.c_int(self.demod_mode))

    def _toggle_stream(self):
        if self.vlc_playing:
            self._stop_stream()
        else:
            self._start_stream()

    def _start_stream(self):
        ch = CHANNELS[self.ch_idx]
        url = ch["url"]

        if self.vlc_player:
            try:
                self.video_overlay.place_forget()
                media = self.vlc_instance.media_new(url)
                self.vlc_player.set_media(media)
                self.vlc_player.play()
                self.vlc_playing = True
                self.stream_btn.config(text="Stop Stream")
                return
            except Exception:
                pass

        # Fallback to native VLC executable window
        if launch_native_player_exe(url):
            self.vlc_playing = True
            self.stream_btn.config(text="Stop Stream")

    def _stop_stream(self):
        if self.vlc_player:
            try:
                self.vlc_player.stop()
            except Exception:
                pass
        self.vlc_playing = False
        self.stream_btn.config(text="Play Stream")
        self.video_overlay.config(text="Playback Stopped")
        self.video_overlay.place(relx=0.5, rely=0.5, anchor="center")

    def _popout_stream(self):
        """Detach video into a dedicated native player window."""
        ch = CHANNELS[self.ch_idx]
        launch_native_player_exe(ch["url"])

    def _toggle_demod_audio(self):
        self.demod_audio_enabled = not self.demod_audio_enabled
        if self.demod_audio_enabled:
            self.demod_audio_btn.config(text="Audio: Active", bg="#2a6a42")
        else:
            self.demod_audio_btn.config(text="Audio: Muted", bg="#2c2c34")
        if demod_dll:
            demod_dll.tv_demod_enable_audio_playback(ctypes.c_int(1 if self.demod_audio_enabled else 0))

    def _export_m3u(self):
        p = os.path.join(current_dir, "channels.m3u8")
        reg = REGIONS[self.region_idx]
        lines = ["#EXTM3U"]
        for ch in CHANNELS:
            freq = get_channel_freq(ch, reg)
            lines.append(f'#EXTINF:-1 tvg-id="{ch["id"]}" tvg-name="{ch["name"]}" group-title="{ch["category"]}" radio="{"true" if ch["radio"] else "false"}",{ch["name"]} ({freq:.1f} MHz)')
            lines.append(ch["url"])
        with open(p, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        messagebox.showinfo("Export Complete", f"Playlist saved to:\n{p}")

    def _open_scanner(self):
        if not demod_dll: return
        results = (ScanChannelResult * 32)()
        n = demod_dll.tv_demod_run_uhf_scan(results, 32)
        self._tune_current_channel()

        w = tk.Toplevel(self)
        w.title("UHF Band Scan Results")
        w.geometry("540x420")
        w.configure(bg="#141416")

        ttk.Label(w, text="UHF Channels 21 - 48 (470 - 694 MHz)", font=("Segoe UI", 10, "bold")).pack(pady=8)
        tree = ttk.Treeview(w, columns=("ch", "freq", "rssi", "snr", "status", "mux"), show="headings", height=14)
        tree.heading("ch", text="CH")
        tree.heading("freq", text="Freq (MHz)")
        tree.heading("rssi", text="RSSI")
        tree.heading("snr", text="SNR")
        tree.heading("status", text="Status")
        tree.heading("mux", text="Multiplex")

        tree.column("ch", width=50, anchor="center")
        tree.column("freq", width=75, anchor="center")
        tree.column("rssi", width=75, anchor="center")
        tree.column("snr", width=65, anchor="center")
        tree.column("status", width=75, anchor="center")
        tree.column("mux", width=160, anchor="w")

        for i in range(n):
            r = results[i]
            mux_str = r.multiplex_name.decode("utf-8", errors="replace")
            stat = "Locked" if r.is_locked else "Search"
            tree.insert("", tk.END, values=(f"{r.uhf_channel}", f"{r.freq_mhz:.1f}", f"{r.rssi_dbfs:.1f}", f"+{r.snr_db:.1f}", stat, mux_str))

        tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=8)

    def _dsp_update_loop(self):
        if demod_dll:
            m = DemodMetrics()
            demod_dll.tv_demod_get_metrics(ctypes.byref(m))
            demod_dll.tv_demod_get_psd(self.freq_buf, self.psd_buf, 2048)

            self.m_freq.set(f"{m.center_freq_hz / 1e6:.3f} MHz")
            self.m_rssi.set(f"{m.rssi_dbfs:.1f} dBFS")
            self.m_snr.set(f"+{m.snr_db:.1f} dB")
            self.m_lock.set("Locked" if m.is_carrier_locked else "Searching")
            self.m_qual.set(f"{m.quality_pct:.0f}%")

            msg = m.status_message.decode("utf-8", errors="replace")
            if m.is_hardware:
                self.status_var.set(f"Hardware: RTL-SDR Active ({msg})")
                self.status_lbl.config(bg="#1c3624", fg="#50c080")
            elif m.sdr_status == 2:
                hint = f" ({msg})" if msg else ""
                self.status_var.set(f"Hardware Busy{hint} [Click to Retry]")
                self.status_lbl.config(bg="#3a2818", fg="#d09050")
            else:
                hint = f" ({msg})" if msg else ""
                self.status_var.set(f"Fallback Mode{hint} [Click to Reconnect]")
                self.status_lbl.config(bg="#24242c", fg="#9095a8")

            self._draw_spectrum(m)
            self._draw_waterfall()

        self._timer_id = self.after(33, self._dsp_update_loop)

    def _draw_spectrum(self, m):
        c = self.spec_canvas
        w = c.winfo_width()
        h = c.winfo_height()
        if w < 50 or h < 50: return

        c.delete("all")
        for db in range(-80, 0, 20):
            norm_y = (db - (-85.0)) / 75.0
            y = h - (norm_y * (h - 20)) - 10
            c.create_line(35, y, w - 10, y, fill="#1c1c22", dash=(2, 4))
            c.create_text(28, y, text=f"{db}", fill="#606470", font=("Segoe UI", 7), anchor="e")

        cx = 35 + (w - 45) // 2
        c.create_line(cx, 8, cx, h - 8, fill="#c05050", dash=(3, 3))

        nf_norm = (m.noise_floor_dbfs - (-85.0)) / 75.0
        nf_y = h - (nf_norm * (h - 20)) - 10
        c.create_line(35, nf_y, w - 10, nf_y, fill="#505560", dash=(2, 2))

        pts = []
        plot_w = w - 45
        plot_h = h - 20
        psd_arr = np.frombuffer(self.psd_buf, dtype=np.float32, count=2048)

        for i in range(0, 2048, 4):
            val = psd_arr[i]
            norm = max(0.0, min(1.0, (val - (-85.0)) / 75.0))
            x = 35 + (i * plot_w) / 2048
            y = h - (norm * plot_h) - 10
            pts.extend([x, y])

        if len(pts) >= 4:
            c.create_line(*pts, fill="#3880c8", width=2)

        c.create_text(42, 10, text=f"Center: {m.center_freq_hz/1e6:.3f} MHz  |  Span: 2.4 MHz  |  Noise: {m.noise_floor_dbfs:.1f} dBFS", fill="#8a90a0", font=("Segoe UI", 7), anchor="w")

    def _draw_waterfall(self):
        c = self.wf_canvas
        w = c.winfo_width()
        h = c.winfo_height()
        if w < 50 or h < 30: return

        c.delete("all")
        psd_arr = np.frombuffer(self.psd_buf, dtype=np.float32, count=2048)
        resampled = np.interp(np.linspace(0, 2048, self.wf_cols), np.arange(2048), psd_arr)
        norm_row = np.clip((resampled - (-85.0)) / 65.0, 0.0, 1.0)

        self.wf_history[1:] = self.wf_history[:-1]
        self.wf_history[0] = norm_row

        row_h = max(1, h / self.wf_rows)
        col_w = max(1, (w - 45) / self.wf_cols)

        for r in range(min(self.wf_rows, 30)):
            y = r * row_h
            for col in range(0, self.wf_cols, 2):
                val = self.wf_history[r, col]
                blue = int(min(220, val * 300))
                green = int(min(180, max(0, (val - 0.25) * 260)))
                red = int(min(180, max(0, (val - 0.5) * 360)))
                c.create_rectangle(35 + col * col_w, y, 35 + (col + 2) * col_w, y + row_h, fill=f"#{red:02x}{green:02x}{blue:02x}", outline="")

    def _on_close(self):
        if hasattr(self, "_timer_id") and self._timer_id:
            try:
                self.after_cancel(self._timer_id)
            except Exception:
                pass
            self._timer_id = None

        if self.vlc_player:
            try:
                self.vlc_player.stop()
                self.vlc_player.release()
            except Exception:
                pass
        if self.vlc_instance:
            try:
                self.vlc_instance.release()
            except Exception:
                pass

        if demod_dll:
            demod_dll.tv_demod_shutdown()
        self.destroy()

def main():
    app = GreekTvDesktopApp()
    app.mainloop()

if __name__ == "__main__":
    main()
