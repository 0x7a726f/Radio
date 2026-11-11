"""
SDR Streaming Interface using rtlsdr.dll via ctypes with simulation fallback.
Acquires raw I/Q samples from hardware RTL-SDR dongles (e.g. RTL-SDR Blog V4/V3).
"""

import ctypes
import os
import sys
import time
import threading
from typing import Optional, List, Tuple, Callable
import numpy as np
import psutil


def find_dll() -> Optional[str]:
    """Find rtlsdr.dll portably in Recorder directory, parent, or system paths."""
    local_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(local_dir, "rtlsdr.dll"),
        os.path.join(local_dir, "..", "rtlsdr.dll"),
        r"C:\Code\SatDump\bin\rtlsdr.dll",
        "rtlsdr.dll"
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    return None


def detect_blocking_processes() -> List[str]:
    """Check if any known SDR process is currently running and holding the USB device."""
    blocking = []
    known_sdr = ['satdump', 'sdrpp', 'sdrangel', 'tv_app', 'rtl_tcp', 'dump1090', 'welle']
    for p in psutil.process_iter(['name']):
        try:
            name = p.info['name']
            if name and any(s in name.lower() for s in known_sdr):
                blocking.append(name)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return list(set(blocking))


class SdrStream:
    """Manages RTL-SDR hardware lifecycle, configuration, and sample streaming."""

    def __init__(self, sample_rate_hz: int = 2400000, center_freq_hz: int = 1420500000):
        self.dll_path = find_dll()
        self.dll: Optional[ctypes.CDLL] = None
        self.dev = ctypes.c_void_p(0)
        self.is_connected = False
        self.is_running = False
        self.is_simulated = False
        self.device_name = "None"
        self.status_detail = ""
        self.sample_rate_hz = sample_rate_hz
        self.center_freq_hz = center_freq_hz
        self.gain_db = 25.4
        self.agc = False
        self.bias_tee = False
        self.ppm_correction = 0
        
        self.worker_thread: Optional[threading.Thread] = None
        self.stop_event = threading.Event()
        self.callbacks: List[Callable[[np.ndarray], None]] = []

        self._init_dll()

    def _init_dll(self):
        if not self.dll_path:
            self.status_detail = "rtlsdr.dll not found."
            return
        try:
            self.dll = ctypes.CDLL(self.dll_path)
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

            self.dll.rtlsdr_get_tuner_gains.restype = ctypes.c_int
            self.dll.rtlsdr_get_tuner_gains.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]

            self.dll.rtlsdr_set_freq_correction.restype = ctypes.c_int
            self.dll.rtlsdr_set_freq_correction.argtypes = [ctypes.c_void_p, ctypes.c_int]

            self.dll.rtlsdr_reset_buffer.restype = ctypes.c_int
            self.dll.rtlsdr_reset_buffer.argtypes = [ctypes.c_void_p]

            self.dll.rtlsdr_read_sync.restype = ctypes.c_int
            self.dll.rtlsdr_read_sync.argtypes = [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.POINTER(ctypes.c_int),
            ]

            if hasattr(self.dll, "rtlsdr_set_bias_tee"):
                self.dll.rtlsdr_set_bias_tee.restype = ctypes.c_int
                self.dll.rtlsdr_set_bias_tee.argtypes = [ctypes.c_void_p, ctypes.c_int]

            if hasattr(self.dll, "rtlsdr_get_device_usb_strings"):
                self.dll.rtlsdr_get_device_usb_strings.restype = ctypes.c_int
                self.dll.rtlsdr_get_device_usb_strings.argtypes = [
                    ctypes.c_uint32,
                    ctypes.c_char_p,
                    ctypes.c_char_p,
                    ctypes.c_char_p,
                ]
        except Exception as e:
            self.status_detail = f"DLL error: {e}"

    def scan_devices(self) -> List[str]:
        """Scan available RTL-SDR hardware devices."""
        if not self.dll:
            return ["Simulation Device (Virtual SDR)"]
        try:
            count = self.dll.rtlsdr_get_device_count()
            if count == 0:
                return ["Simulation Device (Virtual SDR)"]
            
            devices = []
            for i in range(count):
                mfg = ctypes.create_string_buffer(256)
                prod = ctypes.create_string_buffer(256)
                ser = ctypes.create_string_buffer(256)
                ret = -1
                if hasattr(self.dll, "rtlsdr_get_device_usb_strings"):
                    ret = self.dll.rtlsdr_get_device_usb_strings(i, mfg, prod, ser)
                if ret == 0 and (prod.value or mfg.value):
                    mfg_s = mfg.value.decode(errors='replace').strip()
                    prod_s = prod.value.decode(errors='replace').strip()
                    ser_s = ser.value.decode(errors='replace').strip()
                    name_str = f"{mfg_s} {prod_s} #{ser_s}".strip()
                else:
                    raw_name = self.dll.rtlsdr_get_device_name(i)
                    name_str = raw_name.decode(errors="replace") if raw_name else f"Generic RTL2832U #{i}"
                devices.append(name_str)
            return devices
        except Exception:
            return ["Simulation Device (Virtual SDR)"]

    def open(self, device_index: int = 0) -> bool:
        """Open physical SDR device or fall back to simulation mode."""
        self.close()

        # Check if USB is occupied by another SDR app (e.g. SatDump)
        blocking = detect_blocking_processes()
        if blocking:
            self.is_connected = True
            self.is_simulated = True
            procs = ", ".join(blocking)
            self.status_detail = f"Device held by {procs}. Running in Virtual SDR mode."
            self.device_name = f"Virtual SDR ({procs} active)"
            return True

        if self.dll:
            try:
                count = self.dll.rtlsdr_get_device_count()
                if count > 0 and device_index < count:
                    dev = ctypes.c_void_p(0)
                    ret = self.dll.rtlsdr_open(ctypes.byref(dev), device_index)
                    if ret == 0 and dev.value:
                        self.dev = dev
                        self.is_connected = True
                        self.is_simulated = False
                        devices = self.scan_devices()
                        self.device_name = devices[device_index] if device_index < len(devices) else "RTL-SDR"
                        self.status_detail = f"Connected to {self.device_name}"
                        
                        # Apply initial hardware settings
                        self.set_sample_rate(self.sample_rate_hz)
                        self.set_frequency(self.center_freq_hz)
                        self.set_gain(self.gain_db, self.agc)
                        self.set_bias_tee(self.bias_tee)
                        self.set_ppm(self.ppm_correction)
                        return True
                    else:
                        self.status_detail = f"Could not open device (code {ret})."
            except Exception as e:
                self.status_detail = f"Hardware open error: {e}"

        # Fallback to simulation mode
        self.is_connected = True
        self.is_simulated = True
        self.device_name = "Virtual SDR (Simulation Mode)"
        if not self.status_detail:
            self.status_detail = "Virtual SDR active (Hydrogen Line simulation)."
        return True

    def close(self):
        """Stop streaming and release hardware device."""
        self.stop()
        if self.dev and self.dll:
            try:
                if hasattr(self.dll, "rtlsdr_set_bias_tee") and self.bias_tee:
                    self.dll.rtlsdr_set_bias_tee(self.dev, 0)
                self.dll.rtlsdr_close(self.dev)
            except Exception:
                pass
        self.dev = ctypes.c_void_p(0)
        self.is_connected = False
        self.is_simulated = False

    def get_valid_gains(self) -> List[float]:
        """Return list of valid tuner gains supported by hardware in dB."""
        if self.dev and self.dll:
            try:
                gains_buf = (ctypes.c_int * 64)()
                n = self.dll.rtlsdr_get_tuner_gains(self.dev, gains_buf)
                if n > 0:
                    return [gains_buf[i] / 10.0 for i in range(n)]
            except Exception:
                pass
        return [0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6,
                19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8, 36.4, 37.2,
                38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6]

    def set_sample_rate(self, rate_hz: int):
        self.sample_rate_hz = rate_hz
        if self.dev and self.dll and not self.is_simulated:
            try:
                self.dll.rtlsdr_set_sample_rate(self.dev, int(rate_hz))
                self.dll.rtlsdr_reset_buffer(self.dev)
            except Exception:
                pass

    def set_frequency(self, freq_hz: int):
        self.center_freq_hz = freq_hz
        if self.dev and self.dll and not self.is_simulated:
            try:
                self.dll.rtlsdr_set_center_freq(self.dev, int(freq_hz))
                self.dll.rtlsdr_reset_buffer(self.dev)
            except Exception:
                pass

    def set_gain(self, gain_db: float, agc: bool = False):
        self.gain_db = gain_db
        self.agc = agc
        if self.dev and self.dll and not self.is_simulated:
            try:
                if agc:
                    self.dll.rtlsdr_set_tuner_gain_mode(self.dev, 0)
                else:
                    self.dll.rtlsdr_set_tuner_gain_mode(self.dev, 1)
                    self.dll.rtlsdr_set_tuner_gain(self.dev, int(gain_db * 10))
            except Exception:
                pass

    def set_bias_tee(self, enabled: bool):
        self.bias_tee = enabled
        if self.dev and self.dll and hasattr(self.dll, "rtlsdr_set_bias_tee") and not self.is_simulated:
            try:
                self.dll.rtlsdr_set_bias_tee(self.dev, 1 if enabled else 0)
            except Exception:
                pass

    def set_ppm(self, ppm: int):
        self.ppm_correction = ppm
        if self.dev and self.dll and not self.is_simulated:
            try:
                self.dll.rtlsdr_set_freq_correction(self.dev, int(ppm))
            except Exception:
                pass

    def add_callback(self, cb: Callable[[np.ndarray], None]):
        if cb not in self.callbacks:
            self.callbacks.append(cb)

    def remove_callback(self, cb: Callable[[np.ndarray], None]):
        if cb in self.callbacks:
            self.callbacks.remove(cb)

    def start(self):
        """Start background acquisition thread."""
        if self.is_running:
            return
        if not self.is_connected:
            self.open()

        self.is_running = True
        self.stop_event.clear()
        self.worker_thread = threading.Thread(target=self._stream_loop, daemon=True)
        self.worker_thread.start()

    def stop(self):
        """Stop streaming thread."""
        self.is_running = False
        self.stop_event.set()
        if self.worker_thread and self.worker_thread.is_alive():
            self.worker_thread.join(timeout=1.0)
        self.worker_thread = None

    def _stream_loop(self):
        """Continuous streaming loop."""
        buf_len = 131072 * 2  # 262144 bytes
        buf = (ctypes.c_uint8 * buf_len)()
        n_read = ctypes.c_int(0)
        sim_sample_idx = 0

        while not self.stop_event.is_set():
            if not self.is_simulated and self.dev and self.dll:
                ret = self.dll.rtlsdr_read_sync(self.dev, buf, buf_len, ctypes.byref(n_read))
                if ret == 0 and n_read.value > 0:
                    raw_u8 = np.ctypeslib.as_array(buf)[: n_read.value].copy()
                    for cb in list(self.callbacks):
                        try:
                            cb(raw_u8)
                        except Exception:
                            pass
                else:
                    time.sleep(0.005)
            else:
                # Simulation mode: generate realistic 21cm Hydrogen Line RF
                chunk_samples = buf_len // 2
                t = (np.arange(chunk_samples) + sim_sample_idx) / self.sample_rate_hz
                sim_sample_idx += chunk_samples

                # 1. Thermal background noise floor
                noise = np.random.normal(0, 16.0, chunk_samples) + 1j * np.random.normal(0, 16.0, chunk_samples)

                # 2. Coaxial cable standing wave ripple (~710 kHz period)
                ripple_freq = 710000.0
                ripple = 4.0 * np.sin(2 * np.pi * ripple_freq * t)

                # 3. Celestial Neutral Hydrogen Line (1420.40575 MHz)
                hi_offset_hz = 1420405752.0 - self.center_freq_hz
                if abs(hi_offset_hz) < (self.sample_rate_hz * 0.45):
                    # Filtered emission profile with galactic Doppler shift (-15 km/s = +71 kHz)
                    hi_sig = np.random.normal(0, 14.0, chunk_samples) + 1j * np.random.normal(0, 14.0, chunk_samples)
                    hi_carrier = np.exp(1j * 2 * np.pi * hi_offset_hz * t)
                    hi_emission = hi_sig * hi_carrier
                else:
                    hi_emission = 0

                # 4. Central LO DC offset spike
                dc_spike = 12.0 * np.exp(1j * np.zeros_like(t))

                total_iq = noise + ripple + hi_emission + dc_spike
                i_vals = np.clip(np.real(total_iq) + 128.0, 0, 255).astype(np.uint8)
                q_vals = np.clip(np.imag(total_iq) + 128.0, 0, 255).astype(np.uint8)

                raw_u8 = np.empty(buf_len, dtype=np.uint8)
                raw_u8[0::2] = i_vals
                raw_u8[1::2] = q_vals

                for cb in list(self.callbacks):
                    try:
                        cb(raw_u8)
                    except Exception:
                        pass

                # Pace simulation at real time
                time.sleep(chunk_samples / self.sample_rate_hz)
