"""Test Suite for HMI Subsystem (Models, Adapters, REST & WebSocket Endpoints).

Conforms to spec_production.md Section 2.3, 4.1, 4.2, and HMI decoupling rules.
"""

import asyncio
import json
import pytest
from fastapi.testclient import TestClient

from hmi.constants import (
    CMD_START,
    CMD_STOP,
    CMD_RESET,
    CMD_SET_SETPOINT,
    ERR_OK,
    ERR_INVALID_ARGUMENT,
    ERR_ALARM_ACTIVE,
    ERR_INTERLOCK_ACTIVE,
    STATUS_BIT_RUNNING,
    STATUS_BIT_READY,
    STATUS_BIT_PHYSICAL_ESTOP,
    CommandOutcome,
    LinkState,
    AlarmCause,
)
from hmi.models import (
    PlcStateModel,
    SafetyAlarmModel,
    TriggerCommandRequest,
    ClearFaultRequest,
    FaultInjectionRequest,
)
from hmi.adapters import MockHmiAdapter, Ros2HmiAdapter
from hmi.bridge_server import app


@pytest.mark.asyncio
async def test_mock_adapter_lifecycle_and_state():
    adapter = MockHmiAdapter(station_id=1)
    await adapter.start()
    await asyncio.sleep(0.05)  # Let simulation cycle run

    state = adapter.get_plc_state()
    assert state.station_id == 1
    assert state.ready is True
    assert state.running is False
    assert state.physical_estop is False
    assert state.heartbeat >= 1000

    pres = adapter.get_presentation_state()
    assert pres.statusText == "READY"
    assert pres.canStart is True
    assert pres.canStop is False
    assert pres.canClear is False
    assert pres.isInRange is True

    await adapter.stop()


@pytest.mark.asyncio
async def test_mock_adapter_commands_start_stop():
    adapter = MockHmiAdapter(station_id=1)
    await adapter.start()

    # 1. Start command
    res_start = await adapter.trigger_command(TriggerCommandRequest(command=CMD_START))
    assert res_start.success is True
    assert res_start.outcome == int(CommandOutcome.CONFIRMED)
    assert res_start.error_code == ERR_OK

    pres = adapter.get_presentation_state()
    assert pres.statusText == "RUNNING"
    assert pres.canStart is False
    assert pres.canStop is True

    # 2. Stop command
    res_stop = await adapter.trigger_command(TriggerCommandRequest(command=CMD_STOP))
    assert res_stop.success is True
    assert res_stop.outcome == int(CommandOutcome.CONFIRMED)

    pres = adapter.get_presentation_state()
    assert pres.statusText == "READY"
    assert pres.canStart is True
    assert pres.canStop is False

    await adapter.stop()


@pytest.mark.asyncio
async def test_mock_adapter_setpoint_boundaries():
    adapter = MockHmiAdapter(station_id=1)
    await adapter.start()

    # Valid boundary: 0
    res_zero = await adapter.trigger_command(TriggerCommandRequest(command=CMD_SET_SETPOINT, value=0))
    assert res_zero.success is True
    assert adapter.get_plc_state().setpoint_raw == 0

    # Valid boundary: 1000
    res_thousand = await adapter.trigger_command(TriggerCommandRequest(command=CMD_SET_SETPOINT, value=1000))
    assert res_thousand.success is True
    assert adapter.get_plc_state().setpoint_raw == 1000

    # Invalid boundary: -1 -> REJECTED
    res_neg = await adapter.trigger_command(TriggerCommandRequest(command=CMD_SET_SETPOINT, value=-1))
    assert res_neg.success is False
    assert res_neg.outcome == int(CommandOutcome.REJECTED)
    assert res_neg.error_code == ERR_INVALID_ARGUMENT

    # Invalid boundary: 1001 -> REJECTED
    res_overflow = await adapter.trigger_command(TriggerCommandRequest(command=CMD_SET_SETPOINT, value=1001))
    assert res_overflow.success is False
    assert res_overflow.outcome == int(CommandOutcome.REJECTED)
    assert res_overflow.error_code == ERR_INVALID_ARGUMENT

    await adapter.stop()


