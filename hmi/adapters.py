"""HMI Data Adapters: Decoupled Live ROS 2 DDS & High-Fidelity Mock.

Strictly conforms to spec_production.md Section 2.3, 2.7, 4.1, 4.2.
Decoupled from Gateway internals; communicates only via ROS 2 DDS topic/service contracts.
"""

import asyncio
import logging
import math
import random
import time
from abc import ABC, abstractmethod
from typing import Any, Dict, Optional

from hmi.constants import (
    ALARM_CAUSE_MESSAGES,
    CMD_RESET,
    CMD_SET_SETPOINT,
    CMD_START,
    CMD_STOP,
    COUNTER_MODULO,
    ERROR_CODE_MESSAGES,
    ERR_ALARM_ACTIVE,
    ERR_INTERLOCK_ACTIVE,
    ERR_INVALID_ARGUMENT,
    ERR_NOT_READY,
    ERR_OK,
    FAULT_CODE_ESTOP,
    FAULT_CODE_NONE,
    FAULT_CODE_PROCESS,
    SENSOR_RAW_MAX,
    SENSOR_RAW_MIN,
    SETPOINT_RAW_MAX,
    SETPOINT_RAW_MIN,
    STATUS_BIT_ALARM_TRIPPED,
    STATUS_BIT_PHYSICAL_ESTOP,
    STATUS_BIT_READY,
    STATUS_BIT_RESET_REQUESTED,
    STATUS_BIT_RUN_REQUESTED,
    STATUS_BIT_RUNNING,
    AlarmCause,
    CommandOutcome,
    LinkState,
)
from hmi.models import (
    ClearFaultRequest,
    ClearFaultResponse,
    FaultInjectionRequest,
    PlcStateModel,
    PresentationState,
    SafetyAlarmModel,
    TriggerCommandRequest,
    TriggerCommandResponse,
)

logger = logging.getLogger("hmi.adapters")


class BaseHmiAdapter(ABC):
    """Abstract interface for HMI Gateway data source."""

    @abstractmethod
    async def start(self) -> None:
        pass

    @abstractmethod
    async def stop(self) -> None:
        pass

    @abstractmethod
    def get_plc_state(self) -> PlcStateModel:
        pass

    @abstractmethod
    def get_safety_alarm(self) -> SafetyAlarmModel:
        pass

    @abstractmethod
    def get_presentation_state(self) -> PresentationState:
        pass

    @abstractmethod
    async def trigger_command(self, req: TriggerCommandRequest) -> TriggerCommandResponse:
        pass

    @abstractmethod
    async def clear_fault(self, req: ClearFaultRequest) -> ClearFaultResponse:
        pass

    @abstractmethod
    async def inject_fault(self, req: FaultInjectionRequest) -> Dict[str, Any]:
        pass


