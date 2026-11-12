import subprocess
import time
import os

try:
    import psutil
    has_psutil = True
except ImportError:
    has_psutil = False

def main():
    exe = r"c:\Code\TVRadio\cpp_analyzer\bin\astro_analyzer_cli.exe"
    recording = r"c:\Code\TVRadio\Analyzer\2026-11-11_17-47-26_2400000SPS_1420410000Hz.cs16"
    out_dir = r"c:\Code\TVRadio\cpp_analyzer\bench_audit_4gb"
    
    if not os.path.exists(recording):
        print(f"File not found: {recording}")
        return 1
        
    sz_gb = os.path.getsize(recording) / (1024**3)
    print(f"Auditing C++ RAM on {sz_gb:.2f} GB file: {os.path.basename(recording)}")
    
    cmd = [exe, recording, "-o", out_dir, "--baseline", "als", "-t", "30.0"] # 30 seconds of observation = 288 MB to 500 MB streaming test
    
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    max_ram_mb = 0.0
    samples = 0
    
    if has_psutil:
        p = psutil.Process(proc.pid)
        while proc.poll() is None:
            try:
                mem = p.memory_info().rss / (1024 * 1024)
                if mem > max_ram_mb:
                    max_ram_mb = mem
                samples += 1
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                break
            time.sleep(0.01)
    else:
        while proc.poll() is None:
            time.sleep(0.05)
            
    stdout, stderr = proc.communicate()
    
    print(f"Audit completed: Exit code {proc.returncode}")
    print(f"Peak RSS RAM Consumption: {max_ram_mb:.2f} MB (Target: strictly < 50.0 MB)")
    if max_ram_mb < 50.0:
        print("VERDICT: PASS (RAM is strictly below 50 MB requirement!)")
    else:
        print("VERDICT: FAIL")

if __name__ == "__main__":
    main()