@pytest.mark.asyncio
async def test_mock_adapter_tier1_estop_interlock_and_clear_fault():
    adapter = MockHmiAdapter(station_id=1)
    await adapter.start()

    # 1. Inject Physical E-Stop
    await adapter.inject_fault(FaultInjectionRequest(physical_estop=True))
    state = adapter.get_plc_state()
    assert state.physical_estop is True
    assert state.alarm_active is True

    pres = adapter.get_presentation_state()
    assert pres.statusText == "PHYSICAL E-STOP"
    assert pres.canStart is False
    assert pres.canClear is False  # Hardware E-Stop locks reset!

    # 2. START command must be rejected with ERR_INTERLOCK_ACTIVE
    res_start = await adapter.trigger_command(TriggerCommandRequest(command=CMD_START))
    assert res_start.success is False
    assert res_start.error_code == ERR_INTERLOCK_ACTIVE

    # 3. ClearFault without release should be rejected
    res_clear_fail = await adapter.clear_fault(ClearFaultRequest(force_clear=False))
    assert res_clear_fail.success is False
    assert res_clear_fail.error_code == ERR_INTERLOCK_ACTIVE

    # 4. Release physical E-Stop
    await adapter.inject_fault(FaultInjectionRequest(physical_estop=False))
    pres_after_release = adapter.get_presentation_state()
    assert pres_after_release.isAlarm is True
    assert pres_after_release.canClear is True  # Now can clear software latch

    # 5. Clear fault
    res_clear_ok = await adapter.clear_fault(ClearFaultRequest(force_clear=False))
    assert res_clear_ok.success is True
    assert res_clear_ok.error_code == ERR_OK

    pres_final = adapter.get_presentation_state()
    assert pres_final.isAlarm is False
    assert pres_final.statusText == "READY"
    assert pres_final.canStart is True

    await adapter.stop()


@pytest.mark.asyncio
async def test_mock_adapter_tier2_scenarios():
    adapter = MockHmiAdapter(station_id=1)
    await adapter.start()

    # Test DROP scenario
    await adapter.inject_fault(FaultInjectionRequest(mode="DROP"))
    await asyncio.sleep(0.15)  # 5+ cycles -> failures >= 3
    state = adapter.get_plc_state()
    assert state.consecutive_failures >= 3
    assert state.alarm_active is True
    assert state.link_state == int(LinkState.COMM_FAULT)

    # Recover to NORMAL
    await adapter.inject_fault(FaultInjectionRequest(mode="NORMAL"))
    await adapter.clear_fault(ClearFaultRequest(force_clear=False))
    state = adapter.get_plc_state()
    assert state.consecutive_failures == 0
    assert state.alarm_active is False
    assert state.link_state == int(LinkState.OPERATIONAL)

    # Test FREEZE scenario
    await adapter.inject_fault(FaultInjectionRequest(mode="FREEZE"))
    await asyncio.sleep(0.15)
    state = adapter.get_plc_state()
    assert state.alarm_active is True

    # Recover
    await adapter.inject_fault(FaultInjectionRequest(mode="NORMAL"))
    await adapter.clear_fault(ClearFaultRequest())
    assert adapter.get_plc_state().alarm_active is False

    await adapter.stop()


def test_rest_api_endpoints():
    with TestClient(app) as client:
        # 1. Health check
        resp_health = client.get("/api/health")
        assert resp_health.status_code == 200
        data_health = resp_health.json()
        assert data_health["status"] == "ok"

        # 2. State fetch
        resp_state = client.get("/api/state")
        assert resp_state.status_code == 200
        data_state = resp_state.json()
        assert "presentation" in data_state
        assert "raw" in data_state
        assert "alarm" in data_state
        assert data_state["presentation"]["stationId"] == 1

        # 3. Trigger command: START
        resp_cmd = client.post("/api/trigger_command", json={"command": CMD_START, "value": 0})
        assert resp_cmd.status_code == 200
        data_cmd = resp_cmd.json()
        assert data_cmd["success"] is True
        assert data_cmd["outcome"] == int(CommandOutcome.CONFIRMED)

        # 4. Trigger command: STOP
        resp_stop = client.post("/api/trigger_command", json={"command": CMD_STOP, "value": 0})
        assert resp_stop.status_code == 200
        assert resp_stop.json()["success"] is True

        # 5. Setpoint endpoint
        resp_sp = client.post("/api/setpoint", json={"value": 550})
        assert resp_sp.status_code == 200
        assert resp_sp.json()["success"] is True

        # 6. Fault injection endpoint
        resp_fi = client.post("/api/fault_injection", json={"mode": "NORMAL", "physical_estop": False})
        assert resp_fi.status_code == 200
        assert resp_fi.json()["success"] is True

        # 7. Clear fault endpoint
        resp_cf = client.post("/api/clear_fault", json={"force_clear": False})
        assert resp_cf.status_code == 200
        assert resp_cf.json()["success"] is True