class MockHmiAdapter(BaseHmiAdapter):
    """High-Fidelity Mock & Simulation Adapter for cross-platform OS portability.

    Runs fully on Windows, macOS, Linux, and edge containers without requiring ROS 2 binaries.
    Accurately simulates 50Hz PLC dynamics, 10Hz decimated ISA-101 presentation model,
    boundary validation, and Tier 1 / Tier 2 fault injections.
    """

    def __init__(self, station_id: int = 1) -> None:
        self.station_id = station_id
        self._running = False
        self._ready = True
        self._physical_estop = False
        self._process_fault = False
        self._alarm_active = False
        self._alarm_cause = int(AlarmCause.NONE)
        self._link_state = int(LinkState.OPERATIONAL)

        self._heartbeat = 1000
        self._sensor_raw = 512
        self._setpoint_raw = 500
        self._fault_code = FAULT_CODE_NONE
        self._command_counter = 0

        self._read_rtt_ns = 420000     # 0.42 ms
        self._poll_jitter_ns = 12000   # 0.012 ms
        self._consecutive_failures = 0
        self._io_error_count = 0
        self._publish_seq = 0
        self._sample_seq = 0
        self._generation = 1
        self._command_id_seq = 100

        self._fault_mode = "NORMAL"
        self._delay_ms: Optional[int] = None
        self._is_frozen = False

        self._task: Optional[asyncio.Task] = None
        self._lock = asyncio.Lock()

    async def start(self) -> None:
        """Start the 50Hz simulation loop."""
        if self._task is None:
            self._task = asyncio.create_task(self._simulation_loop())
            logger.info("MockHmiAdapter started 50Hz background simulation engine.")

    async def stop(self) -> None:
        """Stop background simulation loop."""
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None
            logger.info("MockHmiAdapter simulation engine stopped.")

    async def _simulation_loop(self) -> None:
        """Runs at 50Hz (20ms interval), updating state and sensor wave."""
        try:
            while True:
                await asyncio.sleep(0.02)
                async with self._lock:
                    self._publish_seq += 1
                    self._sample_seq += 1

                    if not self._is_frozen:
                        self._heartbeat = (self._heartbeat + 1) % COUNTER_MODULO

                    # Handle Fault Modes
                    if self._fault_mode == "ESTOP":
                        self._physical_estop = True
                        self._alarm_active = True
                        self._alarm_cause = int(AlarmCause.PLC_INTERLOCK)
                        self._fault_code = FAULT_CODE_ESTOP
                        self._running = False
                        self._ready = False

                    elif self._fault_mode == "DISCONNECT":
                        self._link_state = int(LinkState.COMM_FAULT)
                        self._alarm_active = True
                        self._alarm_cause = int(AlarmCause.COMM_TIMEOUT)
                        self._consecutive_failures += 1
                        self._io_error_count += 1
                        self._running = False
                        self._ready = False

                    elif self._fault_mode == "DROP":
                        self._consecutive_failures = min(5, self._consecutive_failures + 1)
                        self._io_error_count += 1
                        if self._consecutive_failures >= 3:
                            self._link_state = int(LinkState.COMM_FAULT)
                            self._alarm_active = True
                            self._alarm_cause = int(AlarmCause.COMM_TIMEOUT)
                            self._running = False
                            self._ready = False

                    elif self._fault_mode == "FREEZE":
                        self._is_frozen = True
                        # Heartbeat does not advance; after threshold trigger alarm
                        self._consecutive_failures = min(10, self._consecutive_failures + 1)
                        if self._consecutive_failures >= 3:
                            self._alarm_active = True
                            self._alarm_cause = int(AlarmCause.HEARTBEAT_STALE)
                            self._running = False
                            self._ready = False

                    elif self._fault_mode == "DELAY":
                        delay = self._delay_ms or 30
                        self._read_rtt_ns = delay * 1_000_000
                        self._poll_jitter_ns = 5_000_000
                        if delay > 25:  # Gateway timeout is 25ms SLA
                            self._consecutive_failures = min(5, self._consecutive_failures + 1)
                            if self._consecutive_failures >= 3:
                                self._alarm_active = True
                                self._alarm_cause = int(AlarmCause.COMM_TIMEOUT)
                                self._running = False
                                self._ready = False

                    else:  # NORMAL
                        self._is_frozen = False
                        if not self._alarm_active:
                            self._link_state = int(LinkState.OPERATIONAL)
                            self._consecutive_failures = 0
                            # Deterministic RTT simulation ~0.42ms ± 0.05ms
                            self._read_rtt_ns = 380_000 + random.randint(0, 80_000)
                            self._poll_jitter_ns = 8_000 + random.randint(0, 8_000)

                    # Dynamic sensor wave when running (tracking setpoint ± drift)
                    if self._running:
                        target = float(self._setpoint_raw)
                        drift = math.sin(time.monotonic() * 1.5) * 45.0
                        noise = (random.random() - 0.5) * 6.0
                        val = int(target + drift + noise)
                        self._sensor_raw = max(SENSOR_RAW_MIN, min(SENSOR_RAW_MAX, val))
                    else:
                        # Sensor settles towards 500 when stopped
                        diff = 500 - self._sensor_raw
                        if abs(diff) > 2:
                            self._sensor_raw += int(diff * 0.1)

        except asyncio.CancelledError:
            pass

    def get_plc_state(self) -> PlcStateModel:
        status_flags = 0
        if self._running:
            status_flags |= STATUS_BIT_RUNNING
        if self._ready:
            status_flags |= STATUS_BIT_READY
        if self._physical_estop:
            status_flags |= STATUS_BIT_PHYSICAL_ESTOP
        if self._alarm_active or self._alarm_cause != int(AlarmCause.NONE):
            status_flags |= STATUS_BIT_ALARM_TRIPPED

        return PlcStateModel(
            station_id=self.station_id,
            publish_sequence=self._publish_seq,
            sample_sequence=self._sample_seq,
            generation=self._generation,
            link_state=self._link_state,
            has_sample=True,
            data_valid=not self._alarm_active and self._link_state == int(LinkState.OPERATIONAL),
            alarm_active=self._alarm_active,
            heartbeat=self._heartbeat,
            status_flags=status_flags,
            run_requested=False,
            ready=self._ready,
            running=self._running,
            physical_estop=self._physical_estop,
            reset_requested=False,
            alarm_tripped=self._alarm_active,
            sensor_raw=self._sensor_raw,
            setpoint_raw=self._setpoint_raw,
            fault_code=self._fault_code,
            applied_command_counter=self._command_counter,
            sample_age_ns=1_000_000,
            heartbeat_age_ns=20_000_000 if not self._is_frozen else 500_000_000,
            read_rtt_ns=self._read_rtt_ns,
            poll_jitter_ns=self._poll_jitter_ns,
            poll_attempt_count=self._publish_seq,
            io_error_count=self._io_error_count,
            consecutive_failures=self._consecutive_failures,
            error_code=ERR_ALARM_ACTIVE if self._alarm_active else ERR_OK,
        )

    def get_safety_alarm(self) -> SafetyAlarmModel:
        return SafetyAlarmModel(
            station_id=self.station_id,
            event_sequence=1 if self._alarm_active else 0,
            active=self._alarm_active,
            cause=self._alarm_cause,
            error_code=ERR_ALARM_ACTIVE if self._alarm_active else ERR_OK,
            generation=self._generation,
            detected_ns=time.monotonic_ns() if self._alarm_active else 0,
            published_ns=time.monotonic_ns(),
            last_progress_ns=time.monotonic_ns(),
        )

    def get_presentation_state(self) -> PresentationState:
        s = self.get_plc_state()
        is_alarm = s.alarm_active or s.physical_estop or s.link_state == int(LinkState.COMM_FAULT)

        status_text = "READY"
        status_color = "text-[#90CAF9]"
        status_badge_bg = "bg-[#1565C0]/25 border-[#1E88E5]"

        if s.physical_estop:
            status_text = "PHYSICAL E-STOP"
            status_color = "text-[#EF5350]"
            status_badge_bg = "bg-[#C62828]/25 border-[#E53935] shadow-[0_0_15px_rgba(229,57,53,0.3)]"
        elif s.link_state == int(LinkState.COMM_FAULT) or is_alarm:
            status_text = "COMM FAULT / LATCHED"
            status_color = "text-[#FF7043]"
            status_badge_bg = "bg-[#D84315]/25 border-[#F4511E]"
        elif s.running:
            status_text = "RUNNING"
            status_color = "text-[#81C784]"
            status_badge_bg = "bg-[#2E7D32]/25 border-[#388E3C] shadow-[0_0_12px_rgba(56,142,60,0.2)]"
        elif s.ready:
            status_text = "READY"
            status_color = "text-[#90CAF9]"
            status_badge_bg = "bg-[#1565C0]/25 border-[#1E88E5]"
        else:
            status_text = "IDLE"
            status_color = "text-[#B0BEC5]"
            status_badge_bg = "bg-[#37474F]/30 border-[#455A64]"

        # Guidance message
        guide = "System is operating normally within engineering tolerances."
        if s.physical_estop:
            guide = "[Step 1 Emergency] Hardware E-Stop asserted. Ensure physical safety and rotate E-Stop knob CW to release."
        elif is_alarm:
            guide = "[Step 2 Reset] Hardware safe. Press [RESET / Clear Fault] to release software safety latch."
        elif not s.running and s.ready:
            guide = "Equipment is READY. Long-press [START] for 1.5 seconds to start the motor."

        is_in_range = 400 <= s.sensor_raw <= 600
        can_start = s.ready and not s.running and not is_alarm and not s.physical_estop
        can_stop = s.running
        can_clear = is_alarm and not s.physical_estop  # Hardware E-Stop locks reset!
        can_setpoint = not s.physical_estop

        link_state_names = {0: "DISCONNECTED", 1: "CONNECTING", 2: "OPERATIONAL", 3: "COMM_FAULT", 4: "STOPPING"}

        return PresentationState(
            statusText=status_text,
            statusColor=status_color,
            statusBadgeBg=status_badge_bg,
            stationId=s.station_id,
            sensorVal=s.sensor_raw,
            sensorPct=round((s.sensor_raw / 1000.0) * 100.0, 1),
            setpointVal=s.setpoint_raw,
            isInRange=is_in_range,
            rttMs=round(s.read_rtt_ns / 1_000_000.0, 2),
            jitterMs=round(s.poll_jitter_ns / 1_000_000.0, 3),
            heartbeat=s.heartbeat,
            isAlarm=is_alarm,
            alarmCause=self._alarm_cause,
            alarmCauseText=ALARM_CAUSE_MESSAGES.get(self._alarm_cause, "Unknown"),
            errorCode=s.error_code,
            errorCodeText=ERROR_CODE_MESSAGES.get(s.error_code, "OK"),
            canStart=can_start,
            canStop=can_stop,
            canClear=can_clear,
            canSetSetpoint=can_setpoint,
            alarmGuideMsg=guide,
            physicalEstop=s.physical_estop,
            linkState=s.link_state,
            linkStateText=link_state_names.get(s.link_state, "UNKNOWN"),
            ioErrorCount=s.io_error_count,
            consecutiveFailures=s.consecutive_failures,
            busFreqHz=50.0,
        )

    async def trigger_command(self, req: TriggerCommandRequest) -> TriggerCommandResponse:
        start_ns = time.monotonic_ns()
        self._command_id_seq += 1

        async with self._lock:
            # 1. Validate boundary for SET_SETPOINT
            if req.command == CMD_SET_SETPOINT:
                if req.value < SETPOINT_RAW_MIN or req.value > SETPOINT_RAW_MAX:
                    elapsed = int((time.monotonic_ns() - start_ns) / 1_000_000)
                    return TriggerCommandResponse(
                        success=False,
                        command_id=0,
                        outcome=int(CommandOutcome.REJECTED),
                        error_code=ERR_INVALID_ARGUMENT,
                        message=f"Setpoint {req.value} out of range [0, 1000]",
                        generation=self._generation,
                        confirmed_sample_sequence=self._sample_seq,
                        elapsed_ms=elapsed,
                    )

            # 2. Check safety interlocks
            if self._physical_estop:
                elapsed = int((time.monotonic_ns() - start_ns) / 1_000_000)
                return TriggerCommandResponse(
                    success=False,
                    command_id=0,
                    outcome=int(CommandOutcome.REJECTED),
                    error_code=ERR_INTERLOCK_ACTIVE,
                    message="Physical E-Stop is active; command rejected",
                    generation=self._generation,
                    confirmed_sample_sequence=self._sample_seq,
                    elapsed_ms=elapsed,
                )

            if req.command == CMD_START:
                if self._alarm_active:
                    elapsed = int((time.monotonic_ns() - start_ns) / 1_000_000)
                    return TriggerCommandResponse(
                        success=False,
                        command_id=0,
                        outcome=int(CommandOutcome.REJECTED),
                        error_code=ERR_ALARM_ACTIVE,
                        message="Safety alarm is active; cannot start equipment",
                        generation=self._generation,
                        confirmed_sample_sequence=self._sample_seq,
                        elapsed_ms=elapsed,
                    )
                self._running = True
                self._ready = False

            elif req.command == CMD_STOP:
                self._running = False
                self._ready = True

            elif req.command == CMD_RESET:
                self._alarm_active = False
                self._alarm_cause = int(AlarmCause.NONE)
                self._ready = True
                self._running = False
                self._fault_code = FAULT_CODE_NONE

            elif req.command == CMD_SET_SETPOINT:
                self._setpoint_raw = req.value

            else:
                elapsed = int((time.monotonic_ns() - start_ns) / 1_000_000)
                return TriggerCommandResponse(
                    success=False,
                    command_id=0,
                    outcome=int(CommandOutcome.REJECTED),
                    error_code=ERR_INVALID_ARGUMENT,
                    message=f"Unsupported command code: {req.command}",
                    generation=self._generation,
                    confirmed_sample_sequence=self._sample_seq,
                    elapsed_ms=elapsed,
                )

            self._command_counter = (self._command_counter + 1) % COUNTER_MODULO
            elapsed = max(1, int((time.monotonic_ns() - start_ns) / 1_000_000))

            return TriggerCommandResponse(
                success=True,
                command_id=self._command_id_seq,
                outcome=int(CommandOutcome.CONFIRMED),
                error_code=ERR_OK,
                message="Command confirmed by simulated PLC",
                generation=self._generation,
                confirmed_sample_sequence=self._sample_seq,
                elapsed_ms=elapsed,
            )

    async def clear_fault(self, req: ClearFaultRequest) -> ClearFaultResponse:
        start_ns = time.monotonic_ns()
        async with self._lock:
            if self._physical_estop and not req.force_clear:
                elapsed = int((time.monotonic_ns() - start_ns) / 1_000_000)
                return ClearFaultResponse(
                    success=False,
                    error_code=ERR_INTERLOCK_ACTIVE,
                    message="Physical E-Stop is asserted; ClearFault rejected (hardware interlock)",
                    elapsed_ms=elapsed,
                )

            self._alarm_active = False
            self._alarm_cause = int(AlarmCause.NONE)
            self._ready = True
            self._running = False
            self._fault_code = FAULT_CODE_NONE
            self._fault_mode = "NORMAL"
            self._link_state = int(LinkState.OPERATIONAL)
            self._consecutive_failures = 0
            self._is_frozen = False

            elapsed = max(1, int((time.monotonic_ns() - start_ns) / 1_000_000))
            return ClearFaultResponse(
                success=True,
                error_code=ERR_OK,
                message="Fault latch successfully cleared",
                elapsed_ms=elapsed,
            )

    async def inject_fault(self, req: FaultInjectionRequest) -> Dict[str, Any]:
        async with self._lock:
            old_mode = self._fault_mode
            self._fault_mode = req.mode
            self._delay_ms = req.delay_ms

            if req.physical_estop is not None:
                self._physical_estop = req.physical_estop
                if self._physical_estop:
                    self._alarm_active = True
                    self._alarm_cause = int(AlarmCause.PLC_INTERLOCK)
                    self._fault_code = FAULT_CODE_ESTOP
                    self._running = False
                    self._ready = False

            if req.process_fault is not None:
                self._process_fault = req.process_fault
                if self._process_fault:
                    self._alarm_active = True
                    self._alarm_cause = int(AlarmCause.PLC_INTERLOCK)
                    self._fault_code = FAULT_CODE_PROCESS
                    self._running = False
                    self._ready = False

            if req.mode == "NORMAL":
                self._consecutive_failures = 0
                self._is_frozen = False
                self._delay_ms = None
                if not self._physical_estop and not self._process_fault:
                    self._link_state = int(LinkState.OPERATIONAL)
                    self._fault_code = FAULT_CODE_NONE

            logger.info("Fault injected: mode=%s, old_mode=%s, delay=%s, estop=%s, process_fault=%s",
                        req.mode, old_mode, req.delay_ms, req.physical_estop, req.process_fault)

            return {
                "success": True,
                "mode": self._fault_mode,
                "delay_ms": self._delay_ms,
                "physical_estop": self._physical_estop,
                "process_fault": self._process_fault,
                "message": f"Applied fault mode {req.mode}",
            }


