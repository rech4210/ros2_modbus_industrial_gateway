"""HMI SSOT Constants and Definitions.

Conforms strictly to spec_production.md Section 2.3, 4.1, mock_plc/constants.py,
and include/ros2_modbus_gateway/register_map.hpp & types.hpp.
Zero ROS or C++ binary dependencies for high OS portability.
"""

from enum import IntEnum
from typing import Dict

try:
    from mock_plc.constants import (
        HOLDING_REGISTER_COUNT,
        HR_HEARTBEAT,
        HR_STATUS_FLAGS,
        HR_SENSOR_RAW,
        HR_SETPOINT_RAW,
        HR_FAULT_CODE,
        HR_APPLIED_COMMAND_COUNTER,
        STATUS_BIT_RUN_REQUESTED,
        STATUS_BIT_READY,
        STATUS_BIT_RUNNING,
        STATUS_BIT_PHYSICAL_ESTOP,
        STATUS_BIT_RESET_REQUESTED,
        STATUS_BIT_ALARM_TRIPPED,
        STATUS_BIT_RESERVED_MASK,
        COIL_COUNT,
        COIL_RUN_REQUESTED,
        COIL_RESET_REQUESTED,
        COIL_VALUE_ON,
        COIL_VALUE_OFF,
        SENSOR_RAW_MIN,
        SENSOR_RAW_MAX,
        SETPOINT_RAW_MIN,
        SETPOINT_RAW_MAX,
        COUNTER_MODULO,
        FAULT_CODE_NONE,
        FAULT_CODE_PROCESS,
        FAULT_CODE_ESTOP,
        CMD_START,
        CMD_STOP,
        CMD_RESET,
        CMD_SET_SETPOINT,
        ERR_OK,
        ERR_INVALID_ARGUMENT,
        ERR_INVALID_CONFIG,
        ERR_NOT_READY,
        ERR_BUSY,
        ERR_ALARM_ACTIVE,
        ERR_INTERLOCK_ACTIVE,
        ERR_CONNECT_FAILED,
        ERR_IO_TIMEOUT,
        ERR_CONNECTION_LOST,
        ERR_PROTOCOL_ERROR,
        ERR_MODBUS_EXCEPTION,
        ERR_HEARTBEAT_STALE,
        ERR_BULK_READ_MISMATCH,
        ERR_COMMAND_TIMEOUT,
        ERR_COMMAND_OUTCOME_UNKNOWN,
        ERR_GENERATION_EXPIRED,
        ERR_SHUTTING_DOWN,
        ERR_INTERNAL_ERROR,
        ERR_PLC_FAULT,
        ERR_RESPONSE_DELIVERY_FAILED,
        ERR_COMMAND_CONFLICT,
    )
except ImportError:
    # Standalone mirror fallback
    HOLDING_REGISTER_COUNT = 6
    HR_HEARTBEAT = 0
    HR_STATUS_FLAGS = 1
    HR_SENSOR_RAW = 2
    HR_SETPOINT_RAW = 3
    HR_FAULT_CODE = 4
    HR_APPLIED_COMMAND_COUNTER = 5

    STATUS_BIT_RUN_REQUESTED = 0x0001
    STATUS_BIT_READY = 0x0002
    STATUS_BIT_RUNNING = 0x0004
    STATUS_BIT_PHYSICAL_ESTOP = 0x0008
    STATUS_BIT_RESET_REQUESTED = 0x0010
    STATUS_BIT_ALARM_TRIPPED = 0x0020
    STATUS_BIT_RESERVED_MASK = 0xFFC0

    COIL_COUNT = 2
    COIL_RUN_REQUESTED = 0
    COIL_RESET_REQUESTED = 1
    COIL_VALUE_ON = 0xFF00
    COIL_VALUE_OFF = 0x0000

    SENSOR_RAW_MIN = 0
    SENSOR_RAW_MAX = 1000
    SETPOINT_RAW_MIN = 0
    SETPOINT_RAW_MAX = 1000
    COUNTER_MODULO = 65536

    FAULT_CODE_NONE = 0
    FAULT_CODE_PROCESS = 1
    FAULT_CODE_ESTOP = 2

    CMD_START = 1
    CMD_STOP = 2
    CMD_RESET = 3
    CMD_SET_SETPOINT = 4

    ERR_OK = 0
    ERR_INVALID_ARGUMENT = 1
    ERR_INVALID_CONFIG = 2
    ERR_NOT_READY = 3
    ERR_BUSY = 4
    ERR_ALARM_ACTIVE = 5
    ERR_INTERLOCK_ACTIVE = 6
    ERR_CONNECT_FAILED = 10
    ERR_IO_TIMEOUT = 11
    ERR_CONNECTION_LOST = 12
    ERR_PROTOCOL_ERROR = 13
    ERR_MODBUS_EXCEPTION = 14
    ERR_HEARTBEAT_STALE = 15
    ERR_BULK_READ_MISMATCH = 16
    ERR_COMMAND_TIMEOUT = 17
    ERR_COMMAND_OUTCOME_UNKNOWN = 18
    ERR_GENERATION_EXPIRED = 19
    ERR_SHUTTING_DOWN = 20
    ERR_INTERNAL_ERROR = 21
    ERR_PLC_FAULT = 22
    ERR_RESPONSE_DELIVERY_FAILED = 23
    ERR_COMMAND_CONFLICT = 24