def test_websocket_telemetry_and_command():
    with TestClient(app) as client:
        with client.websocket_connect("/ws") as ws:
            # 1. First message is immediate initial state snapshot
            init_msg = json.loads(ws.receive_text())
            assert init_msg["type"] == "STATE_UPDATE"
            assert "presentation" in init_msg
            assert "raw" in init_msg

            # 2. Send ping
            ws.send_text(json.dumps({"type": "PING", "timestamp": 12345}))
            pong = json.loads(ws.receive_text())
            assert pong["type"] == "PONG"

            # 3. Send command via websocket
            ws.send_text(json.dumps({"type": "COMMAND", "command": CMD_START, "value": 0}))
            cmd_res = json.loads(ws.receive_text())
            assert cmd_res["type"] == "COMMAND_RESULT"
            assert cmd_res["result"]["success"] is True

            # 4. Clean stop
            ws.send_text(json.dumps({"type": "COMMAND", "command": CMD_STOP, "value": 0}))
            stop_res = json.loads(ws.receive_text())
            assert stop_res["type"] == "COMMAND_RESULT"
            assert stop_res["result"]["success"] is True


def test_static_frontend_serving():
    with TestClient(app) as client:
        resp = client.get("/")
        assert resp.status_code == 200
        assert "html" in resp.headers.get("content-type", "").lower()
        assert "ROS 2 Modbus Industrial Gateway HMI" in resp.text


def test_rest_api_boundary_and_interlock_edge_cases():
    with TestClient(app) as client:
        # Out-of-range setpoint via REST
        res_neg = client.post("/api/trigger_command", json={"command": CMD_SET_SETPOINT, "value": -10})
        assert res_neg.status_code == 200
        data_neg = res_neg.json()
        assert data_neg["success"] is False
        assert data_neg["error_code"] == ERR_INVALID_ARGUMENT

        res_over = client.post("/api/trigger_command", json={"command": CMD_SET_SETPOINT, "value": 2000})
        assert res_over.status_code == 200
        data_over = res_over.json()
        assert data_over["success"] is False
        assert data_over["error_code"] == ERR_INVALID_ARGUMENT

        # Inject physical E-Stop
        res_estop = client.post("/api/fault_injection", json={"physical_estop": True})
        assert res_estop.status_code == 200

        # Try to START while E-Stop is active -> Rejected with ERR_INTERLOCK_ACTIVE
        res_start_estop = client.post("/api/trigger_command", json={"command": CMD_START, "value": 0})
        assert res_start_estop.status_code == 200
        data_start_estop = res_start_estop.json()
        assert data_start_estop["success"] is False
        assert data_start_estop["error_code"] == ERR_INTERLOCK_ACTIVE

        # Try ClearFault while E-Stop is active without force -> Rejected
        res_cf_estop = client.post("/api/clear_fault", json={"force_clear": False})
        assert res_cf_estop.status_code == 200
        assert res_cf_estop.json()["success"] is False
        assert res_cf_estop.json()["error_code"] == ERR_INTERLOCK_ACTIVE

        # Release E-Stop
        res_rel = client.post("/api/fault_injection", json={"physical_estop": False})
        assert res_rel.status_code == 200

        # Now ClearFault succeeds
        res_cf_ok = client.post("/api/clear_fault", json={"force_clear": False})
        assert res_cf_ok.status_code == 200
        assert res_cf_ok.json()["success"] is True


@pytest.mark.asyncio
async def test_ros2_adapter_fallback_on_non_ros2_env():
    """Verify Ros2HmiAdapter gracefully falls back to MockHmiAdapter when ROS 2 is not installed/running."""
    adapter = Ros2HmiAdapter()
    await adapter.start()

    # State must be available (via mock fallback)
    state = adapter.get_plc_state()
    assert state.station_id == 1
    assert state.ready is True

    pres = adapter.get_presentation_state()
    assert pres.statusText == "READY"

    # Command execution must work via fallback
    res = await adapter.trigger_command(TriggerCommandRequest(command=CMD_START))
    assert res.success is True
    assert adapter.get_presentation_state().statusText == "RUNNING"

    # Stop command
    res_stop = await adapter.trigger_command(TriggerCommandRequest(command=CMD_STOP))
    assert res_stop.success is True

    # Clean shutdown
    await adapter.stop()


