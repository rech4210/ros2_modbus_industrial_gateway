"""Unit tests for PlcDataStore.

Verifies Step 3 of spec_production.md.
Uses mock_plc.constants to eliminate magic numbers.
"""

import pytest
import sys
from pathlib import Path

# Ensure mock_plc module can be imported
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mock_plc.datastore import PlcDataStore
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


def test_initial_state():
    ds = PlcDataStore()
    regs = ds.read_holding(HR_HEARTBEAT, HOLDING_REGISTER_COUNT)
    assert len(regs) == HOLDING_REGISTER_COUNT
    heartbeat, flags, sensor, setpoint, fault, counter = regs
    assert heartbeat == 0
    # ready should be 1 (STATUS_BIT_READY)
    assert (flags & STATUS_BIT_READY) != 0
    assert (flags & STATUS_BIT_RUN_REQUESTED) == 0
    assert (flags & STATUS_BIT_RUNNING) == 0
    assert (flags & STATUS_BIT_PHYSICAL_ESTOP) == 0
    assert sensor == 0
    assert setpoint == 0
    assert fault == FAULT_CODE_NONE
    assert counter == 0


def test_read_holding_boundaries():
    ds = PlcDataStore()
    # Read count 6 from 0
    assert len(ds.read_holding(HR_HEARTBEAT, HOLDING_REGISTER_COUNT)) == HOLDING_REGISTER_COUNT
    # Read count 1 from last register
    assert len(ds.read_holding(HR_APPLIED_COMMAND_COUNTER, 1)) == 1

    # Invalid reads
    with pytest.raises(ValueError):
        ds.read_holding(-1, 1)
    with pytest.raises(ValueError):
        ds.read_holding(HR_HEARTBEAT, 0)
    with pytest.raises(ValueError):
        ds.read_holding(HR_HEARTBEAT, HOLDING_REGISTER_COUNT + 1)
    with pytest.raises(ValueError):
        ds.read_holding(HR_APPLIED_COMMAND_COUNTER, 2)


def test_heartbeat_increment_and_wrap():
    ds = PlcDataStore()
    ds.scan()
    assert ds.heartbeat == 1
    ds.scan()
    assert ds.heartbeat == 2

    # Test modulo wrap
    ds.heartbeat = COUNTER_MODULO - 1
    ds.scan()
    assert ds.heartbeat == 0


def test_start_and_stop_command():
    ds = PlcDataStore()
    # Set setpoint first
    assert ds.write_register(HR_SETPOINT_RAW, 500) is True
    ds.scan()
    assert ds.setpoint_raw == 500
    assert ds.applied_command_counter == 1
    assert ds.running is False
    assert ds.sensor_raw == 0

    # Send START (coil 0 = True)
    assert ds.write_coil(COIL_RUN_REQUESTED, True) is True
    ds.scan()
    assert ds.run_requested is True
    assert ds.running is True
    assert ds.ready is True
    assert ds.sensor_raw == 500  # sensor_raw == setpoint_raw when running
    assert ds.applied_command_counter == 2
    assert (ds.status_flags & STATUS_BIT_RUN_REQUESTED) != 0
    assert (ds.status_flags & STATUS_BIT_RUNNING) != 0

    # Send STOP (coil 0 = False)
    assert ds.write_coil(COIL_RUN_REQUESTED, False) is True
    ds.scan()
    assert ds.run_requested is False
    assert ds.running is False
    assert ds.sensor_raw == 0  # sensor_raw = 0 when stopped
    assert ds.applied_command_counter == 3
    assert (ds.status_flags & STATUS_BIT_RUNNING) == 0


def test_setpoint_limits():
    ds = PlcDataStore()
    assert ds.write_register(HR_SETPOINT_RAW, SETPOINT_RAW_MIN) is True
    assert ds.write_register(HR_SETPOINT_RAW, SETPOINT_RAW_MAX) is True
    assert ds.write_register(HR_SETPOINT_RAW, SETPOINT_RAW_MIN - 1) is False
    assert ds.write_register(HR_SETPOINT_RAW, SETPOINT_RAW_MAX + 1) is False
    # Register other than HR_SETPOINT_RAW is not writable
    assert ds.write_register(HR_HEARTBEAT, 100) is False
    assert ds.write_register(HR_SENSOR_RAW, 100) is False


def test_physical_estop_interlock():
    ds = PlcDataStore()
    # Start running
    ds.write_register(HR_SETPOINT_RAW, 800)
    ds.write_coil(COIL_RUN_REQUESTED, True)
    ds.scan()
    assert ds.running is True
    assert ds.sensor_raw == 800

    # Trip physical E-Stop
    ds.set_inputs(physical_estop=True, process_fault=False)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_ESTOP
    assert ds.ready is False
    assert ds.running is False
    assert ds.run_requested is False
    assert ds.sensor_raw == 0
    assert (ds.status_flags & STATUS_BIT_PHYSICAL_ESTOP) != 0
    assert (ds.status_flags & STATUS_BIT_ALARM_TRIPPED) != 0

    # Try to start while E-Stop is active: must be rejected without incrementing counter
    counter_before = ds.applied_command_counter
    ds.write_coil(COIL_RUN_REQUESTED, True)
    ds.scan()
    assert ds.running is False
    assert ds.run_requested is False
    assert ds.applied_command_counter == counter_before


