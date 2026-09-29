"""Mock PLC DataStore and 10ms Scan Logic implementation.

Conforms to spec_production.md Section 2.3.
Uses mock_plc.constants as Single Source of Truth (SSOT).
"""

from typing import Any, Dict, List, Tuple

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
    COIL_COUNT,
    COIL_RUN_REQUESTED,
    COIL_RESET_REQUESTED,
    SETPOINT_RAW_MIN,
    SETPOINT_RAW_MAX,
    COUNTER_MODULO,
    FAULT_CODE_NONE,
    FAULT_CODE_PROCESS,
    FAULT_CODE_ESTOP,
)


class PlcDataStore:
    """Simulates 6 Holding Registers and 2 Coils for industrial PLC."""

    def __init__(self) -> None:
        # Internal states
        self.heartbeat: int = 0
        self.run_requested: bool = False
        self.ready: bool = True
        self.running: bool = False
        self.physical_estop: bool = False
        self.reset_requested: bool = False
        self.alarm_tripped: bool = False
        self.sensor_raw: int = 0
        self.setpoint_raw: int = 0
        self.fault_code: int = FAULT_CODE_NONE
        self.applied_command_counter: int = 0

        self._process_fault: bool = False
        self._write_queue: List[Tuple[str, int, Any]] = []

        # Initial flags computation
        self._update_status_flags()

    def _update_status_flags(self) -> None:
        self.alarm_tripped = (self.fault_code != FAULT_CODE_NONE)
        flags = 0
        if self.run_requested:
            flags |= STATUS_BIT_RUN_REQUESTED
        if self.ready:
            flags |= STATUS_BIT_READY
        if self.running:
            flags |= STATUS_BIT_RUNNING
        if self.physical_estop:
            flags |= STATUS_BIT_PHYSICAL_ESTOP
        if self.reset_requested:
            flags |= STATUS_BIT_RESET_REQUESTED
        if self.alarm_tripped:
            flags |= STATUS_BIT_ALARM_TRIPPED
        self.status_flags = flags

    def read_holding(self, address: int, count: int) -> List[int]:
        """Read 6 Holding Registers (PDU Offset 0~5)."""
        if address < 0 or count <= 0 or (address + count) > HOLDING_REGISTER_COUNT:
            raise ValueError(f"Invalid holding register range: address={address}, count={count}")

        registers = [
            self.heartbeat,
            self.status_flags,
            self.sensor_raw,
            self.setpoint_raw,
            self.fault_code,
            self.applied_command_counter,
        ]
        return registers[address : address + count]

    def write_coil(self, address: int, value: bool) -> bool:
        """Write to control coils (Offset 0: run_requested, Offset 1: reset_requested)."""
        if address in (COIL_RUN_REQUESTED, COIL_RESET_REQUESTED):
            self._write_queue.append(("coil", address, bool(value)))
            return True
        return False

    def write_register(self, address: int, value: int) -> bool:
        """Write to Holding Register (Offset 3: setpoint_raw, 0~1000)."""
        if address == HR_SETPOINT_RAW:
            if SETPOINT_RAW_MIN <= value <= SETPOINT_RAW_MAX:
                self._write_queue.append(("register", address, int(value)))
                return True
        return False

    def scan(self, now_ns: int = 0) -> None:
        """Execute 10ms PLC logic scan."""
        # 1. 수신된 쓰기 요청(START/STOP/RESET/SET_SETPOINT)을 큐에서 꺼내 반영한다.
        while self._write_queue:
            cmd_type, addr, val = self._write_queue.pop(0)
            if cmd_type == "coil":
                if addr == COIL_RUN_REQUESTED:
                    val_bool = bool(val)
                    if val_bool:
                        # START: ready 상태이고 결함 없을 때만 성공 반영
                        if not self.physical_estop and self.fault_code == FAULT_CODE_NONE:
                            self.run_requested = True
                            self.applied_command_counter = (self.applied_command_counter + 1) % COUNTER_MODULO
                        else:
                            self.run_requested = False
                    else:
                        # STOP: 항상 성공 반영
                        self.run_requested = False
                        self.applied_command_counter = (self.applied_command_counter + 1) % COUNTER_MODULO
                elif addr == COIL_RESET_REQUESTED:
                    self.reset_requested = bool(val)
            elif cmd_type == "register":
                if addr == HR_SETPOINT_RAW and SETPOINT_RAW_MIN <= val <= SETPOINT_RAW_MAX:
                    self.setpoint_raw = int(val)
                    self.applied_command_counter = (self.applied_command_counter + 1) % COUNTER_MODULO

        # 2. reset_requested가 1이면 physical_estop == 0일 때 fault_code를 0으로 리셋하고 reset_requested를 0으로 소비한다.
        if self.reset_requested:
            if not self.physical_estop and not self._process_fault:
                self.fault_code = FAULT_CODE_NONE
                self.reset_requested = False
                # 7. 제어 쓰기를 성공적으로 반영할 때마다 applied_command_counter 1 증가
                self.applied_command_counter = (self.applied_command_counter + 1) % COUNTER_MODULO

        # 3. physical_estop == 1이면 fault_code = 2, ready = 0, running = 0, run_requested = 0.
        if self.physical_estop:
            self.fault_code = FAULT_CODE_ESTOP
            self.ready = False
            self.running = False
            self.run_requested = False
        elif self.fault_code != FAULT_CODE_NONE:
            # 4. fault_code != 0이면 ready = 0, running = 0.
            self.ready = False
            self.running = False
        else:
            # 5. 정상이면 ready = 1, running = run_requested.
            self.ready = True
            self.running = self.run_requested

        # 6. running == 1일 때 sensor_raw = setpoint_raw, 정지 중이면 sensor_raw = 0.
        if self.running:
            self.sensor_raw = self.setpoint_raw
        else:
            self.sensor_raw = 0

        # 8. 스캔 루프 종료 시 heartbeat = (heartbeat + 1) % COUNTER_MODULO.
        self.heartbeat = (self.heartbeat + 1) % COUNTER_MODULO

        # 상태 비트필드 갱신
        self._update_status_flags()

    def set_inputs(self, physical_estop: bool, process_fault: bool) -> None:
        """Manipulate physical E-Stop and process fault inputs."""
        self.physical_estop = bool(physical_estop)
        self._process_fault = bool(process_fault)
        if self.physical_estop:
            self.fault_code = FAULT_CODE_ESTOP
            self.ready = False
            self.running = False
            self.run_requested = False
        elif self._process_fault:
            if self.fault_code == FAULT_CODE_NONE:
                self.fault_code = FAULT_CODE_PROCESS
                self.ready = False
                self.running = False
        self._update_status_flags()

    def get_snapshot(self) -> Dict[str, Any]:
        """Return diagnostic snapshot of all registers and states."""
        return {
            "heartbeat": self.heartbeat,
            "status_flags": self.status_flags,
            "run_requested": self.run_requested,
            "ready": self.ready,
            "running": self.running,
            "physical_estop": self.physical_estop,
            "reset_requested": self.reset_requested,
            "alarm_tripped": self.alarm_tripped,
            "sensor_raw": self.sensor_raw,
            "setpoint_raw": self.setpoint_raw,
            "fault_code": self.fault_code,
            "applied_command_counter": self.applied_command_counter,
        }
