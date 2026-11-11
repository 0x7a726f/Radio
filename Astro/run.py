#!/usr/bin/env python3
"""
Astro Launcher Script
"""
import os
import sys

# Ensure standard encoding safety on Windows
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# Ensure local Astro folder is in path
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from astro_analyzer import main

if __name__ == "__main__":
    main()
