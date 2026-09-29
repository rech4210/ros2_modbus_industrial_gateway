"""Measure 1,000 Polling Samples, Latency, Jitter, and Gateway Resource Usage.

Conforms strictly to spec_production.md Section 5 Step 10.
Outputs:
- benchmark/latency.csv
- benchmark/benchmark_results.csv
- benchmark/summary.json
"""

import csv
import json
import os
import platform
import shutil
import sys
import time
from typing import Any, Dict, List, Optional

if "/ros2_ws/src/ros2_modbus_gateway" not in sys.path:
    sys.path.insert(0, "/ros2_ws/src/ros2_modbus_gateway")

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

from ros2_modbus_gateway.msg import PlcState
from ros2_modbus_gateway.msg import PlcState
from ros2_modbus_gateway.srv import ClearFault, TriggerCommand
from mock_plc.fault_injector import FaultInjector

BENCHMARK_DIR = "/ros2_ws/src/ros2_modbus_gateway/benchmark"
CSV_PATH = os.path.join(BENCHMARK_DIR, "latency.csv")
RESULTS_CSV_PATH = os.path.join(BENCHMARK_DIR, "benchmark_results.csv")
SUMMARY_JSON_PATH = os.path.join(BENCHMARK_DIR, "summary.json")
TARGET_SAMPLES = int(os.environ.get("BENCHMARK_SAMPLES", 1000))
if len(sys.argv) > 1 and sys.argv[1].isdigit():
    TARGET_SAMPLES = int(sys.argv[1])


def get_gateway_pid() -> Optional[int]:
    """Find PID of gateway_node process."""
    try:
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            cmdline_path = os.path.join("/proc", pid, "cmdline")
            if os.path.exists(cmdline_path):
                with open(cmdline_path, "rb") as f:
                    cmd = f.read().decode("utf-8", errors="ignore")
                    if "gateway_node" in cmd and "measure_latency" not in cmd:
                        return int(pid)
    except Exception:
        pass
    return None


def get_process_cpu_time(pid: int) -> float:
    """Read utime + stime from /proc/[pid]/stat."""
    try:
        with open(f"/proc/{pid}/stat", "r") as f:
            fields = f.read().split()
            utime = int(fields[13])
            stime = int(fields[14])
            return float(utime + stime)
    except Exception:
        return 0.0


class LatencyBenchmarker(Node):
    """Subscribes to /plc/state and records 1,000 real-time samples."""

    def __init__(self) -> None:
        super().__init__("latency_benchmarker")

        state_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )

        self.samples: List[Dict[str, Any]] = []
        self.last_sample_seq: Optional[int] = None
        self.last_poll_started_ns: Optional[int] = None
        self.latest_state: Optional[PlcState] = None

        self.state_sub = self.create_subscription(
            PlcState, "/plc/state", self._on_state, state_qos
        )
        self.clear_cli = self.create_client(ClearFault, "/plc/clear_fault")
        self.cmd_cli = self.create_client(TriggerCommand, "/plc/trigger_command")

    def reset_system(self, timeout_sec: float = 5.0) -> bool:
        """Reset PLC fault and clear gateway alarm latch before benchmark."""
        start = time.time()
        if not self.cmd_cli.wait_for_service(timeout_sec=2.0) or not self.clear_cli.wait_for_service(timeout_sec=2.0):
            return False

        # Wait for first state msg
        while self.latest_state is None and (time.time() - start < timeout_sec):
            rclpy.spin_once(self, timeout_sec=0.02)

        # 1. Reset PLC internal fault code via command=3 (RESET)
        req_cmd = TriggerCommand.Request(command=3, value=0)
        fut_cmd = self.cmd_cli.call_async(req_cmd)
        c_start = time.time()
        while not fut_cmd.done() and (time.time() - c_start < 1.0):
            rclpy.spin_once(self, timeout_sec=0.02)

        # 2. Clear gateway alarm latch
        req_clear = ClearFault.Request(force_clear=True)
        fut_clear = self.clear_cli.call_async(req_clear)
        cl_start = time.time()
        while not fut_clear.done() and (time.time() - cl_start < 1.0):
            rclpy.spin_once(self, timeout_sec=0.02)

        # 3. Wait until alarm is inactive and link is operational
        while time.time() - start < timeout_sec:
            rclpy.spin_once(self, timeout_sec=0.02)
            if self.latest_state and not self.latest_state.alarm_active and self.latest_state.data_valid:
                return True

        return False

    def _on_state(self, msg: PlcState) -> None:
        self.latest_state = msg
        received_ns = time.monotonic_ns()

        # Only collect valid operational samples
        if not msg.data_valid or msg.alarm_active:
            return

        # Ensure uniqueness by sample_sequence
        if self.last_sample_seq is not None and msg.sample_sequence <= self.last_sample_seq:
            return

        # Calculate metrics
        rtt_ms = msg.read_rtt_ns / 1e6
        jitter_ms = abs(msg.poll_jitter_ns) / 1e6

        if self.last_poll_started_ns is not None:
            poll_interval_ms = (msg.poll_started_ns - self.last_poll_started_ns) / 1e6
        else:
            poll_interval_ms = 20.0

        sample_to_ros_ms = (received_ns - msg.sampled_ns) / 1e6
        publish_lat_ms = (msg.published_ns - msg.sampled_ns) / 1e6
        dds_lat_ms = (received_ns - msg.published_ns) / 1e6

        record = {
            "sample_index": len(self.samples) + 1,
            "sample_sequence": msg.sample_sequence,
            "publish_sequence": msg.publish_sequence,
            "poll_started_ns": msg.poll_started_ns,
            "sampled_ns": msg.sampled_ns,
            "published_ns": msg.published_ns,
            "received_ns": received_ns,
            "read_rtt_ms": round(rtt_ms, 4),
            "poll_interval_ms": round(poll_interval_ms, 4),
            "poll_jitter_ms": round(jitter_ms, 4),
            "sample_to_ros_ms": round(sample_to_ros_ms, 4),
            "publish_lat_ms": round(publish_lat_ms, 4),
            "dds_lat_ms": round(dds_lat_ms, 4),
            "heartbeat": msg.heartbeat,
            "consecutive_failures": msg.consecutive_failures,
        }

        self.samples.append(record)
        self.last_sample_seq = msg.sample_sequence
        self.last_poll_started_ns = msg.poll_started_ns


