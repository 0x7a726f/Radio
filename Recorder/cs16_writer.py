"""
High-Speed CS16 Binary Stream Writer
Converts incoming uint8 I/Q data to signed 16-bit integer (CS16) format
and writes to disk via a dedicated buffered I/O thread.
Accompanies every recording with a comprehensive parameters .txt file and structured .json.
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

HI_REST_FREQ_HZ = 1420405751.786  # 1420.405752 MHz


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
        """Stop recording and write accompanied parameters .txt file and sidecar .json."""
        if not self.is_recording:
            return {}

        self.is_recording = False
        self.stop_event.set()

        if self.writer_thread and self.writer_thread.is_alive():
            self.writer_thread.join(timeout=2.0)
        self.writer_thread = None

        duration_s = time.time() - self.start_time if self.start_time else 0.0

        # Offset calculation
        offset_hz = self.center_freq_hz - HI_REST_FREQ_HZ
        offset_khz = offset_hz / 1e3
        offset_mhz = offset_hz / 1e6

        metadata = {
            "format": "cs16",
            "endianness": "little",
            "bytes_per_sample": 4,
            "sample_rate_hz": self.sample_rate_hz,
            "center_freq_hz": self.center_freq_hz,
            "hi_rest_freq_hz": HI_REST_FREQ_HZ,
            "lo_offset_hz": offset_hz,
            "lo_offset_khz": round(offset_khz, 3),
            "timestamp": datetime.fromtimestamp(self.start_time).isoformat() if self.start_time else "",
            "duration_seconds": round(duration_s, 2),
            "total_samples": self.samples_written,
            "file_size_bytes": self.bytes_written,
            "device": self.device_name,
            "gain_db": self.gain_db,
            "bias_tee": self.bias_tee,
            "file_path": os.path.abspath(self.current_filepath) if self.current_filepath else "",
        }

        if self.current_filepath and os.path.exists(self.current_filepath):
            base_path = os.path.splitext(self.current_filepath)[0]
            
            # 1. Write Accompanied Parameters .txt File
            txt_path = base_path + ".txt"
            self._write_accompanied_txt(txt_path, metadata)

            # 2. Write Sidecar .json
            json_path = base_path + ".json"
            try:
                with open(json_path, "w", encoding="utf-8") as f:
                    json.dump(metadata, f, indent=2)
            except Exception as e:
                print(f"Error saving sidecar JSON: {e}")

        return metadata

    def _write_accompanied_txt(self, txt_path: str, meta: Dict[str, Any]):
        """Write human-readable parameters text file with exact parameters for Analyzer."""
        fc_hz = meta["center_freq_hz"]
        fc_mhz = fc_hz / 1e6
        fs_hz = meta["sample_rate_hz"]
        fs_msps = fs_hz / 1e6
        offset_hz = meta["lo_offset_hz"]
        offset_khz = meta["lo_offset_khz"]
        dur_s = meta["duration_seconds"]
        dur_min = dur_s / 60.0
        size_mb = meta["file_size_bytes"] / (1024 * 1024)
        size_gb = meta["file_size_bytes"] / (1024**3)
        cs16_filename = os.path.basename(meta["file_path"])

        passband_min_mhz = (fc_hz - fs_hz / 2.0) / 1e6
        passband_max_mhz = (fc_hz + fs_hz / 2.0) / 1e6

        if abs(offset_khz) < 1.0:
            offset_desc = "DIRECT CENTERED (LO placed exactly on HI line; residual DC spike may affect line peak)"
        elif offset_hz > 0:
            offset_desc = f"+{offset_khz:.2f} kHz ABOVE HI line (EXCELLENT: LO DC spike is safe and clear of the HI line)"
        else:
            offset_desc = f"{offset_khz:.2f} kHz BELOW HI line (EXCELLENT: LO DC spike is safe and clear of the HI line)"

        content = f"""================================================================================
  HYDROGEN LINE (21cm) RADIO ASTRONOMY OBSERVATION PARAMETERS
================================================================================
Recording File:             {cs16_filename}
Full Path:                  {meta["file_path"]}
Capture Timestamp:          {meta["timestamp"]}
Receiver Hardware:          {meta["device"]}

1. OBSERVATIONAL FREQUENCIES & LO OFFSET
--------------------------------------------------------------------------------
Celestial Target Line:      Neutral Atomic Hydrogen (H I, 21 cm Spin-Flip)
HI Rest Frequency:          1420.405752 MHz (1420405751.786 Hz)
Tuned SDR Center LO:        {fc_mhz:.6f} MHz ({fc_hz:,.0f} Hz)
LO Frequency Offset:        {offset_khz:+.3f} kHz ({offset_hz:+,.0f} Hz)
Offset Strategy:            {offset_desc}
DC Spike Excision Window:   Excise +/- 5.0 kHz around {fc_mhz:.6f} MHz

2. SDR HARDWARE & RF CONFIGURATION
--------------------------------------------------------------------------------
Sampling Rate (Fs):         {fs_msps:.3f} MSPS ({fs_hz:,.0f} Hz)
Receiver Nyquist Bandwidth: {fs_msps:.3f} MHz (Coverage: {passband_min_mhz:.3f} to {passband_max_mhz:.3f} MHz)
Tuner LNA Gain:             {meta["gain_db"]:.1f} dB
Bias-Tee (4.5V LNA Power):  {'Enabled (Powers external Sawbird+ H1 LNA)' if meta["bias_tee"] else 'Disabled'}
Data Format:                CS16 (Signed 16-bit Complex Integer, Little-Endian)
Bytes Per Complex Sample:   4 bytes (2 bytes I + 2 bytes Q)
Sample Interleaving:        [I0, Q0, I1, Q1, I2, Q2, ...]

3. ACQUISITION RUNTIME METRICS
--------------------------------------------------------------------------------
Observation Duration:       {dur_s:.2f} seconds ({dur_min:.2f} minutes)
Total Complex Samples:      {meta["total_samples"]:,} samples
Total File Size:            {meta["file_size_bytes"]:,} bytes ({size_mb:.2f} MB / {size_gb:.3f} GB)
Average Data Rate:          {(fs_hz * 4) / (1024 * 1024):.2f} MB/s

4. OFFLINE ANALYSIS INSTRUCTIONS
--------------------------------------------------------------------------------
The accompanying Analyzer can automatically read this file, or you can run
directly with the observed parameters:

  [Primary Analysis Pipeline]
  python Analyzer/astro_analyzer.py "{cs16_filename}" --fc {fc_mhz:.4f}MHz --fs {fs_msps:.3f}MSPS

  [Quick 3-Panel Matplotlib Viewer]
  python Analyzer/hydro_quick.py "{cs16_filename}" --fc {fc_mhz:.4f}M --fs {fs_msps:.3f}M

  [Interactive Wizard (Auto-Detects Parameters)]
  python Analyzer/run.py
================================================================================
"""
        try:
            with open(txt_path, "w", encoding="utf-8") as f:
                f.write(content)
        except Exception as e:
            print(f"Error saving accompanied parameters TXT: {e}")

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

        disk_target = self.output_dir if os.path.exists(self.output_dir) else "."
        total, used, free = shutil.disk_usage(disk_target)
        free_gb = free / (1024**3)

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
