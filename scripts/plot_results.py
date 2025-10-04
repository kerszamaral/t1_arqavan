#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = [
#   "pandas",
#   "numpy",
#   "matplotlib",
# ]
# ///

"""
plot_results.py
UV script to parse run results (results/runs.csv and results/*.log produced by run_and_measure.sh +
papito) and create matplotlib PNG plots summarizing the experiments.

Usage:
  uv run scripts/plot_results.py --results-dir ./results

Outputs:
  - results/analysis_table.csv      <- aggregated dataframe
  - results/plots/time_by_mode_bs.png
  - results/plots/ipc_by_mode_bs.png
  - results/plots/branch_misp_by_mode_bs.png
  - results/plots/l3miss_by_mode_bs.png
  - results/plots/energy_by_mode_bs.png   (if RAPL present)
  - results/plots/*_by_n_bs_*.png       <- NEW: Line plots for each metric and block size
"""

from __future__ import annotations
import argparse
import os
import glob
import re
import math
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# -----------------------
# Helpers for parsing logs
# -----------------------
PAPITO_COUNTERS_RE = re.compile(r'^PAPITO_COUNTERS\s*(.*)$')
PAPITO_VALUES_RE = re.compile(r'^PAPITO_VALUES\s*(.*)$')
SUMMARY_RE = re.compile(r'^SUMMARY\t.*seconds=([0-9.eE+-]+).*checksum=([0-9.eE+-]+)')

def parse_papito_block_from_log(logpath: Path):
    """
    Parse a single log file for PAPITO_COUNTERS and PAPITO_VALUES pairs.
    Returns dict {counter_name: value}
    If multiple PAPITO lines appear, the last pair is used.
    """
    counters = None
    values = None
    seconds = None
    checksum = None
    with logpath.open('r', errors='ignore') as f:
        for line in f:
            line = line.rstrip('\n')
            m1 = PAPITO_COUNTERS_RE.match(line)
            if m1:
                # fields separated by tab or whitespace
                parts = re.split(r'\s+', m1.group(1).strip())
                counters = [p for p in parts if p != '']
                continue
            m2 = PAPITO_VALUES_RE.match(line)
            if m2:
                parts = re.split(r'\s+', m2.group(1).strip())
                # convert to ints if possible
                vals = []
                for p in parts:
                    try:
                        vals.append(int(p))
                    except:
                        try:
                            vals.append(float(p))
                        except:
                            vals.append(np.nan)
                values = vals
                continue
            ms = SUMMARY_RE.match(line)
            if ms:
                try:
                    seconds = float(ms.group(1))
                except:
                    seconds = None
                try:
                    checksum = float(ms.group(2))
                except:
                    checksum = None
    if counters is None or values is None:
        return None, None, seconds, checksum
    # pair them up
    d = {}
    # if lengths differ, pair up to min
    n = min(len(counters), len(values))
    for i in range(n):
        d[counters[i]] = values[i]
    return d, counters, seconds, checksum

# -----------------------
# Aggregate runs
# -----------------------
def build_dataframe(results_dir: Path):
    runs_csv = results_dir / "runs.csv"
    if not runs_csv.exists():
        raise FileNotFoundError(f"{runs_csv} not found. Run experiments first.")
    runs = pd.read_csv(runs_csv)
    # ensure columns: timestamp, mode, N, BS, run, elapsed_s, checksum, logfile
    # some old runs may have 'stdout' field; handle both
    if 'logfile' not in runs.columns and 'stdout' in runs.columns:
        runs = runs.rename(columns={'stdout':'logfile'})

    # expand columns types
    runs['N'] = runs['N'].astype(int)
    runs['BS'] = runs['BS'].astype(int)
    # elapsed may be "NA"
    runs['elapsed_s'] = pd.to_numeric(runs['elapsed_s'], errors='coerce')

    # For each run, parse its logfile to get counters
    parsed = []
    all_counter_names = set()
    for idx, row in runs.iterrows():
        logfile = row['logfile']
        if not isinstance(logfile, str) or logfile.strip() == '':
            parsed.append({})
            continue
        logpath = results_dir / Path(logfile).name  # logs are stored in results/
        if not logpath.exists():
            # maybe the csv stores absolute path
            logpath = Path(logfile)
        if not logpath.exists():
            parsed.append({})
            continue
        d, counters_order, seconds, checksum = parse_papito_block_from_log(logpath)
        if d is None:
            parsed.append({})
            continue
        parsed.append(d)
        all_counter_names.update(d.keys())
    # create dataframe of counters
    if parsed:
        counters_df = pd.DataFrame(parsed)
    else:
        counters_df = pd.DataFrame(index=runs.index)
    # counters_df is columns = counter names, rows aligned with runs
    counters_df = counters_df.fillna(np.nan)

    # concat
    merged = pd.concat([runs.reset_index(drop=True), counters_df.reset_index(drop=True)], axis=1)
    return merged