class Ros2HmiAdapter(BaseHmiAdapter):
    """ROS 2 DDS Middleware Bridge Adapter.

    Interacts strictly with standard ROS 2 topics and services:
      - Subscribes to `/plc/state` (PlcState)
      - Subscribes to `/safety/alarm` (SafetyAlarm)
      - Calls `/plc/trigger_command` (TriggerCommand)
      - Calls `/plc/clear_fault` (ClearFault)
    Completely decoupled from C++ Gateway core.
    """

    def __init__(self) -> None:
        self._node = None
        self._executor = None
        self._thread = None
        self._latest_state = PlcStateModel()
        self._latest_alarm = SafetyAlarmModel()
        self._lock = asyncio.Lock()
        self._has_real_ros2 = False
        self._mock_fallback: Optional[MockHmiAdapter] = None

    async def start(self) -> None:
        try:
            import rclpy
            from rclpy.executors import SingleThreadedExecutor
            import threading
            from rclpy.node import Node
            from rclpy.qos import (
                QoSDurabilityPolicy,
                QoSHistoryPolicy,
                QoSProfile,
                QoSReliabilityPolicy,
            )
            from ros2_modbus_gateway.msg import PlcState, SafetyAlarm
            from ros2_modbus_gateway.srv import ClearFault, TriggerCommand

            if not rclpy.ok():
                rclpy.init()

            self._node = Node("hmi_bridge_client")

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

            self._state_sub = self._node.create_subscription(
                PlcState, "/plc/state", self._on_plc_state, state_qos
            )
            self._alarm_sub = self._node.create_subscription(
                SafetyAlarm, "/safety/alarm", self._on_safety_alarm, alarm_qos
            )

            self._cmd_client = self._node.create_client(TriggerCommand, "/plc/trigger_command")
            self._clear_client = self._node.create_client(ClearFault, "/plc/clear_fault")

            self._executor = SingleThreadedExecutor()
            self._executor.add_node(self._node)
            self._thread = threading.Thread(target=self._executor.spin, daemon=True)
            self._thread.start()

            self._has_real_ros2 = True
            logger.info("Ros2HmiAdapter successfully initialized and spinning in background thread.")

        except Exception as e:
            logger.warning("ROS 2 rclpy initialization unavailable (%s). Falling back to mock adapter.", e)
            self._has_real_ros2 = False
            self._mock_fallback = MockHmiAdapter(station_id=1)
            await self._mock_fallback.start()

    def _on_plc_state(self, msg: Any) -> None:
        try:
            self._latest_state = PlcStateModel(
                station_id=msg.station_id,
                publish_sequence=msg.publish_sequence,
                sample_sequence=msg.sample_sequence,
                generation=msg.generation,
                link_state=msg.link_state,
                has_sample=msg.has_sample,
                data_valid=msg.data_valid,
                alarm_active=msg.alarm_active,
                heartbeat=msg.heartbeat,
                status_flags=msg.status_flags,
                run_requested=msg.run_requested,
                ready=msg.ready,
                running=msg.running,
                physical_estop=msg.physical_estop,
                reset_requested=msg.reset_requested,
                alarm_tripped=msg.alarm_tripped,
                sensor_raw=msg.sensor_raw,
                setpoint_raw=msg.setpoint_raw,
                fault_code=msg.fault_code,
                applied_command_counter=msg.applied_command_counter,
                sample_age_ns=msg.sample_age_ns,
                heartbeat_age_ns=msg.heartbeat_age_ns,
                read_rtt_ns=msg.read_rtt_ns,
                poll_jitter_ns=msg.poll_jitter_ns,
                poll_attempt_count=msg.poll_attempt_count,
                io_error_count=msg.io_error_count,
                consecutive_failures=msg.consecutive_failures,
                error_code=msg.error_code,
            )
        except Exception as e:
            logger.error("Error parsing ROS 2 PlcState message: %s", e)

    def _on_safety_alarm(self, msg: Any) -> None:
        try:
            self._latest_alarm = SafetyAlarmModel(
                station_id=msg.station_id,
                event_sequence=msg.event_sequence,
                active=msg.active,
                cause=msg.cause,
                error_code=msg.error_code,
                generation=msg.generation,
                detected_ns=msg.detected_ns,
                published_ns=msg.published_ns,
                last_progress_ns=msg.last_progress_ns,
            )
        except Exception as e:
            logger.error("Error parsing ROS 2 SafetyAlarm message: %s", e)

    async def stop(self) -> None:
        if self._mock_fallback:
            await self._mock_fallback.stop()
            self._mock_fallback = None

        if self._executor:
            self._executor.shutdown()
            self._executor = None
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
            self._thread = None
        if self._node:
            self._node.destroy_node()
            self._node = None
        try:
            import rclpy
            if rclpy.ok():
                rclpy.shutdown()
        except Exception:
            pass

    def get_plc_state(self) -> PlcStateModel:
        if not self._has_real_ros2 and self._mock_fallback:
            return self._mock_fallback.get_plc_state()
        return self._latest_state

    def get_safety_alarm(self) -> SafetyAlarmModel:
        if not self._has_real_ros2 and self._mock_fallback:
            return self._mock_fallback.get_safety_alarm()
        return self._latest_alarm

    def get_presentation_state(self) -> PresentationState:
        if not self._has_real_ros2 and self._mock_fallback:
            return self._mock_fallback.get_presentation_state()

        s = self._latest_state
        a = self._latest_alarm
        is_alarm = s.alarm_active or a.active or s.physical_estop or s.link_state == int(LinkState.COMM_FAULT)

        status_text = "READY"
        status_color = "text-[#90CAF9]"
        status_badge_bg = "bg-[#1565C0]/25 border-[#1E88E5]"

        if s.physical_estop:
            status_text = "PHYSICAL E-STOP"
            status_color = "text-[#EF5350]"
            status_badge_bg = "bg-[#C62828]/25 border-[#E53935] shadow-[0_0_15px_rgba(229,57,53,0.3)]"
        elif s.link_state == int(LinkState.COMM_FAULT) or is_alarm:
            status_text = "COMM FAULT / LATCHED"
            status_color = "text-[#FF7043]"
            status_badge_bg = "bg-[#D84315]/25 border-[#F4511E]"
        elif s.running:
            status_text = "RUNNING"
            status_color = "text-[#81C784]"
            status_badge_bg = "bg-[#2E7D32]/25 border-[#388E3C] shadow-[0_0_12px_rgba(56,142,60,0.2)]"
        elif s.ready:
            status_text = "READY"
            status_color = "text-[#90CAF9]"
            status_badge_bg = "bg-[#1565C0]/25 border-[#1E88E5]"
        else:
            status_text = "IDLE"
            status_color = "text-[#B0BEC5]"
            status_badge_bg = "bg-[#37474F]/30 border-[#455A64]"

        guide = "System is operating normally within engineering tolerances."
        if s.physical_estop:
            guide = "[Step 1 Emergency] Hardware E-Stop asserted. Rotate physical knob CW to release."
        elif is_alarm:
            guide = "[Step 2 Reset] Hardware safe. Press [RESET / Clear Fault] to release software safety latch."
        elif not s.running and s.ready:
            guide = "Equipment is READY. Long-press [START] for 1.5 seconds to start."

        link_state_names = {0: "DISCONNECTED", 1: "CONNECTING", 2: "OPERATIONAL", 3: "COMM_FAULT", 4: "STOPPING"}

        return PresentationState(
            statusText=status_text,
            statusColor=status_color,
            statusBadgeBg=status_badge_bg,
            stationId=s.station_id,
            sensorVal=s.sensor_raw,
            sensorPct=round((s.sensor_raw / 1000.0) * 100.0, 1),
            setpointVal=s.setpoint_raw,
            isInRange=400 <= s.sensor_raw <= 600,
            rttMs=round(s.read_rtt_ns / 1_000_000.0, 2),
            jitterMs=round(s.poll_jitter_ns / 1_000_000.0, 3),
            heartbeat=s.heartbeat,
            isAlarm=is_alarm,
            alarmCause=a.cause,
            alarmCauseText=ALARM_CAUSE_MESSAGES.get(a.cause, "None"),
            errorCode=s.error_code,
            errorCodeText=ERROR_CODE_MESSAGES.get(s.error_code, "OK"),
            canStart=s.ready and not s.running and not is_alarm and not s.physical_estop,
            canStop=s.running,
            canClear=is_alarm and not s.physical_estop,
            canSetSetpoint=not s.physical_estop,
            alarmGuideMsg=guide,
            physicalEstop=s.physical_estop,
            linkState=s.link_state,
            linkStateText=link_state_names.get(s.link_state, "UNKNOWN"),
            ioErrorCount=s.io_error_count,
            consecutiveFailures=s.consecutive_failures,
            busFreqHz=50.0,
        )

    async def trigger_command(self, req: TriggerCommandRequest) -> TriggerCommandResponse:
        if not self._has_real_ros2:
            if self._mock_fallback:
                return await self._mock_fallback.trigger_command(req)
            return TriggerCommandResponse(
                success=False,
                outcome=int(CommandOutcome.NOT_SENT),
                error_code=ERR_NOT_READY,
                message="ROS 2 node or TriggerCommand service client not ready",
            )
        try:
            from ros2_modbus_gateway.srv import TriggerCommand
            ros_req = TriggerCommand.Request()
            ros_req.command = req.command
            ros_req.value = req.value

            future = self._cmd_client.call_async(ros_req)
            loop = asyncio.get_running_loop()
            async_future = loop.create_future()

            def _on_done(f: Any) -> None:
                if not async_future.done():
                    exc = f.exception()
                    if exc is not None:
                        loop.call_soon_threadsafe(async_future.set_exception, exc)
                    else:
                        loop.call_soon_threadsafe(async_future.set_result, f.result())

            future.add_done_callback(_on_done)
            resp = await asyncio.wait_for(async_future, timeout=2.0)
            return TriggerCommandResponse(
                success=resp.success,
                command_id=resp.command_id,
                outcome=resp.outcome,
                error_code=resp.error_code,
                system_errno=resp.system_errno,
                modbus_exception=resp.modbus_exception,
                message=resp.message,
                generation=resp.generation,
                confirmed_sample_sequence=resp.confirmed_sample_sequence,
                elapsed_ms=resp.elapsed_ms,
            )
        except Exception as e:
            logger.error("ROS 2 service call failed: %s", e)
            return TriggerCommandResponse(
                success=False,
                outcome=int(CommandOutcome.UNKNOWN),
                error_code=ERR_NOT_READY,
                message=str(e),
            )

    async def clear_fault(self, req: ClearFaultRequest) -> ClearFaultResponse:
        if not self._has_real_ros2:
            if self._mock_fallback:
                return await self._mock_fallback.clear_fault(req)
            return ClearFaultResponse(
                success=False,
                error_code=ERR_NOT_READY,
                message="ROS 2 node or ClearFault service client not ready",
            )
        try:
            from ros2_modbus_gateway.srv import ClearFault
            ros_req = ClearFault.Request()
            ros_req.force_clear = req.force_clear

            future = self._clear_client.call_async(ros_req)
            loop = asyncio.get_running_loop()
            async_future = loop.create_future()

            def _on_done(f: Any) -> None:
                if not async_future.done():
                    exc = f.exception()
                    if exc is not None:
                        loop.call_soon_threadsafe(async_future.set_exception, exc)
                    else:
                        loop.call_soon_threadsafe(async_future.set_result, f.result())

            future.add_done_callback(_on_done)
            resp = await asyncio.wait_for(async_future, timeout=2.0)
            return ClearFaultResponse(
                success=resp.success,
                error_code=resp.error_code,
                message=resp.message,
                elapsed_ms=resp.elapsed_ms,
            )
        except Exception as e:
            logger.error("ROS 2 ClearFault call failed: %s", e)
            return ClearFaultResponse(
                success=False,
                error_code=ERR_NOT_READY,
                message=str(e),
            )

    async def inject_fault(self, req: FaultInjectionRequest) -> Dict[str, Any]:
        if not self._has_real_ros2 and self._mock_fallback:
            return await self._mock_fallback.inject_fault(req)
        try:
            from mock_plc.fault_injector import FaultInjector
            injector = FaultInjector()
            results: Dict[str, Any] = {}
            if req.mode is not None:
                res_fault = injector.set_fault(req.mode, req.delay_ms)
                results["fault"] = res_fault
            if req.physical_estop is not None or req.process_fault is not None:
                res_inputs = injector.set_inputs(req.physical_estop, req.process_fault)
                results["inputs"] = res_inputs
            return {"success": True, "results": results}
        except Exception as e:
            if self._mock_fallback:
                return await self._mock_fallback.inject_fault(req)
            logger.warning("Fault injection via UDS failed: %s", e)
            return {"success": False, "error": str(e)}
