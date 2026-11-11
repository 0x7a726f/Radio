#!/usr/bin/env python3
"""
Greek TV & Radio SDR Live Tuner Launcher
Launches the native desktop application powered by high-performance C++ demodulation (tv_demod.dll).
"""

import os
import sys
import subprocess
import argparse

# Ensure standard encoding safety on Windows
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

current_dir = os.path.dirname(os.path.abspath(__file__))

def main():
    parser = argparse.ArgumentParser(description="Greek TV & Radio SDR Live Tuner Launcher")
    parser.add_argument("--native", action="store_true", help="Launch native C++ executable (tv_app.exe)")
    parser.add_argument("--pyapp", action="store_true", help="Launch native Python Tkinter desktop app (tv_desktop_app.py)")
    parser.add_argument("--web", action="store_true", help="Launch legacy Streamlit WebGUI")
    args = parser.parse_args()

    print("=" * 70)
    print("   GREEK TELEVISION & RADIO SDR LIVE TUNER")
    print("   High-Performance C++ Demodulator Engine & Native Desktop App")
    print("=" * 70)
    print("Demod Engine: SIMD AVX2 Optimized (tv_demod.dll)\n")

    cpp_app_path = os.path.join(current_dir, "tv_app.exe")
    py_desktop_path = os.path.join(current_dir, "tv_desktop_app.py")
    web_app_path = os.path.join(current_dir, "app.py")

    if args.web:
        print("[*] Launching Streamlit WebGUI (legacy mode)...")
        cmd = [sys.executable, "-m", "streamlit", "run", web_app_path, "--server.headless=false"]
        subprocess.run(cmd)
        return

    if args.pyapp:
        print("[*] Launching Python Desktop App (tv_desktop_app.py)...")
        subprocess.run([sys.executable, py_desktop_path])
        return

    # Default: Prefer native C++ application if compiled, otherwise Python desktop app
    if os.path.exists(cpp_app_path) and not args.pyapp:
        print(f"[+] Found native C++ Desktop Application: {cpp_app_path}")
        print("[*] Launching high-speed C++ Native Desktop App...")
        subprocess.run([cpp_app_path])
    elif os.path.exists(py_desktop_path):
        print(f"[+] Launching Native Python Desktop App: {py_desktop_path}...")
        subprocess.run([sys.executable, py_desktop_path])
    else:
        print("[*] Falling back to WebGUI...")
        subprocess.run([sys.executable, "-m", "streamlit", "run", web_app_path])

if __name__ == "__main__":
    main()