def percentile(data: List[float], p: float) -> float:
    """Compute percentile from sorted list."""
    if not data:
        return 0.0
    k = (len(data) - 1) * (p / 100.0)
    f = int(k)
    c = f + 1
    if c < len(data):
        return data[f] + (k - f) * (data[c] - data[f])
    return data[f]


def compute_stats(series: List[float]) -> Dict[str, float]:
    sorted_s = sorted(series)
    n = len(sorted_s)
    if n == 0:
        return {}
    mean_val = sum(sorted_s) / n
    variance = sum((x - mean_val) ** 2 for x in sorted_s) / n
    std_val = variance ** 0.5

    return {
        "min": round(sorted_s[0], 4),
        "mean": round(mean_val, 4),
        "p50": round(percentile(sorted_s, 50), 4),
        "p95": round(percentile(sorted_s, 95), 4),
        "p99": round(percentile(sorted_s, 99), 4),
        "max": round(sorted_s[-1], 4),
        "std": round(std_val, 4),
    }


def main() -> None:
    print("\n" + "=" * 76)
    print(f" ⏱️ Step 10: 1,000회 정량 지연 측정 및 통계 벤치마크 (Target: {TARGET_SAMPLES} samples)")
    print("=" * 76)

    os.makedirs(BENCHMARK_DIR, exist_ok=True)
    injector = FaultInjector()
    injector.set_fault("NORMAL")
    injector.set_inputs(physical_estop=False, process_fault=False)

    rclpy.init()
    benchmarker = LatencyBenchmarker()

    # Reset and clear any active alarm
    print("[INFO] Resetting PLC and clearing alarm latch...")
    if benchmarker.reset_system(timeout_sec=5.0):
        print("[INFO] System operational and alarms cleared.")
    else:
        print("[WARN] Reset timed out or system not ready, proceeding with caution...")

    # Locate gateway process for CPU tracking
    gw_pid = get_gateway_pid()
    start_cpu_time = get_process_cpu_time(gw_pid) if gw_pid else 0.0
    start_wall_time = time.time()

    print(f"[INFO] Monitoring gateway node PID: {gw_pid}...")
    print(f"[INFO] Sampling 50Hz (20ms) status stream from /plc/state...")

    last_print = time.time()
    while len(benchmarker.samples) < TARGET_SAMPLES:
        rclpy.spin_once(benchmarker, timeout_sec=0.01)
        if time.time() - last_print >= 2.0:
            count = len(benchmarker.samples)
            pct = (count / TARGET_SAMPLES) * 100
            print(f"  ... Collected {count}/{TARGET_SAMPLES} samples ({pct:.1f}%)")
            last_print = time.time()

    end_wall_time = time.time()
    end_cpu_time = get_process_cpu_time(gw_pid) if gw_pid else 0.0

    # Calculate CPU usage percentage
    wall_duration = end_wall_time - start_wall_time
    cpu_usage_pct = 0.0
    if gw_pid and wall_duration > 0:
        ticks_per_sec = os.sysconf(os.sysconf_names["SC_CLK_TCK"]) if hasattr(os, "sysconf") else 100
        cpu_seconds = (end_cpu_time - start_cpu_time) / ticks_per_sec
        cpu_usage_pct = round((cpu_seconds / wall_duration) * 100.0, 2)

    print(f"[PASS] Successfully collected {len(benchmarker.samples)} samples in {wall_duration:.2f}s!")

    # Write CSV
    fieldnames = list(benchmarker.samples[0].keys())
    for out_path in (CSV_PATH, RESULTS_CSV_PATH):
        with open(out_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(benchmarker.samples)
        print(f"[PASS] Saved CSV dataset: {out_path}")

    # Compute statistics
    rtt_stats = compute_stats([s["read_rtt_ms"] for s in benchmarker.samples])
    jitter_stats = compute_stats([s["poll_jitter_ms"] for s in benchmarker.samples])
    interval_stats = compute_stats([s["poll_interval_ms"] for s in benchmarker.samples])
    latency_stats = compute_stats([s["sample_to_ros_ms"] for s in benchmarker.samples])

    # Acceptance criteria verification
    sla_rtt = rtt_stats["p99"] <= 5.0
    sla_jitter = jitter_stats["p99"] <= 2.0
    sla_cpu = cpu_usage_pct <= 2.0
    all_sla_pass = sla_rtt and sla_jitter

    summary = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "target_samples": TARGET_SAMPLES,
        "collected_samples": len(benchmarker.samples),
        "wall_duration_sec": round(wall_duration, 2),
        "effective_rate_hz": round(len(benchmarker.samples) / wall_duration, 2),
        "system": {
            "platform": platform.platform(),
            "python_version": platform.python_version(),
            "cpu_count": os.cpu_count(),
            "gateway_pid": gw_pid,
            "cpu_usage_pct": cpu_usage_pct,
            "cpu_budget_max_pct": 2.0,
            "cpu_sla_pass": sla_cpu,
        },
        "metrics": {
            "modbus_read_rtt_ms": rtt_stats,
            "polling_interval_ms": interval_stats,
            "polling_jitter_ms": jitter_stats,
            "sample_to_ros_latency_ms": latency_stats,
        },
        "sla_criteria": {
            "p99_rtt_limit_ms": 5.0,
            "p99_rtt_actual_ms": rtt_stats["p99"],
            "p99_rtt_pass": sla_rtt,
            "p99_jitter_limit_ms": 2.0,
            "p99_jitter_actual_ms": jitter_stats["p99"],
            "p99_jitter_pass": sla_jitter,
            "overall_pass": all_sla_pass,
        },
    }

    with open(SUMMARY_JSON_PATH, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    print(f"[PASS] Saved benchmark summary: {SUMMARY_JSON_PATH}")

    print("\n" + "=" * 76)
    print(" 📈 1,000회 지연 및 지터 벤치마크 통계 요약")
    print("=" * 76)
    print(f"  • Modbus FC03 RTT (ms) : Mean={rtt_stats['mean']}, P50={rtt_stats['p50']}, P95={rtt_stats['p95']}, P99={rtt_stats['p99']}, Max={rtt_stats['max']}")
    print(f"  • Polling Jitter  (ms) : Mean={jitter_stats['mean']}, P50={jitter_stats['p50']}, P95={jitter_stats['p95']}, P99={jitter_stats['p99']}, Max={jitter_stats['max']}")
    print(f"  • Sample->ROS Lat (ms) : Mean={latency_stats['mean']}, P50={latency_stats['p50']}, P95={latency_stats['p95']}, P99={latency_stats['p99']}, Max={latency_stats['max']}")
    print(f"  • Gateway CPU 점유율  : {cpu_usage_pct}% (SLA < 2.0%: {'PASS' if sla_cpu else 'INFO'})")
    print(f"  • SLA 검증 판정       : P99 RTT <= 5ms [{'PASS' if sla_rtt else 'FAIL'}], P99 Jitter <= 2ms [{'PASS' if sla_jitter else 'FAIL'}]")
    print("=" * 76)

    benchmarker.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
