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
plot_results.py

A unified script to generate high-quality, readable comparative plots from benchmark results.
It creates separate, well-organized plots for each metric and block size to ensure clarity
and provides a '--filter' argument to select specific modes for plotting.
"""

import argparse
import re
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

# --- Data Parsing Logic ---
PAPITO_COUNTERS_RE = re.compile(r"^PAPITO_COUNTERS\s*(.*)$")
PAPITO_VALUES_RE = re.compile(r"^PAPITO_VALUES\s*(.*)$")

def parse_papito_from_log(logpath: Path):
    """Parses a log file to extract PAPI counters and values."""
    counters, values = None, None
    try:
        with logpath.open("r", errors="ignore") as f:
            for line in f:
                if m := PAPITO_COUNTERS_RE.match(line):
                    counters = re.split(r"\s+", m.group(1).strip())
                elif m := PAPITO_VALUES_RE.match(line):
                    raw_values = re.split(r"\s+", m.group(1).strip())
                    values = [pd.to_numeric(v, errors="coerce") for v in raw_values]
    except FileNotFoundError:
        print(f"Warning: Log file not found at {logpath}")
        return None

    if counters and values and len(counters) == len(values):
        return dict(zip(counters, values))
    return None


def build_dataframe(results_dir: Path):
    """Builds a pandas DataFrame from runs.csv and associated log files."""
    runs_csv_path = results_dir / "runs.csv"
    if not runs_csv_path.exists():
        raise FileNotFoundError(
            f"'{runs_csv_path}' not found. Please run experiments first using run_and_measure.sh."
        )

    df = pd.read_csv(runs_csv_path)
    df["elapsed_s"] = pd.to_numeric(df["elapsed_s"], errors="coerce")

    # Parse PAPI data from each log file
    papi_data = [
        parse_papito_from_log(results_dir / Path(row["logfile"]).name)
        for _, row in df.iterrows()
    ]
    papi_df = pd.DataFrame([d if d else {} for d in papi_data], index=df.index)

    return pd.concat([df, papi_df], axis=1)


def calculate_metrics(df: pd.DataFrame):
    """Calculates derived metrics like IPC, L3 Miss Rate, and Branch Mispredict Rate."""
    df_calc = df.copy()

    # Calculate IPC
    if "PAPI_TOT_INS" in df_calc.columns and "PAPI_TOT_CYC" in df_calc.columns:
        tot_ins = pd.to_numeric(df_calc["PAPI_TOT_INS"], errors="coerce")
        tot_cyc = pd.to_numeric(df_calc["PAPI_TOT_CYC"], errors="coerce")
        df_calc["IPC"] = tot_ins.div(tot_cyc).fillna(0).replace([np.inf, -np.inf], 0)
    else:
        df_calc["IPC"] = np.nan

    # Calculate L3 Miss Rate
    if 'PAPI_L3_TCM' in df.columns and 'PAPI_LD_INS' in df.columns and df['PAPI_LD_INS'].sum() > 0:
        df_calc['L3_MISS_RATE'] = df['PAPI_L3_TCM'] / df['PAPI_LD_INS']
    else:
        df_calc['L3_MISS_RATE'] = np.nan
        
    # Calculate Branch Misprediction Rate
    if 'PAPI_BR_MSP' in df.columns and 'PAPI_BR_INS' in df.columns and df['PAPI_BR_INS'].sum() > 0:
        df_calc['BR_MISP_RATE'] = df['PAPI_BR_MSP'] / df['PAPI_BR_INS']
    else:
        df_calc['BR_MISP_RATE'] = np.nan

    # Create a display label for modes with tunings
    df_calc["display_mode"] = df_calc.apply(
        lambda row: f"{row['mode']} ({row['tuning']})"
        if pd.notna(row["tuning"]) and row["tuning"] != "NA"
        else row["mode"],
        axis=1,
    )
    return df_calc


# --- Enhanced Plotting Functions ---

def plot_by_bs_and_metric(df: pd.DataFrame, plot_dir: Path):
    """
    Generates a separate plot for each metric and each block size,
    showing performance vs. matrix size (N).
    """
    if df.empty:
        print("No data available to plot after filtering.")
        return

    metrics_to_plot = {
        "elapsed_s": "Execution Time (s)",
        "IPC": "Instructions Per Cycle (IPC)",
        "PAPI_TOT_CYC": "Total Cycles",
        "PAPI_FP_OPS": "Floating Point Operations",
        "PAPI_VEC_INS": "Vector Instructions",
        "L3_MISS_RATE": "L3 Cache Miss Rate",
        "BR_MISP_RATE": "Branch Misprediction Rate",
    }

    block_sizes = sorted(df[df["BS"] > 0]["BS"].unique())

    for bs in block_sizes:
        df_bs = df[df["BS"] == bs]
        if df_bs.empty:
            continue

        for metric, title in metrics_to_plot.items():
            if metric not in df_bs.columns or df_bs[metric].isnull().all():
                continue

            # Aggregate data by taking the mean of repeats
            grouped = (
                df_bs.groupby(["display_mode", "N"]).mean(numeric_only=True).reset_index()
            )

            if grouped.empty or metric not in grouped.columns or grouped[metric].isnull().all():
                continue

            plt.figure(figsize=(14, 8))
            sns.set_theme(style="whitegrid", font_scale=1.2)

            ax = sns.lineplot(
                data=grouped,
                x="N",
                y=metric,
                hue="display_mode",
                style="display_mode",
                markers=True,
                dashes=False,
                legend="full",
                palette="bright",
                linewidth=2.5,
                markersize=8,
            )

            ax.set_title(f"{title} vs. Matrix Size (N) for BS={bs}", fontsize=20, weight="bold")
            ax.set_ylabel(title, fontsize=14)
            ax.set_xlabel("Matrix Size (N)", fontsize=14)
            ax.legend(title="Algorithm (Tuning)", fontsize=12)
            ax.set_xscale("log", base=2)
            ax.grid(True, which="both", ls="--")

            out_path = plot_dir / f"BS{bs}_{metric}.png"
            plt.tight_layout()
            print(f"Saving plot to {out_path}")
            plt.savefig(out_path, dpi=150)
            plt.close()


# --- Main Execution ---
def main():
    parser = argparse.ArgumentParser(
        description="Generate high-quality comparative plots from benchmark results."
    )
    parser.add_argument(
        "--results-dir",
        "-r",
        type=Path,
        default=Path("results"),
        help="Path to the results directory with runs.csv and log files.",
    )
    parser.add_argument(
        "--filter",
        nargs="+",
        help="A list of modes to include in the plots (e.g., avx scalar blas).",
    )
    args = parser.parse_args()

    try:
        if not args.results_dir.exists():
            print(f"Error: Results directory '{args.results_dir}' not found.")
            return

        df = build_dataframe(args.results_dir)
        df_analyzed = calculate_metrics(df)

        if args.filter:
            print(f"Filtering results to include only modes: {', '.join(args.filter)}")
            df_analyzed = df_analyzed[df_analyzed["mode"].isin(args.filter)]

        plot_dir = args.results_dir / "plots"
        plot_dir.mkdir(exist_ok=True)

        plot_by_bs_and_metric(df_analyzed, plot_dir)

        print(
            "\nPlotting complete! 🎨 Your new, clearer graphs are in the 'results/plots/' directory."
        )

    except FileNotFoundError as e:
        print(f"Error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")


if __name__ == "__main__":
    main()