def test_reset_interlock():
    ds = PlcDataStore()
    ds.set_inputs(physical_estop=True, process_fault=False)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_ESTOP
    assert ds.applied_command_counter == 0

    # Reset while E-Stop is still active: should NOT clear fault, should NOT consume reset, counter unchanged
    ds.write_coil(COIL_RESET_REQUESTED, True)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_ESTOP
    assert ds.reset_requested is True
    assert (ds.status_flags & STATUS_BIT_RESET_REQUESTED) != 0
    assert ds.applied_command_counter == 0

    # Release E-Stop: scan should consume pending reset and clear fault
    ds.set_inputs(physical_estop=False, process_fault=False)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_NONE
    assert ds.reset_requested is False
    assert (ds.status_flags & STATUS_BIT_RESET_REQUESTED) == 0
    assert ds.ready is True
    assert ds.applied_command_counter == 1

    # Subsequent reset when normal
    ds.write_coil(COIL_RESET_REQUESTED, True)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_NONE
    assert ds.reset_requested is False
    assert ds.applied_command_counter == 2


def test_process_fault_and_reset():
    ds = PlcDataStore()
    ds.set_inputs(physical_estop=False, process_fault=True)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_PROCESS
    assert ds.ready is False
    assert ds.applied_command_counter == 0

    # Try reset while process fault is still asserted: cannot clear and cannot consume
    ds.write_coil(COIL_RESET_REQUESTED, True)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_PROCESS
    assert ds.reset_requested is True
    assert ds.applied_command_counter == 0

    # Clear process fault input: scan consumes pending reset and clears fault
    ds.set_inputs(physical_estop=False, process_fault=False)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_NONE
    assert ds.reset_requested is False
    assert ds.ready is True
    assert ds.applied_command_counter == 1


def test_snapshot_keys():
    ds = PlcDataStore()
    snap = ds.get_snapshot()
    expected_keys = {
        "heartbeat", "status_flags", "run_requested", "ready",
        "running", "physical_estop", "reset_requested", "alarm_tripped",
        "sensor_raw", "setpoint_raw", "fault_code", "applied_command_counter"
    }
    assert set(snap.keys()) == expected_keys


def test_modbus_tcp_server_fc03_fc05_fc06():
    import asyncio
    from pymodbus.client import AsyncModbusTcpClient
    from pymodbus.datastore import ModbusServerContext
    from pymodbus.server import StartAsyncTcpServer
    from mock_plc.mock_plc_server import PlcModbusSlaveContext, scan_loop

    async def _test():
        ds = PlcDataStore()
        slave_ctx = PlcModbusSlaveContext(ds)
        server_ctx = ModbusServerContext(slaves=slave_ctx, single=True)

        test_port = 55022
        scan_task = asyncio.create_task(scan_loop(ds, interval=0.010))
        server_task = asyncio.create_task(
            StartAsyncTcpServer(context=server_ctx, address=("127.0.0.1", test_port))
        )
        await asyncio.sleep(0.15)  # Wait for server to start

        try:
            client = AsyncModbusTcpClient("127.0.0.1", port=test_port)
            connected = await client.connect()
            assert connected is True

            # 1. FC03 Read 6 Holding Registers
            rr = await client.read_holding_registers(address=HR_HEARTBEAT, count=HOLDING_REGISTER_COUNT, slave=1)
            assert not rr.isError()
            assert len(rr.registers) == HOLDING_REGISTER_COUNT
            assert (rr.registers[HR_STATUS_FLAGS] & STATUS_BIT_READY) != 0

            # 2. FC06 Write setpoint
            wr = await client.write_register(address=HR_SETPOINT_RAW, value=750, slave=1)
            assert not wr.isError()

            # 3. FC05 Write run_requested (START)
            wc = await client.write_coil(address=COIL_RUN_REQUESTED, value=True, slave=1)
            assert not wc.isError()

            # Wait for 5 scan cycles (~50ms)
            await asyncio.sleep(0.06)

            # 4. FC03 Read back updated registers
            rr2 = await client.read_holding_registers(address=HR_HEARTBEAT, count=HOLDING_REGISTER_COUNT, slave=1)
            assert not rr2.isError()
            heartbeat, flags, sensor_raw, setpoint_raw, fault_code, counter = rr2.registers
            assert heartbeat > 0
            assert (flags & STATUS_BIT_RUNNING) != 0
            assert setpoint_raw == 750
            assert sensor_raw == 750
            assert fault_code == FAULT_CODE_NONE
            assert counter == 2

            client.close()
        finally:
            scan_task.cancel()
            server_task.cancel()
            await asyncio.gather(scan_task, server_task, return_exceptions=True)

    asyncio.run(_test())


def test_start_interlock_on_fault():
    ds = PlcDataStore()
    ds.set_inputs(physical_estop=False, process_fault=True)
    ds.scan()
    assert ds.fault_code == FAULT_CODE_PROCESS
    assert ds.ready is False

    # Attempt to start while in process fault: must be rejected and counter unchanged
    ds.write_coil(COIL_RUN_REQUESTED, True)
    ds.scan()
    assert ds.running is False
    assert ds.run_requested is False
    assert ds.applied_command_counter == 0


def test_multiple_commands_single_scan():
    ds = PlcDataStore()
    # Queue setpoint and start together
    ds.write_register(HR_SETPOINT_RAW, 400)
    ds.write_coil(COIL_RUN_REQUESTED, True)
    ds.scan()
    assert ds.setpoint_raw == 400
    assert ds.run_requested is True
    assert ds.running is True
    assert ds.sensor_raw == 400
    assert ds.applied_command_counter == 2