def test_websocket_rpc_request_id_and_rejections():
    """Verify WebSocket messages support request_id correlation and convey rejection errors."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws") as ws:
            # Consume initial STATE_UPDATE
            init = json.loads(ws.receive_text())
            assert init["type"] == "STATE_UPDATE"

            # 1. Send command with request_id = 42
            ws.send_text(json.dumps({
                "type": "COMMAND",
                "request_id": 42,
                "command": CMD_START,
                "value": 0,
            }))
            res_start = json.loads(ws.receive_text())
            assert res_start["type"] == "COMMAND_RESULT"
            assert res_start["request_id"] == 42
            assert res_start["result"]["success"] is True

            # 2. Out-of-range setpoint (-1) with request_id = 43
            ws.send_text(json.dumps({
                "type": "COMMAND",
                "request_id": 43,
                "command": CMD_SET_SETPOINT,
                "value": -1,
            }))
            res_boundary = json.loads(ws.receive_text())
            assert res_boundary["type"] == "COMMAND_RESULT"
            assert res_boundary["request_id"] == 43
            assert res_boundary["result"]["success"] is False
            assert res_boundary["result"]["error_code"] == ERR_INVALID_ARGUMENT

            # 3. Assert physical E-Stop via FAULT_INJECTION with request_id = 44
            ws.send_text(json.dumps({
                "type": "FAULT_INJECTION",
                "request_id": 44,
                "physical_estop": True,
            }))
            res_fi = json.loads(ws.receive_text())
            assert res_fi["type"] == "FAULT_INJECTION_RESULT"
            assert res_fi["request_id"] == 44
            assert res_fi["result"]["success"] is True

            # 4. Attempt ClearFault while E-Stop active with request_id = 45 -> Must be rejected
            ws.send_text(json.dumps({
                "type": "CLEAR_FAULT",
                "request_id": 45,
                "force_clear": False,
            }))
            res_cf = json.loads(ws.receive_text())
            assert res_cf["type"] == "CLEAR_FAULT_RESULT"
            assert res_cf["request_id"] == 45
            assert res_cf["result"]["success"] is False
            assert res_cf["result"]["error_code"] == ERR_INTERLOCK_ACTIVE

            # 5. Clean recovery
            ws.send_text(json.dumps({"type": "FAULT_INJECTION", "physical_estop": False}))
            json.loads(ws.receive_text())
            ws.send_text(json.dumps({"type": "CLEAR_FAULT", "force_clear": False}))
            res_recovered = json.loads(ws.receive_text())
            assert res_recovered["result"]["success"] is True


def test_locale_dictionary_integrity():
    """Verify that both TypeScript locale definitions (ko and en) contain identical top-level sections and alarmCauses."""
    import re
    from pathlib import Path

    ko_path = Path("hmi_web_prototype/src/locales/ko.ts")
    en_path = Path("hmi_web_prototype/src/locales/en.ts")
    types_path = Path("hmi_web_prototype/src/locales/types.ts")

    assert ko_path.exists(), "ko.ts locale file must exist"
    assert en_path.exists(), "en.ts locale file must exist"
    assert types_path.exists(), "types.ts locale file must exist"

    ko_content = ko_path.read_text(encoding="utf-8")
    en_content = en_path.read_text(encoding="utf-8")

    required_sections = [
        "header",
        "safetyBanner",
        "machineState",
        "alarmCauses",
        "process",
        "setpoint",
        "qos",
        "controls",
        "modal",
        "testBench",
        "systemArch",
    ]

    for sec in required_sections:
        assert f"{sec}:" in ko_content, f"Missing section '{sec}' in ko.ts"
        assert f"{sec}:" in en_content, f"Missing section '{sec}' in en.ts"

    # Verify all 8 alarm cause codes (none, startup, commTimeout, heartbeatStale, plcInterlock, manualStop, shutdown, internal)
    alarm_causes = ["none", "startup", "commTimeout", "heartbeatStale", "plcInterlock", "manualStop", "shutdown", "internal"]
    for cause in alarm_causes:
        assert f"{cause}:" in ko_content, f"Missing alarm cause '{cause}' in ko.ts"
        assert f"{cause}:" in en_content, f"Missing alarm cause '{cause}' in en.ts"


def test_multi_client_websocket_broadcast():
    """Verify multiple concurrent WebSocket connections receive state broadcast without crashing."""
    with TestClient(app) as client:
        with client.websocket_connect("/ws") as ws1, client.websocket_connect("/ws") as ws2:
            msg1 = json.loads(ws1.receive_text())
            msg2 = json.loads(ws2.receive_text())
            assert msg1["type"] == "STATE_UPDATE"
            assert msg2["type"] == "STATE_UPDATE"
            assert msg1["presentation"]["stationId"] == msg2["presentation"]["stationId"]


