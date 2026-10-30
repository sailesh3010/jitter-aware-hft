#!/usr/bin/env python3
"""
Publication-ready plotting script for Network Jitter Trading Simulator results.
Generates research charts illustrating quantitative findings and systems benchmarks.
"""

import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

# Style setup for institutional research aesthetics
plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
plt.rcParams['font.sans-serif'] = 'Arial', 'DejaVu Sans', 'Helvetica'
plt.rcParams['axes.edgecolor'] = '#cccccc'
plt.rcParams['axes.linewidth'] = 0.8

def plot_jitter_sweep(results_dir: Path, figures_dir: Path):
    csv_path = results_dir / "jitter_sweep_results.csv"
    if not csv_path.exists():
        print(f"[-] {csv_path} not found. Skipping sweep plot.")
        return

    df = pd.read_csv(csv_path)

    fig, ax1 = plt.subplots(figsize=(9, 5), dpi=300)

    color_pnl = '#1f77b4'
    ax1.set_xlabel('Network Jitter $\\sigma$ (μs)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Total PnL ($)', color=color_pnl, fontsize=12, fontweight='bold')
    line1 = ax1.plot(df['jitter_us'], df['total_pnl'], color=color_pnl, marker='o', linewidth=2.5, label='Total PnL ($)')
    ax1.tick_params(axis='y', labelcolor=color_pnl)
    ax1.grid(True, linestyle='--', alpha=0.5)

    ax2 = ax1.twinx()
    color_sharpe = '#d62728'
    ax2.set_ylabel('Annualized Sharpe Ratio', color=color_sharpe, fontsize=12, fontweight='bold')
    line2 = ax2.plot(df['jitter_us'], df['sharpe_ratio'], color=color_sharpe, marker='s', linestyle='--', linewidth=2.5, label='Sharpe Ratio')
    ax2.tick_params(axis='y', labelcolor=color_sharpe)
    ax2.grid(False)

    lines = line1 + line2
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc='upper right', frameon=True, facecolor='white', framealpha=0.9)

    plt.title('Strategy Performance Alpha Decay vs. Network Jitter', fontsize=14, fontweight='bold', pad=15)
    plt.tight_layout()
    out_file = figures_dir / "jitter_vs_pnl_sharpe.png"
    plt.savefig(out_file)
    plt.close()
    print(f"[+] Generated: {out_file}")

def plot_pnl_trajectory(results_dir: Path, figures_dir: Path):
    csv_path = results_dir / "pnl_series.csv"
    if not csv_path.exists():
        print(f"[-] {csv_path} not found. Skipping PnL trajectory plot.")
        return

    df = pd.read_csv(csv_path)
    time_s = (df['timestamp_ns'] - df['timestamp_ns'].iloc[0]) / 1e9

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True, dpi=300,
                                   gridspec_kw={'height_ratios': [2.5, 1]})

    # Top: PnL & Drawdown
    ax1.plot(time_s, df['total_pnl'], color='#008080', linewidth=2, label='Total PnL ($)')
    ax1.fill_between(time_s, df['total_pnl'], df['total_pnl'] - df['drawdown'],
                     color='#ff7f0e', alpha=0.3, label='Drawdown')
    ax1.set_ylabel('PnL ($)', fontsize=11, fontweight='bold')
    ax1.set_title('High-Frequency Strategy PnL Trajectory & Drawdown', fontsize=13, fontweight='bold')
    ax1.legend(loc='upper left', frameon=True, facecolor='white')
    ax1.grid(True, linestyle='--', alpha=0.5)

    # Bottom: Inventory
    ax2.plot(time_s, df['position'], color='#4a69bd', linewidth=1.5, label='Inventory (Lots)')
    ax2.axhline(0, color='gray', linestyle=':', linewidth=1)
    ax2.set_xlabel('Simulation Time (s)', fontsize=11, fontweight='bold')
    ax2.set_ylabel('Position', fontsize=11, fontweight='bold')
    ax2.legend(loc='upper left', frameon=True, facecolor='white')
    ax2.grid(True, linestyle='--', alpha=0.5)

    plt.tight_layout()
    out_file = figures_dir / "pnl_trajectory.png"
    plt.savefig(out_file)
    plt.close()
    print(f"[+] Generated: {out_file}")

