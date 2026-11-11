"""
Hardware RTL-SDR Interface via ctypes and fallback RF synthesizer.
Directly interfaces with rtlsdr.dll to tune and acquire raw I/Q samples from the antenna.
"""

import ctypes
import os
import sys
import time
from enum import Enum
from typing import Optional, Tuple, List
import numpy as np

class DeviceStatus(Enum):
    DISCONNECTED = "Disconnected"
    BUSY = "Occupied by another application"
    CONNECTED = "Connected & Active"
    SIMULATED = "Simulation Mode"

def find_rtlsdr_dll() -> Optional[str]:
    """Find rtlsdr.dll portably in the local application folder or system paths."""
    local_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(local_dir, "rtlsdr.dll"),
        os.path.join(local_dir, "..", "rtlsdr.dll"),
        "rtlsdr.dll"
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    return None

def detect_blocking_processes() -> List[str]:
    """Check if any known SDR process is currently running and might hold the USB device."""
    blocking = []
    try:
        import subprocess
        cmd = 'powershell -NoProfile -Command "Get-Process | Where-Object {$_.ProcessName -match \'satdump|sdrpp|sdrangel|tv_app|rtl_tcp|dump1090|welle\'} | Select-Object -ExpandProperty ProcessName"'
        out = subprocess.check_output(cmd, shell=True, text=True, timeout=3).strip()
        if out:
            blocking = [line.strip() for line in out.splitlines() if line.strip()]
    except Exception:
        pass
    return blocking

