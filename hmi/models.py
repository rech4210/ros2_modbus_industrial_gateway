"""Data models for HMI Web/REST/WebSocket APIs.

Strictly decoupled from ROS 2 C++ internals while maintaining 100% field parity
with PlcState.msg, SafetyAlarm.msg, TriggerCommand.srv, and ClearFault.srv.
"""

from typing import Any, Dict, List, Optional
from pydantic import BaseModel, Field


class PlcStateModel(BaseModel):
    """Mirror of msg/PlcState.msg."""
    station_id: int = 1
    publish_sequence: int = 0
    sample_sequence: int = 0
    generation: int = 1
    link_state: int = 2  # 0:DISCONNECTED, 1:CONNECTING, 2:OPERATIONAL, 3:COMM_FAULT, 4:STOPPING
    has_sample: bool = True
    data_valid: bool = True
    alarm_active: bool = False

    # Holding Registers
    heartbeat: int = 0
    status_flags: int = 0x0002
    run_requested: bool = False
    ready: bool = True
    running: bool = False
    physical_estop: bool = False
    reset_requested: bool = False
    alarm_tripped: bool = False
    sensor_raw: int = 500
    setpoint_raw: int = 500
    fault_code: int = 0
    applied_command_counter: int = 0

    # Diagnostic & QoS metrics
    sample_age_ns: int = 0
    heartbeat_age_ns: int = 0
    read_rtt_ns: int = 420000       # default ~0.42ms
    poll_jitter_ns: int = 12000     # default ~0.012ms
    poll_attempt_count: int = 0
    io_error_count: int = 0
    consecutive_failures: int = 0
    error_code: int = 0


class SafetyAlarmModel(BaseModel):
    """Mirror of msg/SafetyAlarm.msg."""
    station_id: int = 1
    event_sequence: int = 0
    active: bool = False
    cause: int = 0  # 0:NONE, 1:STARTUP, 2:COMM_TIMEOUT, 3:HEARTBEAT_STALE, 4:PLC_INTERLOCK, ...
    error_code: int = 0
    generation: int = 1
    detected_ns: int = 0
    published_ns: int = 0
    last_progress_ns: int = 0


class TriggerCommandRequest(BaseModel):
    """Request for /plc/trigger_command."""
    command: int = Field(..., description="1:START, 2:STOP, 3:RESET, 4:SET_SETPOINT")
    value: int = Field(default=0, description="Setpoint value (0~1000) when command=4")


class TriggerCommandResponse(BaseModel):
    """Response of /plc/trigger_command."""
    success: bool
    command_id: int = 0
    outcome: int = 0  # 0:NOT_SENT, 1:CONFIRMED, 2:UNKNOWN, 3:REJECTED
    error_code: int = 0
    system_errno: int = 0
    modbus_exception: int = 0
    message: str = ""
    generation: int = 1
    confirmed_sample_sequence: int = 0
    elapsed_ms: int = 0


class ClearFaultRequest(BaseModel):
    """Request for /plc/clear_fault."""
    force_clear: bool = False


class ClearFaultResponse(BaseModel):
    """Response of /plc/clear_fault."""
    success: bool
    error_code: int = 0
    message: str = ""
    elapsed_ms: int = 0


class FaultInjectionRequest(BaseModel):
    """Fault injection request for Test Bench (Tier 1 & Tier 2)."""
    mode: str = Field(default="NORMAL", description="NORMAL, DROP, DELAY, DISCONNECT, FREEZE, DROP_ONE")
    delay_ms: Optional[int] = Field(default=None, description="Latency in ms when mode=DELAY (1~1000)")
    physical_estop: Optional[bool] = Field(default=None, description="Hardware E-Stop toggle")
    process_fault: Optional[bool] = Field(default=None, description="PLC internal process fault toggle")


class PresentationState(BaseModel):
    """ISA-101 Decimated ViewModel state optimized for 10Hz UI rendering."""
    statusText: str = "READY"
    statusColor: str = "text-[#90CAF9]"
    statusBadgeBg: str = "bg-[#1565C0]/25 border-[#1E88E5]"
    stationId: int = 1
    sensorVal: int = 500
    sensorPct: float = 50.0
    setpointVal: int = 500
    isInRange: bool = True
    rttMs: float = 0.42
    jitterMs: float = 0.012
    heartbeat: int = 0
    isAlarm: bool = False
    alarmCause: int = 0
    alarmCauseText: str = "None"
    errorCode: int = 0
    errorCodeText: str = "OK"
    canStart: bool = True
    canStop: bool = False
    canClear: bool = False
    canSetSetpoint: bool = True
    alarmGuideMsg: str = ""
    physicalEstop: bool = False
    linkState: int = 2
    linkStateText: str = "OPERATIONAL"
    ioErrorCount: int = 0
    consecutiveFailures: int = 0
    busFreqHz: float = 50.0