class LinkState(IntEnum):
    DISCONNECTED = 0
    CONNECTING = 1
    OPERATIONAL = 2
    COMM_FAULT = 3
    STOPPING = 4


class CommandOutcome(IntEnum):
    NOT_SENT = 0
    CONFIRMED = 1
    UNKNOWN = 2
    REJECTED = 3


class AlarmCause(IntEnum):
    NONE = 0
    STARTUP = 1
    COMM_TIMEOUT = 2
    HEARTBEAT_STALE = 3
    PLC_INTERLOCK = 4
    MANUAL_STOP = 5
    SHUTDOWN = 6
    INTERNAL = 7


ERROR_CODE_MESSAGES: Dict[int, str] = {
    ERR_OK: "OK",
    ERR_INVALID_ARGUMENT: "Invalid Argument",
    ERR_INVALID_CONFIG: "Invalid Configuration",
    ERR_NOT_READY: "System Not Ready",
    ERR_BUSY: "System Busy (Command Pending)",
    ERR_ALARM_ACTIVE: "Safety Alarm Latched Active",
    ERR_INTERLOCK_ACTIVE: "Safety Interlock Active (E-Stop / PLC Fault)",
    ERR_CONNECT_FAILED: "TCP Connect Failed",
    ERR_IO_TIMEOUT: "Modbus I/O Timeout (>25ms SLA)",
    ERR_CONNECTION_LOST: "TCP Connection Lost",
    ERR_PROTOCOL_ERROR: "Modbus Protocol Framing Error",
    ERR_MODBUS_EXCEPTION: "PLC Returned Modbus Exception",
    ERR_HEARTBEAT_STALE: "PLC Heartbeat Stopped Progressing",
    ERR_BULK_READ_MISMATCH: "Bulk Read Length Mismatch",
    ERR_COMMAND_TIMEOUT: "Command Confirmation Timeout",
    ERR_COMMAND_OUTCOME_UNKNOWN: "Command Outcome Unknown",
    ERR_GENERATION_EXPIRED: "Gateway Generation Expired",
    ERR_SHUTTING_DOWN: "Gateway Node Shutting Down",
    ERR_INTERNAL_ERROR: "Internal Gateway Error",
    ERR_PLC_FAULT: "PLC Internal Process Fault Active",
    ERR_RESPONSE_DELIVERY_FAILED: "Response Delivery Failed",
    ERR_COMMAND_CONFLICT: "Concurrent Command Conflict",
}

ALARM_CAUSE_MESSAGES: Dict[int, str] = {
    AlarmCause.NONE: "None",
    AlarmCause.STARTUP: "Initial Startup Latch",
    AlarmCause.COMM_TIMEOUT: "Modbus Communication Timeout (>3 consecutive failures)",
    AlarmCause.HEARTBEAT_STALE: "PLC Heartbeat Stale / Scan Frozen",
    AlarmCause.PLC_INTERLOCK: "Physical E-Stop or PLC Hardware Interlock Tripped",
    AlarmCause.MANUAL_STOP: "Manual Operator Stop",
    AlarmCause.SHUTDOWN: "System Shutdown",
    AlarmCause.INTERNAL: "Internal Driver Fault",
}