# -----------------------
# Plotting helpers
# -----------------------
def ensure_plot_dir(results_dir: Path):
    plotdir = results_dir / "plots"
    plotdir.mkdir(parents=True, exist_ok=True)
    return plotdir

def grouped_mean_df(df: pd.DataFrame, group_keys, value_col):
    return df.groupby(group_keys)[value_col].agg(['mean','std','count']).reset_index()

def plot_bar(dfagg, xcol, ycol_mean, ycol_err, title, xlabel, ylabel, outpath: Path):
    """
    Single-matplotlib bar chart. Does not set colors explicitly.
    """
    plt.figure(figsize=(10,6))
    x = dfagg[xcol].astype(str)
    y = dfagg[ycol_mean]
    err = dfagg[ycol_err] if ycol_err in dfagg.columns else None
    ax = plt.gca()
    if err is not None:
        ax.bar(x, y, yerr=err, capsize=4)
    else:
        ax.bar(x, y)
    plt.xticks(rotation=45, ha='right')
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.tight_layout()
    plt.savefig(outpath)
    plt.close()

def plot_curves_by_mode(df: pd.DataFrame, xcol: str, ycol: str, title: str, xlabel: str, ylabel: str, outpath: Path):
    """
    Creates a line plot with different curves for each 'mode'.
    X-axis is a specified column, Y-axis is the mean of another column.
    """
    plt.figure(figsize=(12, 7))
    ax = plt.gca()

    modes = sorted(df['mode'].unique())
    for mode in modes:
        # Filter for the current mode
        df_mode = df[df['mode'] == mode]
        
        # Group by the x-axis column and calculate the mean of the y-axis column
        # This aggregates results from multiple runs for the same (N, mode, BS)
        agg_data = df_mode.groupby(xcol)[ycol].mean().reset_index()
        
        # Sort by x-axis value to ensure lines are drawn correctly
        agg_data = agg_data.sort_values(by=xcol)
        
        if not agg_data.empty:
            ax.plot(agg_data[xcol], agg_data[ycol], marker='o', linestyle='-', label=str(mode))

    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.legend(title='Mode')
    plt.grid(True, which='both', linestyle='--', linewidth=0.5)
    # Use a logarithmic scale for the x-axis if N varies over a large range
    if df[xcol].nunique() > 1 and df[xcol].max() / df[xcol].min() > 10:
        ax.set_xscale('log')
    plt.tight_layout()
    plt.savefig(outpath)
    plt.close()

