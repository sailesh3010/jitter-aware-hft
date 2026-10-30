#!/usr/bin/env python3
"""
Orchestration script for Network Jitter Trading Simulator experiments.
Runs backtests across multiple network profiles and invokes plotting routines.
"""

import os
import subprocess
import sys
from pathlib import Path

def main():
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = repo_root / "build"
    results_dir = repo_root / "results"
    results_dir.mkdir(parents=True, exist_ok=True)

    # Locate executable
    possible_paths = [
        build_dir / "src" / "simulator_main.exe",
        build_dir / "src" / "simulator_main",
        build_dir / "simulator_main.exe",
        build_dir / "simulator_main",
        build_dir / "src" / "Release" / "simulator_main.exe",
    ]

    exe = None
    for p in possible_paths:
        if p.exists():
            exe = p
            break

    if not exe:
        print(f"[!] simulator_main binary not found in build directory. Please compile first.")
        sys.exit(1)

    print(f"[*] Found executable: {exe}")

    # 1. Run baseline backtest
    print("\n--- Running Baseline Backtest (Default Config) ---")
    subprocess.run([str(exe), "--mode", "backtest", "--output-dir", str(results_dir)], check=True)

    # 2. Run Jitter Parameter Sweep
    print("\n--- Running Jitter Parameter Sweep ---")
    subprocess.run([str(exe), "--mode", "sweep", "--output-dir", str(results_dir)], check=True)

    # 3. Run Concurrency Benchmark
    print("\n--- Running Lock-Free SPSC vs Mutex Benchmark ---")
    subprocess.run([str(exe), "--mode", "concurrency", "--output-dir", str(results_dir)], check=True)

    print("\n[+] All simulations finished successfully. Results saved in:", results_dir)

if __name__ == "__main__":
    main()