class RtlSdrReceiver:
    """Wrapper around rtlsdr.dll for real-time live SDR RF acquisition."""
    def __init__(self, sample_rate_hz: int = 2400000):
        self.dll_path = find_rtlsdr_dll()
        self.dll: Optional[ctypes.CDLL] = None
        self.dev = ctypes.c_void_p(0)
        self.status = DeviceStatus.DISCONNECTED
        self.status_detail = ""
        self.device_name = "Generic RTL2832U"
        self.sample_rate_hz = sample_rate_hz
        self.center_freq_hz = 474000000  # Default 474 MHz (Athens ERT MUX 1)
        self.gain_tenth_db = 0  # 0 = AGC
        self.is_open = False
        
        self._init_dll()

    def _init_dll(self):
        if not self.dll_path:
            self.status = DeviceStatus.DISCONNECTED
            self.status_detail = "rtlsdr.dll not found in workspace."
            return
        
        try:
            self.dll = ctypes.CDLL(self.dll_path)
            # Setup function signatures
            self.dll.rtlsdr_get_device_count.restype = ctypes.c_uint32
            self.dll.rtlsdr_get_device_name.restype = ctypes.c_char_p
            self.dll.rtlsdr_get_device_name.argtypes = [ctypes.c_uint32]
            
            self.dll.rtlsdr_open.restype = ctypes.c_int
            self.dll.rtlsdr_open.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint32]
            
            self.dll.rtlsdr_close.restype = ctypes.c_int
            self.dll.rtlsdr_close.argtypes = [ctypes.c_void_p]
            
            self.dll.rtlsdr_set_center_freq.restype = ctypes.c_int
            self.dll.rtlsdr_set_center_freq.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            
            self.dll.rtlsdr_get_center_freq.restype = ctypes.c_uint32
            self.dll.rtlsdr_get_center_freq.argtypes = [ctypes.c_void_p]
            
            self.dll.rtlsdr_set_sample_rate.restype = ctypes.c_int
            self.dll.rtlsdr_set_sample_rate.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            
            self.dll.rtlsdr_get_sample_rate.restype = ctypes.c_uint32
            self.dll.rtlsdr_get_sample_rate.argtypes = [ctypes.c_void_p]
            
            self.dll.rtlsdr_set_tuner_gain_mode.restype = ctypes.c_int
            self.dll.rtlsdr_set_tuner_gain_mode.argtypes = [ctypes.c_void_p, ctypes.c_int]
            
            self.dll.rtlsdr_set_tuner_gain.restype = ctypes.c_int
            self.dll.rtlsdr_set_tuner_gain.argtypes = [ctypes.c_void_p, ctypes.c_int]
            
            self.dll.rtlsdr_reset_buffer.restype = ctypes.c_int
            self.dll.rtlsdr_reset_buffer.argtypes = [ctypes.c_void_p]
            
            self.dll.rtlsdr_read_sync.restype = ctypes.c_int
            self.dll.rtlsdr_read_sync.argtypes = [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.POINTER(ctypes.c_int)
            ]
        except Exception as e:
            self.status = DeviceStatus.DISCONNECTED
            self.status_detail = f"Failed to initialize DLL: {e}"

    def connect(self) -> bool:
        """Attempt to claim and initialize the RTL-SDR hardware."""
        if not self.dll:
            self._init_dll()
            if not self.dll:
                return False

        cnt = self.dll.rtlsdr_get_device_count()
        if cnt == 0:
            self.status = DeviceStatus.DISCONNECTED
            self.status_detail = "No RTL-SDR USB dongle detected."
            return False

        try:
            raw_name = self.dll.rtlsdr_get_device_name(0)
            if raw_name:
                self.device_name = raw_name.decode("utf-8", errors="replace")
        except Exception:
            pass

        dev = ctypes.c_void_p()
        ret = self.dll.rtlsdr_open(ctypes.byref(dev), 0)
        if ret == 0:
            self.dev = dev
            self.is_open = True
            self.status = DeviceStatus.CONNECTED
            self.status_detail = f"Hardware Active: {self.device_name}"
            
            # Configure initial SDR settings
            self.dll.rtlsdr_set_sample_rate(self.dev, int(self.sample_rate_hz))
            self.dll.rtlsdr_set_center_freq(self.dev, int(self.center_freq_hz))
            self.dll.rtlsdr_set_tuner_gain_mode(self.dev, 0)  # Automatic Gain Control
            self.dll.rtlsdr_reset_buffer(self.dev)
            return True
        else:
            # Check for blocking process like SatDump or SDR++
            blocking = detect_blocking_processes()
            if blocking:
                self.status = DeviceStatus.BUSY
                procs = ", ".join(blocking)
                self.status_detail = f"Dongle detected but held by: {procs}. Close them to release USB."
            elif ret == -12:
                self.status = DeviceStatus.DISCONNECTED
                self.status_detail = f"WinUSB driver not installed on Interface 0 (code {ret}). Use Zadig to install WinUSB driver."
            elif ret == -1:
                self.status = DeviceStatus.DISCONNECTED
                self.status_detail = f"USB I/O error ({ret}). Unplug and replug the dongle into a direct motherboard USB port."
            elif ret == -3:
                self.status = DeviceStatus.DISCONNECTED
                self.status_detail = f"USB Access Denied ({ret}). Ensure WinUSB driver is installed or run as Administrator."
            else:
                self.status = DeviceStatus.BUSY
                self.status_detail = f"Dongle detected but USB error ({ret}). Running in Simulation fallback."
            return False

    def set_frequency(self, freq_hz: int):
        """Tune center frequency in Hertz."""
        self.center_freq_hz = freq_hz
        if self.is_open and self.dev:
            try:
                self.dll.rtlsdr_set_center_freq(self.dev, int(freq_hz))
                self.dll.rtlsdr_reset_buffer(self.dev)
            except Exception:
                pass

    def set_gain(self, gain_db: float, auto: bool = False):
        """Set gain in dB or enable automatic gain."""
        if self.is_open and self.dev:
            try:
                if auto or gain_db <= 0:
                    self.dll.rtlsdr_set_tuner_gain_mode(self.dev, 0)  # AGC
                else:
                    self.dll.rtlsdr_set_tuner_gain_mode(self.dev, 1)  # Manual
                    self.dll.rtlsdr_set_tuner_gain(self.dev, int(gain_db * 10))
            except Exception:
                pass

    def read_samples(self, num_samples: int = 131072) -> Tuple[np.ndarray, bool]:
        """
        Read complex I/Q samples.
        Returns:
            iq: numpy complex64 array normalized to [-1.0, 1.0]
            is_real: True if acquired from actual hardware, False if synthesized
        """
        if self.is_open and self.dev:
            try:
                buf_len = num_samples * 2
                buf = (ctypes.c_uint8 * buf_len)()
                n_read = ctypes.c_int(0)
                ret = self.dll.rtlsdr_read_sync(self.dev, buf, buf_len, ctypes.byref(n_read))
                if ret == 0 and n_read.value >= buf_len:
                    raw = np.ctypeslib.as_array(buf)[:buf_len].astype(np.float32)
                    raw = (raw - 127.5) / 128.0
                    iq = raw[0::2] + 1j * raw[1::2]
                    return iq.astype(np.complex64), True
            except Exception:
                pass

        # Fallback RF Synthesizer
        iq = self._synthesize_rf(num_samples)
        return iq, False

    def _synthesize_rf(self, num_samples: int) -> np.ndarray:
        """Synthesize realistic DVB-T / COFDM RF passband for the tuned frequency."""
        t = np.arange(num_samples) / self.sample_rate_hz
        # Baseline thermal noise floor (~-75 dBFS)
        noise = (np.random.normal(0, 0.006, num_samples) + 1j * np.random.normal(0, 0.006, num_samples))
        
        freq_mhz = self.center_freq_hz / 1e6
        # Known Greek DTV broadcast frequencies (e.g. 474, 482, 490, 498, 506, 514, 522, 530, 538, 546, 554 MHz)
        is_active_dtv_freq = any(abs(freq_mhz - active_f) < 0.2 for active_f in [474.0, 482.0, 498.0, 522.0, 530.0, 538.0, 546.0, 554.0])
        
        if is_active_dtv_freq:
            # Generate wide flat-topped COFDM signal across the 2.4 MHz receiver window
            # DVB-T uses ~6817 active subcarriers in 8k mode or 1705 in 2k mode
            cofdm_signal = (np.random.normal(0, 0.075, num_samples) + 1j * np.random.normal(0, 0.075, num_samples))
            # Filter edges slightly to emulate analog IF filter rolloff
            edge_factor = np.ones(num_samples)
            edge_len = int(num_samples * 0.08)
            edge_factor[:edge_len] = np.linspace(0.4, 1.0, edge_len)
            edge_factor[-edge_len:] = np.linspace(1.0, 0.4, edge_len)
            # Add small central DC leakage spike typical of Direct Conversion SDRs
            dc_spike = 0.015 * np.exp(1j * np.zeros_like(t))
            return (noise + cofdm_signal * edge_factor + dc_spike).astype(np.complex64)
        else:
            # Idle / Unused TV channel: mostly white noise
            return noise.astype(np.complex64)

    def close(self):
        """Release RTL-SDR hardware."""
        if self.is_open and self.dev:
            try:
                self.dll.rtlsdr_close(self.dev)
            except Exception:
                pass
            self.dev = ctypes.c_void_p(0)
            self.is_open = False
            self.status = DeviceStatus.DISCONNECTED
            self.status_detail = "Device closed."
