#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = [
#   "pandas",
#   "numpy",
#   "matplotlib",
#   "seaborn",
# ]
# ///

"""
plot_comparison.py

Generates high-quality, readable comparative plots from benchmark results.
- Creates a separate plot file for each key performance metric.
- Creates a dedicated plot for IPC comparison.
"""

import argparse
import re
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

# --- Data Parsing Logic ---
PAPITO_COUNTERS_RE = re.compile(r'^PAPITO_COUNTERS\s*(.*)$')
PAPITO_VALUES_RE = re.compile(r'^PAPITO_VALUES\s*(.*)$')

def parse_papito_from_log(logpath: Path):
    counters, values = None, None
    try:
        with logpath.open('r', errors='ignore') as f:
            for line in f:
                if m := PAPITO_COUNTERS_RE.match(line):
                    counters = re.split(r'\s+', m.group(1).strip())
                elif m := PAPITO_VALUES_RE.match(line):
                    raw_values = re.split(r'\s+', m.group(1).strip())
                    values = [pd.to_numeric(v, errors='coerce') for v in raw_values]
    except FileNotFoundError:
        return None

    if counters and values and len(counters) == len(values):
        return dict(zip(counters, values))
    return None

def build_dataframe(results_dir: Path):
    runs_csv_path = results_dir / "runs.csv"
    if not runs_csv_path.exists():
        raise FileNotFoundError(f"'{runs_csv_path}' not found. Please run experiments first.")
    
    df = pd.read_csv(runs_csv_path)
    df['elapsed_s'] = pd.to_numeric(df['elapsed_s'], errors='coerce')

    papi_data = [parse_papito_from_log(results_dir / Path(row['logfile']).name) for _, row in df.iterrows()]
    papi_df = pd.DataFrame([d if d else {} for d in papi_data], index=df.index)
    
    return pd.concat([df, papi_df], axis=1)

def calculate_metrics(df: pd.DataFrame):
    df_calc = df.copy()
    if 'PAPI_TOT_INS' in df_calc.columns and 'PAPI_TOT_CYC' in df_calc.columns:
        tot_ins = pd.to_numeric(df_calc['PAPI_TOT_INS'], errors='coerce')
        tot_cyc = pd.to_numeric(df_calc['PAPI_TOT_CYC'], errors='coerce')
        df_calc['IPC'] = tot_ins.div(tot_cyc).fillna(0).replace([np.inf, -np.inf], 0)
    else:
        print("Warning: 'PAPI_TOT_INS' or 'PAPI_TOT_CYC' not found. Cannot calculate IPC.")
        df_calc['IPC'] = np.nan
    return df_calc

# --- Enhanced Plotting Functions ---

def plot_individual_metrics(df: pd.DataFrame, plot_dir: Path):
    """
    Creates a separate, large, and readable plot for each key performance metric.
    """
    metrics_to_plot = {
        'elapsed_s': 'Execution Time (s)',
        'PAPI_TOT_CYC': 'Total Cycles',
        'PAPI_FP_OPS': 'Floating Point Operations',
        'PAPI_VEC_INS': 'Vector Instructions'
    }
    
    grouped = df.groupby(['mode', 'N', 'BS']).mean(numeric_only=True).reset_index()

    for metric, title in metrics_to_plot.items():
        if metric not in df.columns or df[metric].isnull().all():
            print(f"Metric '{metric}' not found in data. Skipping plot.")
            continue

        plt.figure(figsize=(14, 8))
        sns.set_theme(style="whitegrid", font_scale=1.2)
        
        ax = sns.lineplot(
            data=grouped, x='N', y=metric, hue='mode', style='BS',
            markers=True, dashes=True, legend='full',
            palette='bright', linewidth=2.5, markersize=8
        )
        
        ax.set_title(f'Performance Comparison: {title}', fontsize=20, weight='bold')
        ax.set_ylabel(title, fontsize=14)
        ax.set_xlabel('Matrix Size (N)', fontsize=14)
        ax.legend(title='Algorithm | Block Size', fontsize=12)
        ax.set_xscale('log', base=2)
        ax.grid(True, which="both", ls="--")

        out_path = plot_dir / f"comparison_{metric}.png"
        plt.tight_layout()
        print(f"Saving plot to {out_path}")
        plt.savefig(out_path, dpi=150)
        plt.close()


def plot_ipc_comparison(df: pd.DataFrame, out_path: Path):
    """Creates a large, readable, dedicated plot for IPC comparison."""
    if 'IPC' not in df.columns or df['IPC'].isnull().all():
        print("IPC data is not available, skipping IPC plot.")
        return

    grouped = df.groupby(['mode', 'N', 'BS'])['IPC'].mean().reset_index()

    plt.figure(figsize=(14, 8))
    sns.set_theme(style="whitegrid", font_scale=1.2)
    
    ax = sns.lineplot(
        data=grouped, x='N', y='IPC', hue='mode', style='BS',
        markers=True, dashes=True, legend='full',
        palette='bright', linewidth=2.5, markersize=8
    )
    
    ax.set_title('Instructions Per Cycle (IPC) Comparison', fontsize=20, weight='bold')
    ax.set_ylabel('IPC', fontsize=14)
    ax.set_xlabel('Matrix Size (N)', fontsize=14)
    ax.legend(title='Algorithm | Block Size', fontsize=12)
    ax.set_xscale('log', base=2)
    ax.grid(True, which="both", ls="--")

    plt.tight_layout()
    print(f"Saving IPC comparison plot to {out_path}")
    plt.savefig(out_path, dpi=150)
    plt.close()

# --- Main Execution ---
def main():
    parser = argparse.ArgumentParser(description="Generate comparative plots from benchmark results.")
    parser.add_argument("--results-dir", "-r", type=Path, default=Path("results"),
                        help="Path to the results directory containing runs.csv and log files.")
    args = parser.parse_args()

    if not args.results_dir.exists():
        print(f"Error: Results directory '{args.results_dir}' not found.")
        return

    try:
        df = build_dataframe(args.results_dir)
        df_analyzed = calculate_metrics(df)
        
        plot_dir = args.results_dir / "plots"
        plot_dir.mkdir(exist_ok=True)
        
        # Generate a separate file for each performance metric
        plot_individual_metrics(df_analyzed, plot_dir)
        
        # Generate the dedicated IPC plot
        plot_ipc_comparison(df_analyzed, plot_dir / "comparison_ipc.png")

        print("\nPlotting complete! 🎨 Check the 'results/plots/' directory for your graphs.")

    except FileNotFoundError as e:
        print(f"Error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    main()
