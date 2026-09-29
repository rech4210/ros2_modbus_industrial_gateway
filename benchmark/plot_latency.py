"""Generate latency and jitter visualization graph.

Conforms strictly to spec_production.md Section 5 Step 10.
Outputs:
- benchmark/latency_jitter.png
"""

import csv
import json
import os
import sys
from typing import Any, Dict, List

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

BENCHMARK_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BENCHMARK_DIR, "latency.csv")
SUMMARY_JSON_PATH = os.path.join(BENCHMARK_DIR, "summary.json")
FAULT_CSV_PATH = os.path.join(BENCHMARK_DIR, "fault_events.csv")
OUTPUT_PNG_PATH = os.path.join(BENCHMARK_DIR, "latency_jitter.png")


def load_data():
    if not os.path.exists(CSV_PATH):
        raise FileNotFoundError(f"Missing benchmark CSV: {CSV_PATH}")

    samples = []
    with open(CSV_PATH, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            samples.append({
                "index": int(row["sample_index"]),
                "rtt_ms": float(row["read_rtt_ms"]),
                "interval_ms": float(row["poll_interval_ms"]),
                "jitter_ms": float(row["poll_jitter_ms"]),
                "ros_lat_ms": float(row["sample_to_ros_ms"]),
            })

    summary = {}
    if os.path.exists(SUMMARY_JSON_PATH):
        with open(SUMMARY_JSON_PATH, "r", encoding="utf-8") as f:
            summary = json.load(f)

    fault_events = []
    if os.path.exists(FAULT_CSV_PATH):
        with open(FAULT_CSV_PATH, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row.get("latency_ms") and row["latency_ms"] != "N/A":
                    try:
                        fault_events.append({
                            "name": row["scenario_name"],
                            "mode": row["fault_mode"],
                            "lat_ms": float(row["latency_ms"]),
                        })
                    except ValueError:
                        pass

    return samples, summary, fault_events


def generate_plot():
    samples, summary, fault_events = load_data()
    indices = [s["index"] for s in samples]
    rtt_ms = [s["rtt_ms"] for s in samples]
    jitter_ms = [s["jitter_ms"] for s in samples]
    ros_lat_ms = [s["ros_lat_ms"] for s in samples]

    metrics = summary.get("metrics", {})
    rtt_stats = metrics.get("modbus_read_rtt_ms", {})
    jitter_stats = metrics.get("polling_jitter_ms", {})

    plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=150)
    fig.suptitle("ROS 2 Modbus Industrial Gateway - Real-Time Performance & Jitter Benchmark", fontsize=15, fontweight="bold", y=0.98)

    # 1. Modbus FC03 Read RTT Time Series
    ax1 = axes[0, 0]
    ax1.plot(indices, rtt_ms, color="#1f77b4", alpha=0.8, linewidth=1.0, label="1-FC03 Bulk Read RTT")
    p50 = rtt_stats.get("p50", np.percentile(rtt_ms, 50))
    p95 = rtt_stats.get("p95", np.percentile(rtt_ms, 95))
    p99 = rtt_stats.get("p99", np.percentile(rtt_ms, 99))
    ax1.axhline(p50, color="green", linestyle="--", linewidth=1.2, label=f"P50: {p50:.2f}ms")
    ax1.axhline(p95, color="orange", linestyle="--", linewidth=1.2, label=f"P95: {p95:.2f}ms")
    ax1.axhline(p99, color="red", linestyle="--", linewidth=1.4, label=f"P99: {p99:.2f}ms")
    ax1.axhline(5.0, color="darkred", linestyle=":", linewidth=1.5, label="SLA Ceiling (5.0ms)")
    ax1.set_title(f"Modbus FC03 Single Bulk Read RTT ({len(samples):,} Samples)", fontsize=11, fontweight="bold")
    ax1.set_xlabel("Sample Index (50Hz / 20ms period)")
    ax1.set_ylabel("Round Trip Time (ms)")
    ax1.set_ylim(0, max(6.0, max(rtt_ms) * 1.2))
    ax1.legend(loc="upper right", frameon=True, fontsize=8)

    # 2. Polling Jitter Distribution
    ax2 = axes[0, 1]
    counts, bins, patches = ax2.hist(jitter_ms, bins=40, color="#2ca02c", edgecolor="black", alpha=0.75, density=True)
    j_p50 = jitter_stats.get("p50", np.percentile(jitter_ms, 50))
    j_p99 = jitter_stats.get("p99", np.percentile(jitter_ms, 99))
    ax2.axvline(j_p50, color="darkgreen", linestyle="--", linewidth=1.2, label=f"Median Jitter: {j_p50:.2f}ms")
    ax2.axvline(j_p99, color="orange", linestyle="--", linewidth=1.4, label=f"P99 Jitter: {j_p99:.2f}ms")
    ax2.axvline(2.0, color="darkred", linestyle=":", linewidth=1.5, label="SLA Jitter Limit (2.0ms)")
    ax2.set_title("Polling Interval Jitter Distribution (|Δt - 20ms|)", fontsize=11, fontweight="bold")
    ax2.set_xlabel("Jitter (ms)")
    ax2.set_ylabel("Probability Density")
    ax2.set_xlim(0, max(2.5, max(jitter_ms) * 1.1))
    ax2.legend(loc="upper right", frameon=True, fontsize=8)

    # 3. Sample to ROS Topic Latency
    ax3 = axes[1, 0]
    ax3.hist(ros_lat_ms, bins=40, color="#9467bd", edgecolor="black", alpha=0.75)
    ros_p99 = np.percentile(ros_lat_ms, 99)
    ros_med = np.median(ros_lat_ms)
    ax3.axvline(ros_med, color="purple", linestyle="--", linewidth=1.2, label=f"Median: {ros_med:.2f}ms")
    ax3.axvline(ros_p99, color="red", linestyle="--", linewidth=1.2, label=f"P99: {ros_p99:.2f}ms")
    ax3.axvline(10.0, color="darkred", linestyle=":", linewidth=1.5, label="Budget Limit (10.0ms)")
    ax3.set_title("Modbus Sample -> ROS 2 Topic Delivery Latency", fontsize=11, fontweight="bold")
    ax3.set_xlabel("Delivery Latency (ms)")
    ax3.set_ylabel("Count")
    ax3.legend(loc="upper right", frameon=True, fontsize=8)

    # 4. Fault Reaction E2E Latency vs 100ms Deadline
    ax4 = axes[1, 1]
    if fault_events:
        def clean_name(n: str, m: str) -> str:
            if "DROP" in m:
                return "DROP Fault (3x Timeout)"
            elif "DISCONNECT" in m:
                return "TCP DISCONNECT (RST)"
            elif "DELAY" in m:
                return "DELAY 200ms (3x Timeout)"
            elif "FREEZE" in m:
                return "FREEZE (80ms Stale)"
            return m

        names = [clean_name(f["name"], f["mode"]) for f in fault_events]
        lats = [f["lat_ms"] for f in fault_events]
        bars = ax4.barh(names, lats, color=["#d62728" if l > 100.0 else "#1f77b4" for l in lats], edgecolor="black", alpha=0.8)
        ax4.axvline(100.0, color="red", linestyle="--", linewidth=1.8, label="100ms E2E SLA Deadline")
        for bar, lat in zip(bars, lats):
            ax4.text(lat + 1.5, bar.get_y() + bar.get_height() / 2, f"{lat:.1f}ms", va="center", fontsize=9, fontweight="bold")
        ax4.set_title("Fault Injection -> Alarm Delivery Latency (E2E SLA)", fontsize=11, fontweight="bold")
        ax4.set_xlabel("End-to-End Latency (ms)")
        ax4.set_xlim(0, 120)
        ax4.legend(loc="lower right", frameon=True, fontsize=8)
    else:
        ax4.text(0.5, 0.5, "Fault events data not available", ha="center", va="center")
        ax4.set_title("Fault Injection Latency", fontsize=11, fontweight="bold")

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    plt.savefig(OUTPUT_PNG_PATH, dpi=150)
    plt.close()
    print(f"[PASS] Benchmark chart generated successfully: {OUTPUT_PNG_PATH}")


if __name__ == "__main__":
    generate_plot()
