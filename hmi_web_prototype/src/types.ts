/**
 * HMI TypeScript SSOT Types & Interfaces.
 * Conforms to spec_production.md, mock_plc/constants.py, and types.hpp.
 */

// SSOT Commands (Mirroring types.hpp Command & mock_plc.constants)
export const CMD_START = 1;
export const CMD_STOP = 2;
export const CMD_RESET = 3;
export const CMD_SET_SETPOINT = 4;

// SSOT Error Codes
export const ERR_OK = 0;
export const ERR_INVALID_ARGUMENT = 1;
export const ERR_INVALID_CONFIG = 2;
export const ERR_NOT_READY = 3;
export const ERR_BUSY = 4;
export const ERR_ALARM_ACTIVE = 5;
export const ERR_INTERLOCK_ACTIVE = 6;
export const ERR_CONNECT_FAILED = 10;
export const ERR_IO_TIMEOUT = 11;
export const ERR_CONNECTION_LOST = 12;
export const ERR_PROTOCOL_ERROR = 13;
export const ERR_MODBUS_EXCEPTION = 14;
export const ERR_HEARTBEAT_STALE = 15;
export const ERR_BULK_READ_MISMATCH = 16;
export const ERR_COMMAND_TIMEOUT = 17;
export const ERR_COMMAND_OUTCOME_UNKNOWN = 18;
export const ERR_PLC_FAULT = 22;

// SSOT Bitmasks
export const STATUS_BIT_RUN_REQUESTED = 0x0001;
export const STATUS_BIT_READY = 0x0002;
export const STATUS_BIT_RUNNING = 0x0004;
export const STATUS_BIT_PHYSICAL_ESTOP = 0x0008;
export const STATUS_BIT_RESET_REQUESTED = 0x0010;
export const STATUS_BIT_ALARM_TRIPPED = 0x0020;

export enum LinkState {
  DISCONNECTED = 0,
  CONNECTING = 1,
  OPERATIONAL = 2,
  COMM_FAULT = 3,
  STOPPING = 4,
}

export enum AlarmCause {
  NONE = 0,
  STARTUP = 1,
  COMM_TIMEOUT = 2,
  HEARTBEAT_STALE = 3,
  PLC_INTERLOCK = 4,
  MANUAL_STOP = 5,
  SHUTDOWN = 6,
  INTERNAL = 7,
}

export enum CommandOutcome {
  NOT_SENT = 0,
  CONFIRMED = 1,
  UNKNOWN = 2,
  REJECTED = 3,
}

export interface PlcState {
  station_id: number;
  publish_sequence: number;
  sample_sequence: number;
  generation: number;
  link_state: number;
  has_sample: boolean;
  data_valid: boolean;
  alarm_active: boolean;
  heartbeat: number;
  status_flags: number;
  run_requested: boolean;
  ready: boolean;
  running: boolean;
  physical_estop: boolean;
  reset_requested: boolean;
  alarm_tripped: boolean;
  sensor_raw: number;
  setpoint_raw: number;
  fault_code: number;
  applied_command_counter: number;
  read_rtt_ns: number;
  poll_jitter_ns: number;
  consecutive_failures: number;
  io_error_count: number;
  error_code: number;
}

export interface SafetyAlarm {
  station_id: number;
  event_sequence: number;
  active: boolean;
  cause: number;
  error_code: number;
  generation: number;
  detected_ns: number;
  published_ns: number;
}

export interface PresentationState {
  statusText: string;
  statusColor: string;
  statusBadgeBg: string;
  stationId: number;
  sensorVal: number;
  sensorPct: number;
  setpointVal: number;
  isInRange: boolean;
  rttMs: number;
  jitterMs: number;
  heartbeat: number;
  isAlarm: boolean;
  alarmCause: number;
  alarmCauseText: string;
  errorCode: number;
  errorCodeText: string;
  canStart: boolean;
  canStop: boolean;
  canClear: boolean;
  canSetSetpoint: boolean;
  alarmGuideMsg: string;
  physicalEstop: boolean;
  linkState: number;
  linkStateText: string;
  ioErrorCount: number;
  consecutiveFailures: number;
  busFreqHz: number;
}

export type FaultMode = 'NORMAL' | 'DROP' | 'DELAY' | 'DISCONNECT' | 'FREEZE';

export interface CommandResult {
  success: boolean;
  command_id?: number;
  outcome?: number;
  error_code?: number;
  message?: string;
  elapsed_ms?: number;
}