def plot_latency_cdf(results_dir: Path, figures_dir: Path):
    csv_path = results_dir / "latency.csv"
    if not csv_path.exists():
        print(f"[-] {csv_path} not found. Skipping latency plot.")
        return

    df = pd.read_csv(csv_path)
    latencies = np.sort(df['latency_us'].values)
    cdf = np.arange(1, len(latencies) + 1) / len(latencies)

    p50 = np.percentile(latencies, 50)
    p90 = np.percentile(latencies, 90)
    p99 = np.percentile(latencies, 99)

    plt.figure(figsize=(8, 5), dpi=300)
    plt.plot(latencies, cdf, color='#2b580c', linewidth=2.5, label='Empirical CDF')

    plt.axvline(p50, color='#639a67', linestyle='--', label=f'p50 = {p50:.1f} μs')
    plt.axvline(p90, color='#d9ad26', linestyle='--', label=f'p90 = {p90:.1f} μs')
    plt.axvline(p99, color='#d9534f', linestyle='--', label=f'p99 = {p99:.1f} μs')

    plt.xlabel('Tick-to-Trade Latency (μs)', fontsize=11, fontweight='bold')
    plt.ylabel('Cumulative Probability', fontsize=11, fontweight='bold')
    plt.title('End-to-End Tick-to-Trade Latency Cumulative Distribution', fontsize=13, fontweight='bold')
    plt.legend(loc='lower right', frameon=True, facecolor='white')
    plt.grid(True, linestyle='--', alpha=0.5)

    plt.tight_layout()
    out_file = figures_dir / "latency_cdf.png"
    plt.savefig(out_file)
    plt.close()
    print(f"[+] Generated: {out_file}")

def plot_markout_curve(results_dir: Path, figures_dir: Path):
    csv_path = results_dir / "jitter_sweep_results.csv"
    if not csv_path.exists():
        return

    df = pd.read_csv(csv_path)
    horizons = ['+100 μs', '+1 ms', '+5 ms', '+10 ms', '+50 ms']
    markout_cols = ['markout_100us', 'markout_1ms', 'markout_5ms', 'markout_10ms', 'markout_50ms']

    plt.figure(figsize=(9, 5), dpi=300)

    # Plot markout curves for min jitter, median jitter, and max jitter
    selected_indices = [0, len(df)//2, len(df)-1]
    colors = ['#2ca02c', '#1f77b4', '#d62728']

    for idx, color in zip(selected_indices, colors):
        row = df.iloc[idx]
        jitter_val = row['jitter_us']
        vals = [row[c] for c in markout_cols]
        plt.plot(horizons, vals, marker='o', linewidth=2, color=color,
                 label=f'Jitter $\\sigma$ = {jitter_val} μs')

    plt.axhline(0, color='black', linestyle=':', alpha=0.7)
    plt.xlabel('Post-Trade Horizon $\\tau$', fontsize=11, fontweight='bold')
    plt.ylabel('Markout PnL (ticks)', fontsize=11, fontweight='bold')
    plt.title('Post-Trade Adverse Selection Markout Curve Across Jitter Regimes', fontsize=13, fontweight='bold')
    plt.legend(loc='lower left', frameon=True, facecolor='white')
    plt.grid(True, linestyle='--', alpha=0.5)

    plt.tight_layout()
    out_file = figures_dir / "adverse_selection_markout.png"
    plt.savefig(out_file)
    plt.close()
    print(f"[+] Generated: {out_file}")

def plot_concurrency_comparison(results_dir: Path, figures_dir: Path):
    csv_path = results_dir / "concurrency_benchmark.csv"
    if not csv_path.exists():
        return

    df = pd.read_csv(csv_path)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5), dpi=300)

    # Throughput
    bars1 = ax1.bar(df['queue_type'], df['mops_per_sec'], color=['#1f77b4', '#aec7e8'], width=0.5)
    ax1.set_ylabel('Throughput (Million ops/sec)', fontsize=11, fontweight='bold')
    ax1.set_title('Queue Ingestion Throughput', fontsize=12, fontweight='bold')
    ax1.grid(True, axis='y', linestyle='--', alpha=0.5)
    for bar in bars1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2, yval + 0.2, f'{yval:.2f} M', ha='center', va='bottom', fontweight='bold')

    # Latency
    bars2 = ax2.bar(df['queue_type'], df['avg_latency_ns'], color=['#2ca02c', '#98df8a'], width=0.5)
    ax2.set_ylabel('Average Latency (ns/op)', fontsize=11, fontweight='bold')
    ax2.set_title('Operation Latency', fontsize=12, fontweight='bold')
    ax2.grid(True, axis='y', linestyle='--', alpha=0.5)
    for bar in bars2:
        yval = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2, yval + 1.0, f'{yval:.1f} ns', ha='center', va='bottom', fontweight='bold')

    plt.suptitle('Lock-Free SPSC Ring Buffer vs. Mutex-Condition Queue', fontsize=13, fontweight='bold', y=1.02)
    plt.tight_layout()
    out_file = figures_dir / "concurrency_comparison.png"
    plt.savefig(out_file)
    plt.close()
    print(f"[+] Generated: {out_file}")

def main():
    repo_root = Path(__file__).resolve().parent.parent
    results_dir = repo_root / "results"
    figures_dir = results_dir / "figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    print("[*] Generating quantitative research charts...")
    plot_jitter_sweep(results_dir, figures_dir)
    plot_pnl_trajectory(results_dir, figures_dir)
    plot_latency_cdf(results_dir, figures_dir)
    plot_markout_curve(results_dir, figures_dir)
    plot_concurrency_comparison(results_dir, figures_dir)
    print(f"[+] Chart generation complete. All plots saved to: {figures_dir}")

if __name__ == "__main__":
    main()
