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

Um script unificado para gerar gráficos comparativos de alta qualidade a partir de resultados de benchmark.
Ele cria gráficos separados e bem organizados para cada métrica e tamanho de bloco para garantir clareza,
oferece argumentos de linha de comando para filtrar, renomear e adicionar uma linha de base.
"""

import argparse
import re
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

# --- Lógica de Extração de Dados ---
PAPITO_COUNTERS_RE = re.compile(r"^PAPITO_COUNTERS\s*(.*)$")
PAPITO_VALUES_RE = re.compile(r"^PAPITO_VALUES\s*(.*)$")

def parse_papito_from_log(logpath: Path):
    """Extrai os contadores e valores do PAPI de um arquivo de log."""
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
        print(f"Aviso: Arquivo de log não encontrado em {logpath}")
        return None

    if counters and values and len(counters) == len(values):
        return dict(zip(counters, values))
    return None


def build_dataframe(results_dir: Path):
    """Constrói um DataFrame do pandas a partir do runs.csv e arquivos de log associados."""
    runs_csv_path = results_dir / "runs.csv"
    if not runs_csv_path.exists():
        raise FileNotFoundError(
            f"'{runs_csv_path}' não encontrado. Por favor, execute os experimentos primeiro."
        )

    df = pd.read_csv(runs_csv_path)
    df["elapsed_s"] = pd.to_numeric(df["elapsed_s"], errors="coerce")

    papi_data = [
        parse_papito_from_log(results_dir / Path(row["logfile"]).name)
        for _, row in df.iterrows()
    ]
    papi_df = pd.DataFrame([d if d else {} for d in papi_data], index=df.index)

    return pd.concat([df, papi_df], axis=1)


def calculate_metrics(df: pd.DataFrame, rename_map: dict):
    """Calcula métricas derivadas como IPC, Taxas de Acerto de Cache e percentuais de instruções."""
    df_calc = df.copy()

    if rename_map:
        df_calc['mode'] = df_calc['mode'].replace(rename_map)

    df_calc["display_mode"] = df_calc.apply(
        lambda row: f"{row['mode']} ({row['tuning']})"
        if pd.notna(row["tuning"]) and row["tuning"] != "NA"
        else row["mode"],
        axis=1,
    )

    if "PAPI_TOT_INS" in df_calc.columns and "PAPI_TOT_CYC" in df_calc.columns:
        tot_ins = pd.to_numeric(df_calc["PAPI_TOT_INS"], errors="coerce")
        tot_cyc = pd.to_numeric(df_calc["PAPI_TOT_CYC"], errors="coerce")
        df_calc["IPC"] = tot_ins.div(tot_cyc).fillna(0).replace([np.inf, -np.inf], 0)
    else:
        df_calc["IPC"] = np.nan

    if 'PAPI_L1_DCA' in df.columns and 'PAPI_L1_DCM' in df.columns:
        l1_accesses = pd.to_numeric(df_calc['PAPI_L1_DCA'], errors='coerce')
        l1_misses = pd.to_numeric(df_calc['PAPI_L1_DCM'], errors='coerce')
        l1_hits = l1_accesses - l1_misses
        df_calc['L1_HIT_RATE'] = np.where(l1_accesses == 0, 1.0, l1_hits / l1_accesses) * 100
    else:
        df_calc['L1_HIT_RATE'] = np.nan

    if 'PAPI_L2_DCH' in df.columns and 'PAPI_L2_DCM' in df.columns:
        l2_hits = pd.to_numeric(df_calc['PAPI_L2_DCH'], errors='coerce')
        l2_misses = pd.to_numeric(df_calc['PAPI_L2_DCM'], errors='coerce')
        l2_accesses = l2_hits + l2_misses
        df_calc['L2_HIT_RATE'] = np.where(l2_accesses == 0, 1.0, l2_hits / l2_accesses) * 100
    else:
        df_calc['L2_HIT_RATE'] = np.nan
        
    if 'PAPI_VEC_INS' in df.columns and 'PAPI_TOT_INS' in df.columns:
        vec_ins = pd.to_numeric(df_calc['PAPI_VEC_INS'], errors='coerce')
        tot_ins = pd.to_numeric(df_calc['PAPI_TOT_INS'], errors='coerce')
        df_calc['VEC_INS_PERCENT'] = vec_ins.div(tot_ins).fillna(0).replace([np.inf, -np.inf], 0) * 100
    else:
        df_calc['VEC_INS_PERCENT'] = np.nan

    if 'PAPI_FP_INS' in df.columns and 'PAPI_VEC_INS' in df.columns:
        fp_ins = pd.to_numeric(df_calc['PAPI_FP_INS'], errors='coerce')
        vec_ins = pd.to_numeric(df_calc['PAPI_VEC_INS'], errors='coerce')
        df_calc['VECTORIZED_FP_PERCENT'] = vec_ins.div(fp_ins).fillna(0).replace([np.inf, -np.inf], 0) * 100
    else:
        df_calc['VECTORIZED_FP_PERCENT'] = np.nan

    return df_calc


# --- Funções de Plotagem ---

def plot_by_bs_and_metric(df: pd.DataFrame, plot_dir: Path):
    """
    Gera um gráfico separado para cada métrica e tamanho de bloco.
    """
    if df.empty:
        print("Nenhum dado disponível para plotar após a filtragem.")
        return

    metrics_to_plot = {
        "IPC": "Instruções por Ciclo (IPC)",
        "PAPI_TOT_CYC": "Total de Ciclos",
        "energy_J": "Consumo de Energia (Joules)",
        "L1_HIT_RATE": "Taxa de Acerto do Cache L1 (%)",
        "L2_HIT_RATE": "Taxa de Acerto do Cache L2 (%)",
        "VEC_INS_PERCENT": "Percentual de Instruções Vetoriais (%)",
        "VECTORIZED_FP_PERCENT": "Percentual de FP Vetorizado (%)",
    }
    
    baseline_df = df[df['mode_orig'] == 'blas_whole'].copy()
    if baseline_df.empty:
        print("Aviso: Dados 'blas_whole' não encontrados. Não será possível adicionar como linha de base.")

    block_sizes = sorted(df[df["BS"] > 0]["BS"].unique())
    if not block_sizes:
        print("Nenhum modo com blocos para plotar.")
        return

    for bs in block_sizes:
        df_bs = df[df["BS"] == bs]
        if df_bs.empty and baseline_df.empty:
            continue
            
        combined_df = pd.concat([df_bs, baseline_df], ignore_index=True)

        for metric, title in metrics_to_plot.items():
            if metric not in combined_df.columns or combined_df[metric].isnull().all():
                continue

            grouped = (
                combined_df.groupby(["mode", "display_mode", "N"]).mean(numeric_only=True).reset_index()
            )

            if grouped.empty or metric not in grouped.columns or grouped[metric].isnull().all():
                continue

            # --- MUDANÇA: Renomeia as colunas para legendas mais claras ---
            grouped.rename(columns={
                "mode": "Estratégia",
                "display_mode": "Configuração"
            }, inplace=True)

            plt.figure(figsize=(14, 8))
            sns.set_theme(style="whitegrid", font_scale=1.2)
            ax = plt.gca()

            sns.lineplot(
                data=grouped,
                x="N",
                y=metric,
                hue="Estratégia",      # <-- Usa a coluna renomeada
                style="Configuração",  # <-- Usa a coluna renomeada
                markers=True,
                dashes=False,
                legend="full",
                palette="bright",
                linewidth=2.5,
                markersize=10,
                ax=ax,
            )

            ax.set_title(f"{title} vs. Tamanho da Matriz (N) para Bloco={bs}", fontsize=20, weight="bold")
            ax.set_ylabel(title, fontsize=14)
            ax.set_xlabel("Tamanho da Matriz (N)", fontsize=14)
            # A legenda agora é gerada automaticamente com os nomes corretos
            ax.set_xscale("log", base=2)
            ax.grid(True, which="both", ls="--")

            out_path = plot_dir / f"BS{bs}_{metric}.png"
            plt.tight_layout()
            print(f"Salvando gráfico em {out_path}")
            plt.savefig(out_path, dpi=150)
            plt.close()

def plot_best_vs_whole(df: pd.DataFrame, plot_dir: Path):
    """
    Compara as estratégias 'whole' contra a melhor e a pior estratégia em blocos.
    """
    whole_modes_df = df[df['mode_orig'].str.contains('_whole', na=False)]
    block_modes_df = df[~df['mode_orig'].str.contains('_whole', na=False)]

    if block_modes_df.empty:
        print("Nenhum modo em blocos encontrado para a comparação 'melhor/pior vs. whole'.")
        return

    # --- MUDANÇA: Encontra o melhor E o pior caso ---
    best_block_idx = block_modes_df.groupby('N')['elapsed_s'].idxmin()
    worst_block_idx = block_modes_df.groupby('N')['elapsed_s'].idxmax()

    best_block_runs = block_modes_df.loc[best_block_idx].copy()
    worst_block_runs = block_modes_df.loc[worst_block_idx].copy()
    
    best_block_runs['display_mode'] = 'Melhor Estratégia em Bloco'
    worst_block_runs['display_mode'] = 'Pior Estratégia em Bloco'

    # Combina todos os dados para o gráfico
    comparison_df = pd.concat([whole_modes_df, best_block_runs, worst_block_runs], ignore_index=True)

    metrics_to_plot = {
        "IPC": "Instruções por Ciclo (IPC)",
        "PAPI_TOT_CYC": "Total de Ciclos",
        "energy_J": "Consumo de Energia (Joules)",
    }

    for metric, title in metrics_to_plot.items():
        if metric not in comparison_df.columns or comparison_df[metric].isnull().all():
            continue
        
        grouped = comparison_df.groupby(["display_mode", "N"]).mean(numeric_only=True).reset_index()

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
            palette="viridis",
            linewidth=2.5,
            markersize=8,
        )

        ax.set_title(f"Melhor/Pior em Bloco vs. Matriz Inteira: {title}", fontsize=20, weight="bold")
        ax.set_ylabel(title, fontsize=14)
        ax.set_xlabel("Tamanho da Matriz (N)", fontsize=14)
        ax.legend(title="Estratégia", fontsize=12)
        ax.set_xscale("log", base=2)
        ax.grid(True, which="both", ls="--")

        out_path = plot_dir / f"best_vs_whole_{metric}.png"
        plt.tight_layout()
        print(f"Salvando gráfico de comparação em {out_path}")
        plt.savefig(out_path, dpi=150)
        plt.close()

# --- Execução Principal ---
def main():
    parser = argparse.ArgumentParser(
        description="Gera gráficos comparativos de alta qualidade a partir de resultados de benchmark."
    )
    parser.add_argument(
        "--results-dir",
        "-r",
        type=Path,
        default=Path("results"),
        help="Caminho para o diretório de resultados com runs.csv e arquivos de log.",
    )
    parser.add_argument(
        "--output-dir",
        "-o",
        type=Path,
        help="Caminho para o diretório de saída dos gráficos. Padrão: [results-dir]/plots.",
    )
    parser.add_argument(
        "--sizes",
        type=int,
        nargs="+",
        help="Uma lista de tamanhos de matriz (N) para incluir nos gráficos.",
    )
    parser.add_argument(
        "--rename",
        nargs="+",
        help="Renomeia estratégias na legenda. Formato: 'nome_antigo:Novo Nome'",
    )
    parser.add_argument(
        "--filter",
        nargs="+",
        help="Uma lista de modos para incluir nos gráficos (ex: avx scalar blas).",
    )
    parser.add_argument(
        "--blacklist",
        nargs="+",
        help="Uma lista de modos para excluir dos gráficos.",
    )
    args = parser.parse_args()

    try:
        if not args.results_dir.exists():
            print(f"Erro: Diretório de resultados '{args.results_dir}' não encontrado.")
            return

        df = build_dataframe(args.results_dir)
        
        rename_map = {}
        if args.rename:
            for item in args.rename:
                if ':' not in item:
                    print(f"Aviso: Formato inválido para --rename '{item}'. Use 'antigo:novo'.")
                    continue
                old, new = item.split(':', 1)
                rename_map[old] = new.strip('"\'')

        df['mode_orig'] = df['mode']
        df_analyzed = calculate_metrics(df, rename_map)
        
        if args.sizes:
            print(f"Filtrando resultados para incluir apenas os tamanhos: {', '.join(map(str, args.sizes))}")
            df_analyzed = df_analyzed[df_analyzed["N"].isin(args.sizes)]

        if args.filter:
            print(f"Filtrando resultados para incluir apenas os modos: {', '.join(args.filter)}")
            df_analyzed = df_analyzed[df_analyzed["mode_orig"].isin(args.filter)]
            
        if args.blacklist:
            print(f"Excluindo modos da lista negra: {', '.join(args.blacklist)}")
            df_analyzed = df_analyzed[~df_analyzed["display_mode"].isin(args.blacklist)]

        if args.output_dir:
            plot_dir = args.output_dir
        else:
            plot_dir = args.results_dir / "plots"
        plot_dir.mkdir(exist_ok=True, parents=True)
        
        plot_by_bs_and_metric(df_analyzed, plot_dir)
        plot_best_vs_whole(df_analyzed, plot_dir)

        print(
            f"\nPlotagem completa! 🎨 Seus novos gráficos estão no diretório '{plot_dir}'."
        )

    except FileNotFoundError as e:
        print(f"Erro: {e}")
    except Exception as e:
        print(f"Um erro inesperado ocorreu: {e}")


if __name__ == "__main__":
    main()