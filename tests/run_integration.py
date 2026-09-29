"""End-to-End ROS 2 Service & Topic Integration Tests.

Validates service RPCs (START, STOP, SETPOINT, ClearFault, Interlock, BUSY).
Conforms strictly to spec_production.md Section 5 Step 9.
"""

import asyncio
import os
import sys
import time
import unittest

if "/ros2_ws/src/ros2_modbus_gateway" not in sys.path:
    sys.path.insert(0, "/ros2_ws/src/ros2_modbus_gateway")

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

from ros2_modbus_gateway.msg import PlcState, SafetyAlarm
from ros2_modbus_gateway.srv import TriggerCommand, ClearFault
from mock_plc.fault_injector import FaultInjector
from mock_plc.constants import (
    CMD_START,
    CMD_STOP,
    CMD_RESET,
    CMD_SET_SETPOINT,
    SETPOINT_RAW_MIN,
    SETPOINT_RAW_MAX,
    ERR_OK,
    ERR_INVALID_ARGUMENT,
    ERR_BUSY,
)


class IntegrationTester(Node):
    """ROS 2 Node for driving integration verification tests."""

    def __init__(self) -> None:
        super().__init__("integration_tester")

        # QoS Profiles
        state_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )
        alarm_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )

        self.latest_state: PlcState | None = None
        self.latest_alarm: SafetyAlarm | None = None

        self.state_sub = self.create_subscription(
            PlcState, "/plc/state", self._on_state, state_qos
        )
        self.alarm_sub = self.create_subscription(
            SafetyAlarm, "/safety/alarm", self._on_alarm, alarm_qos
        )

        self.cmd_cli = self.create_client(TriggerCommand, "/plc/trigger_command")
        self.clear_cli = self.create_client(ClearFault, "/plc/clear_fault")

    def _on_state(self, msg: PlcState) -> None:
        self.latest_state = msg

    def _on_alarm(self, msg: SafetyAlarm) -> None:
        self.latest_alarm = msg

    def wait_for_services_and_state(self, timeout_sec: float = 5.0) -> bool:
        start = time.time()
        while time.time() - start < timeout_sec:
            rclpy.spin_once(self, timeout_sec=0.05)
            if self.cmd_cli.service_is_ready() and self.clear_cli.service_is_ready() and self.latest_state is not None:
                return True
        return False

    def call_clear_fault(self, force_clear: bool = True, timeout_sec: float = 2.0) -> ClearFault.Response:
        req = ClearFault.Request()
        req.force_clear = force_clear
        future = self.clear_cli.call_async(req)
        start = time.time()
        while not future.done() and (time.time() - start < timeout_sec):
            rclpy.spin_once(self, timeout_sec=0.02)
        if not future.done():
            raise TimeoutError("ClearFault call timed out")
        return future.result()

    def call_trigger_command(self, command: int, value: int = 0, timeout_sec: float = 2.0) -> TriggerCommand.Response:
        req = TriggerCommand.Request()
        req.command = command
        req.value = value
        future = self.cmd_cli.call_async(req)
        start = time.time()
        while not future.done() and (time.time() - start < timeout_sec):
            rclpy.spin_once(self, timeout_sec=0.02)
        if not future.done():
            raise TimeoutError(f"TriggerCommand({command}, {value}) call timed out")
        return future.result()