# -----------------------
# Analysis & plots
# -----------------------
def analyze_and_plot(df: pd.DataFrame, results_dir: Path):
    plotdir = ensure_plot_dir(results_dir)

    # Basic computed metrics
    # IPC = instructions / cycles (if both present)
    if 'PAPI_TOT_INS' in df.columns and 'PAPI_TOT_CYC' in df.columns:
        df['IPC'] = df['PAPI_TOT_INS'] / df['PAPI_TOT_CYC']
    else:
        df['IPC'] = np.nan

    # Branch mispredict rate = BR_MSP / BR_INS (if both present)
    if 'PAPI_BR_MSP' in df.columns and 'PAPI_BR_INS' in df.columns:
        df['BR_MISP_RATE'] = df['PAPI_BR_MSP'] / df['PAPI_BR_INS']
    elif 'PAPI_BR_MSP' in df.columns:
        df['BR_MISP_RATE'] = df['PAPI_BR_MSP'] # absolute if denom unavailable
    else:
        df['BR_MISP_RATE'] = np.nan

    # L3 miss rate (if L3 and loads present)
    if 'PAPI_L3_TCM' in df.columns and 'PAPI_LD_INS' in df.columns:
        df['L3_MISS_RATE'] = df['PAPI_L3_TCM'] / df['PAPI_LD_INS']
    elif 'PAPI_L3_TCM' in df.columns:
        df['L3_MISS_RATE'] = df['PAPI_L3_TCM']
    else:
        df['L3_MISS_RATE'] = np.nan

    # Energy column if present (try several RAPL names)
    energy_cols = [c for c in df.columns if 'ENERGY' in c.upper() or 'PACKAGE_ENERGY' in c.upper() or 'rapl' in c.lower()]
    if energy_cols:
        # pick first
        df['ENERGY_J'] = df[energy_cols[0]]
    else:
        df['ENERGY_J'] = np.nan

    # Save aggregated table
    agg_out = results_dir / "analysis_table.csv"
    df.to_csv(agg_out, index=False)
    print(f"Wrote aggregated table to {agg_out}")

    # For plotting we will group by (mode, BS)
    df_grouped = df.copy()
    # ensure string mode col
    df_grouped['mode'] = df_grouped['mode'].astype(str)
    df_grouped['BS'] = df_grouped['BS'].astype(int)

    # --- Section 1: Bar Plots (Original) ---
    print("Generating bar plots...")
    # 1) Time (seconds) by mode & BS (mean)
    timeagg = df_grouped.groupby(['mode','BS'])['elapsed_s'].agg(['mean','std','count']).reset_index()
    timeagg['label'] = timeagg['mode'] + " | BS=" + timeagg['BS'].astype(str)
    plot_bar(timeagg, 'label', 'mean', 'std',
             'Elapsed time by mode and block size', 'mode | BS', 'seconds',
             plotdir / 'time_by_mode_bs.png')
    print("Saved plot:", plotdir / 'time_by_mode_bs.png')

    # 2) IPC by mode & BS
    ipcagg = df_grouped.groupby(['mode','BS'])['IPC'].agg(['mean','std','count']).reset_index()
    ipcagg['label'] = ipcagg['mode'] + " | BS=" + ipcagg['BS'].astype(str)
    plot_bar(ipcagg, 'label', 'mean', 'std',
             'IPC by mode and block size', 'mode | BS', 'IPC',
             plotdir / 'ipc_by_mode_bs.png')
    print("Saved plot:", plotdir / 'ipc_by_mode_bs.png')

    # 3) Branch mispredict rate
    bragg = df_grouped.groupby(['mode','BS'])['BR_MISP_RATE'].agg(['mean','std','count']).reset_index()
    bragg['label'] = bragg['mode'] + " | BS=" + bragg['BS'].astype(str)
    plot_bar(bragg, 'label', 'mean', 'std',
             'Branch mispredict rate by mode and block size', 'mode | BS', 'branch mispredicts (rate or count)',
             plotdir / 'branch_misp_by_mode_bs.png')
    print("Saved plot:", plotdir / 'branch_misp_by_mode_bs.png')

    # 4) L3 miss rate
    l3agg = df_grouped.groupby(['mode','BS'])['L3_MISS_RATE'].agg(['mean','std','count']).reset_index()
    l3agg['label'] = l3agg['mode'] + " | BS=" + l3agg['BS'].astype(str)
    plot_bar(l3agg, 'label', 'mean', 'std',
             'L3 miss rate by mode and block size', 'mode | BS', 'L3 misses (rate or count)',
             plotdir / 'l3miss_by_mode_bs.png')
    print("Saved plot:", plotdir / 'l3miss_by_mode_bs.png')

    # 5) Energy (if present)
    if not df['ENERGY_J'].isna().all():
        eagg = df_grouped.groupby(['mode','BS'])['ENERGY_J'].agg(['mean','std','count']).reset_index()
        eagg['label'] = eagg['mode'] + " | BS=" + eagg['BS'].astype(str)
        plot_bar(eagg, 'label', 'mean', 'std',
                 'Energy (J) by mode and block size', 'mode | BS', 'Energy (J)',
                 plotdir / 'energy_by_mode_bs.png')
        print("Saved plot:", plotdir / 'energy_by_mode_bs.png')
    else:
        print("Energy counters not found in data; skipping energy plot.")
    print("All bar plots saved in", plotdir)

    # --- Section 2: Line Plots by Matrix Size (New) ---
    print("\nGenerating curve plots by matrix size (N) for each block size...")
    block_sizes = sorted(df['BS'].unique())
    metrics_to_plot = {
        'elapsed_s': {'title': 'Elapsed Time', 'ylabel': 'Time (s)'},
        'IPC': {'title': 'IPC', 'ylabel': 'IPC'},
        'BR_MISP_RATE': {'title': 'Branch Mispredict Rate', 'ylabel': 'Mispredict Rate'},
        'L3_MISS_RATE': {'title': 'L3 Miss Rate', 'ylabel': 'Miss Rate'},
        'ENERGY_J': {'title': 'Energy', 'ylabel': 'Energy (J)'}
    }

    for bs in block_sizes:
        df_bs = df[df['BS'] == bs]

        for metric, labels in metrics_to_plot.items():
            # Check if the metric column exists and has data for the current block size
            if metric in df_bs.columns and not df_bs[metric].isna().all():
                plot_filename = f'{metric.lower()}_by_n_bs_{bs}.png'
                plot_path = plotdir / plot_filename
                plot_title = f'{labels["title"]} vs. Matrix Size (N) for Block Size={bs}'

                plot_curves_by_mode(
                    df=df_bs,
                    xcol='N',
                    ycol=metric,
                    title=plot_title,
                    xlabel='Matrix Size (N)',
                    ylabel=labels['ylabel'],
                    outpath=plot_path
                )
                print(f"Saved plot: {plot_path}")

    print("\nAll plots saved in", plotdir)


# -----------------------
# Main
# -----------------------
def main():
    ap = argparse.ArgumentParser(description="Parse results and create matplotlib plots.")
    ap.add_argument("--results-dir", "-r", default="results", help="Path to results dir containing runs.csv and logs")
    args = ap.parse_args()
    results_dir = Path(args.results_dir)
    df = build_dataframe(results_dir)
    if df is None or df.shape[0] == 0:
        print("No data found.")
        return
    analyze_and_plot(df, results_dir)

if __name__ == "__main__":
    main()
