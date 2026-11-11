"""
High-Speed CS16 Binary Stream Writer
Converts incoming uint8 I/Q data to signed 16-bit integer (CS16) format
and writes to disk via a dedicated buffered I/O thread.
"""

import os
import sys
import time
import json
import shutil
import queue
import threading
from datetime import datetime
from typing import Optional, Dict, Any
import numpy as np


class Cs16Writer:
    """Manages asynchronous buffered recording of raw I/Q samples into SatDump-compatible CS16 files."""

    # Vectorized lookup table: 0..255 uint8 -> -32768..32512 int16
    LUT = ((np.arange(256, dtype=np.int32) - 128) * 256).astype('<i2')

    def __init__(self, output_dir: str = "recordings"):
        self.output_dir = output_dir
        os.makedirs(self.output_dir, exist_ok=True)

        self.is_recording = False
        self.current_filepath: Optional[str] = None
        self.start_time: float = 0.0
        self.bytes_written: int = 0
        self.samples_written: int = 0
        self.sample_rate_hz: int = 2400000
        self.center_freq_hz: int = 1420500000
        self.device_name: str = "RTL-SDR"
        self.gain_db: float = 25.4
        self.bias_tee: bool = False
        self.max_duration_s: Optional[float] = None

        self.write_queue = queue.Queue(maxsize=128)
        self.writer_thread: Optional[threading.Thread] = None
        self.stop_event = threading.Event()

    def start_recording(
        self,
        sample_rate_hz: int,
        center_freq_hz: int,
        device_name: str = "RTL-SDR",
        gain_db: float = 25.4,
        bias_tee: bool = False,
        custom_filepath: Optional[str] = None,
        max_duration_s: Optional[float] = None,
    ) -> str:
        """Start recording raw CS16 samples to disk."""
        if self.is_recording:
            return self.current_filepath

        self.sample_rate_hz = sample_rate_hz
        self.center_freq_hz = center_freq_hz
        self.device_name = device_name
        self.gain_db = gain_db
        self.bias_tee = bias_tee
        self.max_duration_s = max_duration_s

        if custom_filepath:
            self.current_filepath = custom_filepath
        else:
            now_str = datetime.now().strftime("%Y%m%d_%H%M%S")
            freq_str = f"{center_freq_hz / 1e6:.3f}MHz"
            rate_str = f"{sample_rate_hz / 1e6:.2f}MSPS"
            filename = f"hi_capture_{now_str}_{freq_str}_{rate_str}.cs16"
            self.current_filepath = os.path.join(self.output_dir, filename)

        os.makedirs(os.path.dirname(os.path.abspath(self.current_filepath)), exist_ok=True)

        self.bytes_written = 0
        self.samples_written = 0
        self.start_time = time.time()
        self.is_recording = True
        self.stop_event.clear()

        # Clear write queue
        while not self.write_queue.empty():
            try:
                self.write_queue.get_nowait()
            except queue.Empty:
                break

        self.writer_thread = threading.Thread(target=self._writer_loop, daemon=True)
        self.writer_thread.start()

        return self.current_filepath

    def stop_recording(self) -> Dict[str, Any]:
        """Stop recording and write sidecar JSON metadata."""
        if not self.is_recording:
            return {}

        self.is_recording = False
        self.stop_event.set()

        if self.writer_thread and self.writer_thread.is_alive():
            self.writer_thread.join(timeout=2.0)
        self.writer_thread = None

        duration_s = time.time() - self.start_time if self.start_time else 0.0

        metadata = {
            "format": "cs16",
            "endianness": "little",
            "bytes_per_sample": 4,
            "sample_rate_hz": self.sample_rate_hz,
            "center_freq_hz": self.center_freq_hz,
            "timestamp": datetime.fromtimestamp(self.start_time).isoformat() if self.start_time else "",
            "duration_seconds": round(duration_s, 2),
            "total_samples": self.samples_written,
            "file_size_bytes": self.bytes_written,
            "device": self.device_name,
            "gain_db": self.gain_db,
            "bias_tee": self.bias_tee,
            "file_path": os.path.abspath(self.current_filepath) if self.current_filepath else "",
        }

        # Write sidecar JSON
        if self.current_filepath and os.path.exists(self.current_filepath):
            json_path = os.path.splitext(self.current_filepath)[0] + ".json"
            try:
                with open(json_path, "w", encoding="utf-8") as f:
                    json.dump(metadata, f, indent=2)
            except Exception as e:
                print(f"Error saving sidecar metadata: {e}")

        return metadata

    def feed_samples(self, raw_u8: np.ndarray):
        """Enqueue raw uint8 samples from SDR acquisition callback."""
        if not self.is_recording:
            return
        
        # Check auto-stop duration
        if self.max_duration_s and (time.time() - self.start_time) >= self.max_duration_s:
            self.stop_recording()
            return

        try:
            self.write_queue.put_nowait(raw_u8)
        except queue.Full:
            # Drop frame rather than blocking SDR acquisition
            pass

    def _writer_loop(self):
        """Dedicated background thread for high-speed disk writes."""
        try:
            with open(self.current_filepath, "wb", buffering=1048576) as f:
                while not self.stop_event.is_set() or not self.write_queue.empty():
                    try:
                        raw_u8 = self.write_queue.get(timeout=0.1)
                    except queue.Empty:
                        continue

                    # Vectorized conversion: uint8 -> signed int16 (CS16)
                    cs16_data = self.LUT[raw_u8]
                    raw_bytes = cs16_data.tobytes()
                    f.write(raw_bytes)

                    n_bytes = len(raw_bytes)
                    self.bytes_written += n_bytes
                    self.samples_written += n_bytes // 4
                    self.write_queue.task_done()
        except Exception as e:
            print(f"Disk write error: {e}")
            self.is_recording = False

    def get_stats(self) -> Dict[str, Any]:
        """Query real-time recording statistics matching SatDump UI."""
        elapsed_s = time.time() - self.start_time if (self.is_recording and self.start_time) else 0.0
        hrs = int(elapsed_s // 3600)
        mins = int((elapsed_s % 3600) // 60)
        secs = int(elapsed_s % 60)
        elapsed_str = f"{hrs:02d}:{mins:02d}:{secs:02d}"

        # Disk space calculations
        disk_target = self.output_dir if os.path.exists(self.output_dir) else "."
        total, used, free = shutil.disk_usage(disk_target)
        free_gb = free / (1024**3)

        # Time left based on 4 bytes per sample (CS16)
        bytes_per_sec = self.sample_rate_hz * 4
        if bytes_per_sec > 0:
            time_left_s = free / bytes_per_sec
            tl_days = int(time_left_s // 86400)
            tl_hrs = int((time_left_s % 86400) // 3600)
            tl_mins = int((time_left_s % 3600) // 60)
            tl_secs = int(time_left_s % 60)
            if tl_days > 0:
                time_left_str = f"{tl_days:02d}:{tl_hrs:02d}:{tl_mins:02d}:{tl_secs:02d}"
            else:
                time_left_str = f"{tl_hrs:02d}:{tl_mins:02d}:{tl_secs:02d}"
        else:
            time_left_str = "--:--:--"

        # File size display
        size_mb = self.bytes_written / (1024 * 1024)
        if size_mb >= 1024.0:
            size_str = f"{size_mb / 1024.0:.2f} GB"
        else:
            size_str = f"{size_mb:.2f} MB"

        return {
            "status": "RECORDING" if self.is_recording else "IDLE",
            "file": os.path.basename(self.current_filepath) if self.current_filepath else "None",
            "full_path": self.current_filepath or "",
            "size_str": size_str,
            "bytes_written": self.bytes_written,
            "samples_written": self.samples_written,
            "free_space_gb": free_gb,
            "free_space_str": f"{free_gb:.2f} GB",
            "time_left_str": time_left_str,
            "elapsed_str": elapsed_str,
            "elapsed_s": elapsed_s,
        }