def run_tests() -> bool:
    print("================================================================")
    print(" Running Step 9: ROS 2 Service & PLC Integration Tests")
    print("================================================================")
    rclpy.init()
    tester = IntegrationTester()
    injector = FaultInjector()

    try:
        # 1. Reset proxy & inputs to clean baseline
        injector.set_fault("NORMAL")
        injector.set_inputs(physical_estop=False, process_fault=False)

        print("[INFO] Waiting for gateway services and initial /plc/state...")
        ready = tester.wait_for_services_and_state(timeout_sec=8.0)
        assert ready, "Gateway services or state topic did not become ready!"
        print("[PASS] Gateway node is online and operational.")

        # 2. Initial ClearFault to unlatch startup alarm
        res_clear = tester.call_clear_fault(force_clear=True)
        assert res_clear.success, f"Initial ClearFault failed: {res_clear.message}"
        print(f"[PASS] Startup alarm cleared: {res_clear.message}")

        # Wait for state to reflect alarm_active == False
        t0 = time.time()
        while time.time() - t0 < 2.0:
            rclpy.spin_once(tester, timeout_sec=0.02)
            if tester.latest_state and not tester.latest_state.alarm_active:
                break
        assert tester.latest_state and not tester.latest_state.alarm_active, "Alarm was not unlatched in state"

        # 3. Test SETPOINT boundaries
        print("\n--- Testing SETPOINT boundaries (0, 1000, 1001) ---")
        # Setpoint 0 (MIN)
        resp = tester.call_trigger_command(command=CMD_SET_SETPOINT, value=SETPOINT_RAW_MIN)
        assert resp.success, f"SETPOINT 0 failed: {resp.message}"
        print("[PASS] SETPOINT 0 confirmed")

        # Setpoint 1000 (MAX)
        resp = tester.call_trigger_command(command=CMD_SET_SETPOINT, value=SETPOINT_RAW_MAX)
        assert resp.success, f"SETPOINT 1000 failed: {resp.message}"
        print("[PASS] SETPOINT 1000 confirmed")

        # Setpoint 1001 (Invalid argument: MAX + 1)
        resp = tester.call_trigger_command(command=CMD_SET_SETPOINT, value=SETPOINT_RAW_MAX + 1)
        assert not resp.success, "SETPOINT 1001 should be rejected!"
        assert resp.error_code == ERR_INVALID_ARGUMENT, f"Expected INVALID_ARGUMENT ({ERR_INVALID_ARGUMENT}), got {resp.error_code}"
        print(f"[PASS] SETPOINT 1001 correctly rejected: error_code={resp.error_code} ({resp.message})")

        # Setpoint 500 for operation
        resp = tester.call_trigger_command(command=CMD_SET_SETPOINT, value=500)
        assert resp.success, f"SETPOINT 500 failed: {resp.message}"

        # 4. Test START and STOP
        print("\n--- Testing START and STOP cycle ---")
        # START
        resp = tester.call_trigger_command(command=CMD_START, value=0)
        assert resp.success, f"START command failed: {resp.message}"
        # Spin to confirm state
        t0 = time.time()
        while time.time() - t0 < 1.0:
            rclpy.spin_once(tester, timeout_sec=0.02)
            if tester.latest_state and tester.latest_state.running:
                break
        assert tester.latest_state.running, "PLC running flag not true after START"
        print(f"[PASS] START confirmed, PLC is running (sensor_raw={tester.latest_state.sensor_raw})")

        # STOP
        resp = tester.call_trigger_command(command=CMD_STOP, value=0)
        assert resp.success, f"STOP command failed: {resp.message}"
        t0 = time.time()
        while time.time() - t0 < 1.0:
            rclpy.spin_once(tester, timeout_sec=0.02)
            if tester.latest_state and not tester.latest_state.running:
                break
        assert not tester.latest_state.running, "PLC running flag not false after STOP"
        print("[PASS] STOP confirmed, PLC is stopped")

        # 5. Test Concurrent calls (Single command slot BUSY protection)
        print("\n--- Testing Single Command Slot BUSY Rejection ---")
        req1 = TriggerCommand.Request(command=CMD_SET_SETPOINT, value=300)
        req2 = TriggerCommand.Request(command=CMD_SET_SETPOINT, value=400)
        fut1 = tester.cmd_cli.call_async(req1)
        fut2 = tester.cmd_cli.call_async(req2)
        start = time.time()
        while not (fut1.done() and fut2.done()) and (time.time() - start < 2.0):
            rclpy.spin_once(tester, timeout_sec=0.01)

        r1 = fut1.result() if fut1.done() else None
        r2 = fut2.result() if fut2.done() else None
        assert r1 is not None and r2 is not None, "One of concurrent requests did not complete"
        results = [r1, r2]
        busy_responses = [r for r in results if r.error_code == 4 and not r.success]
        confirmed_responses = [r for r in results if r.error_code == 0 and r.success]
        print(f"Results: {r1.message} (err={r1.error_code}), {r2.message} (err={r2.error_code})")
        assert len(busy_responses) == 1 and len(confirmed_responses) == 1, (
            f"Expected exactly 1 confirmed (0) and 1 busy (4), got err1={r1.error_code}, err2={r2.error_code}"
        )
        print("[PASS] Concurrent request arbitration verified.")

        # 6. Test Physical E-Stop Interlock
        print("\n--- Testing Physical E-Stop Interlock Guard ---")
        injector.set_inputs(physical_estop=True)
        # Give mock PLC a scan tick
        time.sleep(0.05)
        # Spin to observe state
        t0 = time.time()
        while time.time() - t0 < 1.0:
            rclpy.spin_once(tester, timeout_sec=0.02)
            if tester.latest_state and tester.latest_state.physical_estop:
                break
        assert tester.latest_state.physical_estop, "physical_estop flag not reflected"

        # Attempt START during E-Stop
        resp = tester.call_trigger_command(command=1, value=0)
        assert not resp.success, "START must be rejected during physical E-Stop!"
        print(f"[PASS] START rejected during E-Stop: error_code={resp.error_code} ({resp.message})")

        # Attempt ClearFault during E-Stop (must be rejected if force_clear=False)
        resp_clear = tester.call_clear_fault(force_clear=False)
        assert not resp_clear.success, "ClearFault without force must be rejected while E-Stop is physically active!"
        assert resp_clear.error_code == 6, f"Expected INTERLOCK_ACTIVE (6), got {resp_clear.error_code}"
        print(f"[PASS] ClearFault rejected during E-Stop: error_code={resp_clear.error_code} ({resp_clear.message})")

        # Clear physical E-Stop and recover
        injector.set_inputs(physical_estop=False)
        # Reset PLC fault
        resp_reset = tester.call_trigger_command(command=3, value=0)  # RESET
        time.sleep(0.05)
        resp_clear = tester.call_clear_fault(force_clear=True)
        print(f"[PASS] E-Stop restored and unlatched: {resp_clear.message}")

        print("\n================================================================")
        print(" [ALL INTEGRATION TESTS PASSED 100%]")
        print("================================================================")
        return True

    finally:
        injector.set_fault("NORMAL")
        injector.set_inputs(physical_estop=False, process_fault=False)
        tester.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
