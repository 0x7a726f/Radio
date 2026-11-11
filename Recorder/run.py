#!/usr/bin/env python3
"""
Launcher for Hydrogen Line SDR Receiver & CS16 Recorder
Supports both interactive SatDump-style Desktop GUI and headless CLI captures.
"""

import os
import sys
import time
import argparse

# Local imports
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from sdr_stream import SdrStream
from cs16_writer import Cs16Writer


def run_headless_capture(
    duration_s: float,
    center_freq_hz: float = 1420500000,
    sample_rate_hz: int = 2400000,
    gain_db: float = 25.4,
    bias_tee: bool = False,
    output_path: str = None,
):
    print("=" * 70)
    print("  HYDROGEN LINE SDR CS16 HEADLESS RECORDER")
    print("=" * 70)
    print(f"  Center Frequency:  {center_freq_hz / 1e6:.3f} MHz")
    print(f"  Sample Rate:       {sample_rate_hz / 1e6:.2f} MSPS")
    print(f"  Duration:          {duration_s:.1f} seconds")
    print(f"  Gain:              {gain_db:.1f} dB")
    print(f"  Bias-Tee (4.5V):   {'Enabled' if bias_tee else 'Disabled'}")
    print("-" * 70)

    sdr = SdrStream(sample_rate_hz=sample_rate_hz, center_freq_hz=center_freq_hz)
    sdr.gain_db = gain_db
    sdr.bias_tee = bias_tee

    writer = Cs16Writer(output_dir="recordings")
    sdr.add_callback(writer.feed_samples)

    if not sdr.open():
        print(f"Warning: {sdr.status_detail}")

    print(f"Using device: {sdr.device_name}")
    filepath = writer.start_recording(
        sample_rate_hz=sample_rate_hz,
        center_freq_hz=center_freq_hz,
        device_name=sdr.device_name,
        gain_db=gain_db,
        bias_tee=bias_tee,
        custom_filepath=output_path,
        max_duration_s=duration_s,
    )
    print(f"Recording to: {filepath}")

    sdr.start()
    t_start = time.time()

    try:
        while time.time() - t_start < duration_s:
            stats = writer.get_stats()
            elapsed = time.time() - t_start
            print(
                f"\rRecording: {elapsed:5.1f}s / {duration_s:5.1f}s | "
                f"Size: {stats['size_str']} | "
                f"Free: {stats['free_space_str']} | "
                f"Samples: {stats['samples_written']:,}",
                end="",
                flush=True,
            )
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\nInterrupted by user.")

    print("\nFinalizing recording...")
    meta = writer.stop_recording()
    sdr.stop()
    sdr.close()

    print("-" * 70)
    print("Recording finished successfully!")
    print(f"CS16 Data File:     {meta.get('file_path')}")
    print(f"Total Samples:      {meta.get('total_samples'):,}")
    print(f"Total Bytes:        {meta.get('file_size_bytes'):,} bytes")
    print("=" * 70)


def parse_freq_or_rate(val_str: str) -> float:
    cleaned = val_str.strip().lower()
    if cleaned.endswith("mhz") or cleaned.endswith("msps") or cleaned.endswith("m"):
        return float(cleaned.rstrip("mhzsp")) * 1e6
    if cleaned.endswith("khz") or cleaned.endswith("ksps") or cleaned.endswith("k"):
        return float(cleaned.rstrip("khzsp")) * 1e3
    v = float(cleaned)
    return v * 1e6 if v < 10000.0 else v


def main():
    parser = argparse.ArgumentParser(description="Hydrogen Line SDR Receiver & CS16 Recorder")
    parser.add_argument("--cli", action="store_true", help="Run in headless CLI mode instead of GUI")
    parser.add_argument("--duration", "-t", type=float, default=60.0, help="Recording duration in seconds (CLI mode)")
    parser.add_argument("--fc", type=str, default="1420.5M", help="Center LO frequency (default: 1420.5M)")
    parser.add_argument("--fs", type=str, default="2.4M", help="Sample rate (default: 2.4M)")
    parser.add_argument("--gain", type=float, default=25.4, help="LNA gain in dB (default: 25.4)")
    parser.add_argument("--bias-tee", action="store_true", help="Enable 4.5V Bias-Tee for LNA")
    parser.add_argument("--out", "-o", type=str, default=None, help="Custom output .cs16 filepath")

    args = parser.parse_args()

    if args.cli:
        fc_hz = parse_freq_or_rate(args.fc)
        fs_hz = int(parse_freq_or_rate(args.fs))
        run_headless_capture(
            duration_s=args.duration,
            center_freq_hz=fc_hz,
            sample_rate_hz=fs_hz,
            gain_db=args.gain,
            bias_tee=args.bias_tee,
            output_path=args.out,
        )
    else:
        from app import SatDumpRecorderApp
        app = SatDumpRecorderApp()
        app.mainloop()


if __name__ == "__main__":
    main()
