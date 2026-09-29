"""Industrial PLC & Modbus Protocol Constants (Python SSOT).

Conforms strictly to spec_production.md Section 2.3 and mirrors
include/ros2_modbus_gateway/register_map.hpp & types.hpp.
Zero ROS or C++ binary dependencies.
"""

# ==============================================================================
# Holding Register Layout (0-based PDU Offset / Wire Address)
# ==============================================================================
HOLDING_REGISTER_COUNT: int = 6
HR_HEARTBEAT: int = 0
HR_STATUS_FLAGS: int = 1
HR_SENSOR_RAW: int = 2
HR_SETPOINT_RAW: int = 3
HR_FAULT_CODE: int = 4
HR_APPLIED_COMMAND_COUNTER: int = 5

# ==============================================================================
# Status Flags Bitmasks (HR Offset 1)
# ==============================================================================
STATUS_BIT_RUN_REQUESTED: int = 0x0001  # Bit 0: Operator run command requested
STATUS_BIT_READY: int = 0x0002          # Bit 1: Normal state, no estop/fault
STATUS_BIT_RUNNING: int = 0x0004        # Bit 2: Actively running
STATUS_BIT_PHYSICAL_ESTOP: int = 0x0008 # Bit 3: Hardware E-Stop asserted
STATUS_BIT_RESET_REQUESTED: int = 0x0010# Bit 4: Reset request pending
STATUS_BIT_ALARM_TRIPPED: int = 0x0020  # Bit 5: Fault code != 0
STATUS_BIT_RESERVED_MASK: int = 0xFFC0  # Bits 6~15: Reserved (must be 0)

# ==============================================================================
# Control Coils (0-based PDU Offset)
# ==============================================================================
COIL_COUNT: int = 2
COIL_RUN_REQUESTED: int = 0
COIL_RESET_REQUESTED: int = 1

# Modbus FC05 Standard Coil Values
COIL_VALUE_ON: int = 0xFF00
COIL_VALUE_OFF: int = 0x0000

# ==============================================================================
# Engineering Value Ranges & Modulo
# ==============================================================================
SENSOR_RAW_MIN: int = 0
SENSOR_RAW_MAX: int = 1000
SETPOINT_RAW_MIN: int = 0
SETPOINT_RAW_MAX: int = 1000
COUNTER_MODULO: int = 65536

# ==============================================================================
# PLC Fault Codes
# ==============================================================================
FAULT_CODE_NONE: int = 0
FAULT_CODE_PROCESS: int = 1
FAULT_CODE_ESTOP: int = 2

# ==============================================================================
# Gateway Control Commands (TriggerCommand.srv / types.hpp Command enum)
# ==============================================================================
CMD_START: int = 1
CMD_STOP: int = 2
CMD_RESET: int = 3
CMD_SET_SETPOINT: int = 4

# ==============================================================================
# Gateway Error Codes (spec_production.md Section 4.1 / types.hpp ErrorCode)
# ==============================================================================
ERR_OK: int = 0
ERR_INVALID_ARGUMENT: int = 1
ERR_INVALID_CONFIG: int = 2
ERR_NOT_READY: int = 3
ERR_BUSY: int = 4
ERR_ALARM_ACTIVE: int = 5
ERR_INTERLOCK_ACTIVE: int = 6
ERR_CONNECT_FAILED: int = 10
ERR_IO_TIMEOUT: int = 11
ERR_CONNECTION_LOST: int = 12
ERR_PROTOCOL_ERROR: int = 13
ERR_MODBUS_EXCEPTION: int = 14
ERR_HEARTBEAT_STALE: int = 15
ERR_BULK_READ_MISMATCH: int = 16
ERR_COMMAND_TIMEOUT: int = 17
ERR_COMMAND_OUTCOME_UNKNOWN: int = 18
ERR_GENERATION_EXPIRED: int = 19
ERR_SHUTTING_DOWN: int = 20
ERR_INTERNAL_ERROR: int = 21
ERR_PLC_FAULT: int = 22
ERR_RESPONSE_DELIVERY_FAILED: int = 23
ERR_COMMAND_CONFLICT: int = 24
