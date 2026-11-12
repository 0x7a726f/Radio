import os
import sys
import subprocess
import json
import time

def run_cmd(cmd):
    t0 = time.perf_counter()
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    t1 = time.perf_counter()
    return res, (t1 - t0)

def main():
    py_script = r"c:\Code\TVRadio\Analyzer\astro_analyzer.py"
    cpp_exe = r"c:\Code\TVRadio\cpp_analyzer\bin\astro_analyzer_cli.exe"
    recording = r"c:\Code\TVRadio\cpp_recorder\recordings\e2e_test_hi.cs16"
    
    py_out_dir = r"c:\Code\TVRadio\cpp_analyzer\output_py_val"
    cpp_out_dir = r"c:\Code\TVRadio\cpp_analyzer\output_cpp_val"
    
    os.makedirs(py_out_dir, exist_ok=True)
    os.makedirs(cpp_out_dir, exist_ok=True)
    
    print("=" * 70)
    print(" RUNNING SCIENTIFIC CROSS-VALIDATION PIPELINE")
    print("=" * 70)
    print(f"Target Recording: {recording}")
    print(f"Filesize: {os.path.getsize(recording) / (1024*1024):.2f} MB\n")
    
    print("[1/2] Executing Python Reference Pipeline...")
    py_cmd = f"python {py_script} --file {recording} --baseline als --output-dir {py_out_dir}"
    py_res, py_wall_s = run_cmd(py_cmd)
    if py_res.returncode != 0:
        print(f"Python failed: {py_res.stderr}")
        return 1
    print(f"  Python execution wall-time: {py_wall_s:.3f} s")
    
    print("\n[2/2] Executing C++ High-Speed DSP Pipeline...")
    cpp_cmd = f"{cpp_exe} {recording} --baseline als -o {cpp_out_dir}"
    cpp_res, cpp_wall_s = run_cmd(cpp_cmd)
    if cpp_res.returncode != 0:
        print(f"C++ failed: {cpp_res.stderr}")
        return 1
    print(f"  C++ execution wall-time:    {cpp_wall_s:.3f} s")
    
    # Load JSON reports
    py_json_path = os.path.join(py_out_dir, "analysis_report.json")
    cpp_json_path = os.path.join(cpp_out_dir, "analysis_report.json")
    
    with open(py_json_path, "r") as f:
        py_data = json.load(f)
    with open(cpp_json_path, "r") as f:
        cpp_data = json.load(f)
        
    py_det = py_data["hydrogen_line_detection"]
    cpp_det = cpp_data["hydrogen_line_detection"]
    
    py_meta = py_data["metadata"]
    cpp_meta = cpp_data["metadata"]
    
    print("\n" + "=" * 80)
    print(f"{'ASTRONOMICAL METRIC':<36} | {'PYTHON (REF)':<15} | {'C++ PIPELINE':<15} | {'DIFF / STATUS':<12}")
    print("-" * 80)
    
    metrics = [
        ("Observed Peak Frequency (MHz)", "observed_peak_frequency_mhz", 1e-4),
        ("Doppler Recession Vel (km/s)", "doppler_recession_velocity_kms", 0.05),
        ("Peak Antenna Temp TA (K)", "peak_antenna_temperature_k", 0.05),
        ("Peak Power (dB)", "peak_power_above_baseline_db", 0.01),
        ("Noise Floor RMS (K)", "noise_floor_rms_k", 0.05),
        ("Signal-to-Noise Ratio (SNR)", "signal_to_noise_ratio_snr", 0.05),
        ("Line FWHM Width (km/s)", "line_fwhm_kms", 0.1),
        ("Integrated Flux W (K*km/s)", "integrated_flux_k_kms", 0.05),
        ("HI Column Density (atoms/cm^2)", "neutral_hydrogen_column_density_cm2", 1e18),
        ("DC Spike Separation (bins)", "bins_from_dc_to_hi", 0),
    ]
    
    all_matched = True
    for label, key, tol in metrics:
        py_v = py_det[key]
        cpp_v = cpp_det[key]
        diff = abs(py_v - cpp_v)
        
        if tol == 0:
            ok = (py_v == cpp_v)
            diff_str = "EXACT (0)" if ok else f"DIFF {diff}"
        else:
            ok = (diff <= tol)
            diff_str = f"{diff:.4e}" if ok else f"FAIL ({diff:.4e})"
            
        status = "[MATCH]" if ok else "[MISMATCH]"
        if not ok:
            all_matched = False
            
        if "density" in key:
            py_s = f"{py_v:.3e}"
            cpp_s = f"{cpp_v:.3e}"
        elif isinstance(py_v, float):
            py_s = f"{py_v:.4f}"
            cpp_s = f"{cpp_v:.4f}"
        else:
            py_s = str(py_v)
            cpp_s = str(cpp_v)
            
        print(f"{label:<36} | {py_s:<15} | {cpp_s:<15} | {status} {diff_str}")
        
    print("=" * 80)
    
    # Throughput and speedup
    py_tp = py_meta["throughput_mb_s"]
    cpp_tp = cpp_meta["throughput_mb_s"]
    speedup = cpp_tp / py_tp if py_tp > 0 else 0
    
    print("\n" + "=" * 80)
    print(" PERFORMANCE BENCHMARKING & STREAMING THROUGHPUT")
    print("=" * 80)
    print(f"  Python Streaming DSP Throughput: {py_tp:7.1f} MB/s ({(py_tp*1024*1024/4)/1e6:5.1f} MSPS)")
    print(f"  C++ Multi-threaded KissFFT TP:  {cpp_tp:7.1f} MB/s ({(cpp_tp*1024*1024/4)/1e6:5.1f} MSPS)")
    print(f"  Throughput Speedup Factor:      {speedup:7.2f}x faster in C++")
    print(f"  End-to-End Processing Time:     {cpp_meta['processing_time_seconds']:.3f} s (C++) vs {py_meta['processing_time_seconds']:.3f} s (Python)")
    print("=" * 80)
    
    return 0 if all_matched else 1

if __name__ == "__main__":
    sys.exit(main())
