"""Mock PLC Modbus-TCP Server with Integrated Fault Proxy and UDS Control Interface.

Conforms strictly to spec_production.md Section 2.3, Section 2.7, Section 3.7 & Step 8.
"""

import argparse
import asyncio
import logging
import os
import signal
import socket
import sys
from typing import Any, List, Optional

from pymodbus.datastore import ModbusServerContext, ModbusSlaveContext
from pymodbus.server import StartAsyncTcpServer

from mock_plc.constants import (
    HOLDING_REGISTER_COUNT,
    HR_SETPOINT_RAW,
    COIL_COUNT,
    COIL_RUN_REQUESTED,
    COIL_RESET_REQUESTED,
)
from mock_plc.control_protocol import MAX_BUFFER_SIZE, ControlProtocol
from mock_plc.datastore import PlcDataStore
from mock_plc.fault_proxy import FaultProxy

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [mock_plc] %(message)s"
)
logger = logging.getLogger("mock_plc")


class PlcModbusSlaveContext(ModbusSlaveContext):
    """Custom Modbus Slave Context delegating to PlcDataStore."""

    def __init__(self, datastore: PlcDataStore) -> None:
        super().__init__()
        self.datastore = datastore
        self._last_coil_vals: dict[int, bool] = {
            COIL_RUN_REQUESTED: False,
            COIL_RESET_REQUESTED: False,
        }
        self._last_reg_vals: dict[int, int] = {HR_SETPOINT_RAW: 0}

    def validate(self, fc: int, address: int, count: int = 1) -> bool:
        if fc == 3:  # Read Holding Registers
            return 0 <= address and (address + count) <= HOLDING_REGISTER_COUNT
        elif fc in (1, 5):  # Read Coils / Write Single Coil
            return address in (COIL_RUN_REQUESTED, COIL_RESET_REQUESTED) and count == 1
        elif fc == 6:  # Write Single Register
            return address == HR_SETPOINT_RAW and count == 1
        return False

    def getValues(self, fc: int, address: int, count: int = 1) -> List[Any]:
        if fc == 3:
            return self.datastore.read_holding(address, count)
        elif fc == 5:
            return [self._last_coil_vals.get(address, False)]
        elif fc == 1:
            if address == COIL_RUN_REQUESTED:
                return [self.datastore.run_requested]
            elif address == COIL_RESET_REQUESTED:
                return [self.datastore.reset_requested]
            return [False]
        elif fc == 6:
            return [self._last_reg_vals.get(address, self.datastore.setpoint_raw)]
        return []

    def setValues(self, fc: int, address: int, values: List[Any]) -> None:
        if not values:
            return
        if fc == 5:
            val = bool(values[0])
            self._last_coil_vals[address] = val
            self.datastore.write_coil(address, val)
        elif fc == 6:
            val = int(values[0])
            self._last_reg_vals[address] = val
            self.datastore.write_register(address, val)


async def scan_loop(datastore: PlcDataStore, interval: float = 0.010) -> None:
    """10ms periodic PLC logic scan loop."""
    logger.info("Starting 10ms PLC scan loop...")
    try:
        while True:
            await asyncio.sleep(interval)
            datastore.scan()
    except asyncio.CancelledError:
        logger.info("PLC scan loop cancelled.")


class MockPlcSystem:
    """Encapsulates backend Modbus server, FaultProxy, and UDS control interface."""

    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 5020,
        backend_port: int = 15020,
        control_sock: str = "/run/plc/control.sock",
        scan_interval: float = 0.010,
    ) -> None:
        self.host = host
        self.port = port
        self.backend_port = backend_port
        self.control_sock = control_sock
        self.scan_interval = scan_interval

        self.datastore = PlcDataStore()
        self.is_frozen = False

        self.proxy = FaultProxy(
            listen_host=self.host,
            listen_port=self.port,
            backend_host="127.0.0.1",
            backend_port=self.backend_port,
            on_freeze_change=self._on_freeze_change,
        )

        self._uds_servers: List[asyncio.Server] = []
        self._tasks: List[asyncio.Task] = []

    def _on_freeze_change(self, frozen: bool) -> None:
        self.is_frozen = frozen
        logger.info("PLC scan freeze state changed: is_frozen=%s", frozen)

    async def scan_loop(self) -> None:
        """10ms periodic PLC logic scan loop with freeze support."""
        logger.info("Starting 10ms PLC scan loop...")
        try:
            while True:
                await asyncio.sleep(self.scan_interval)
                if not self.is_frozen:
                    self.datastore.scan()
        except asyncio.CancelledError:
            logger.info("PLC scan loop cancelled.")

    async def handle_uds_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        """Handle UDS control request per Section 2.7."""
        req_id: Optional[int] = None
        try:
            line = await reader.readline()
            if not line:
                writer.close()
                await writer.wait_closed()
                return

            req = ControlProtocol.parse_request(line)
            req_id = req["request_id"]
            op = req["op"]

            if op == "SET_FAULT":
                mode = req["mode"]
                delay_ms = req.get("delay_ms")
                mode_res = await self.proxy.set_mode(mode, delay_ms)
                resp = ControlProtocol.build_success_response(
                    request_id=req_id,
                    message=f"Fault mode applied: {mode}",
                    applied_ns=mode_res["applied_ns"],
                    mode=mode_res["mode"],
                    delay_ms=mode_res["delay_ms"],
                    physical_estop=self.datastore.physical_estop,
                    process_fault=self.datastore._process_fault,
                )

            elif op == "SET_INPUTS":
                p_estop = req.get("physical_estop", self.datastore.physical_estop)
                p_fault = req.get("process_fault", self.datastore._process_fault)
                self.datastore.set_inputs(physical_estop=p_estop, process_fault=p_fault)
                resp = ControlProtocol.build_success_response(
                    request_id=req_id,
                    message="PLC inputs updated",
                    applied_ns=self.proxy.applied_ns,
                    mode=self.proxy.mode,
                    delay_ms=self.proxy.delay_ms,
                    physical_estop=self.datastore.physical_estop,
                    process_fault=self.datastore._process_fault,
                )

            elif op == "GET_STATUS":
                resp = ControlProtocol.build_success_response(
                    request_id=req_id,
                    message="PLC and proxy status query",
                    applied_ns=self.proxy.applied_ns,
                    mode=self.proxy.mode,
                    delay_ms=self.proxy.delay_ms,
                    physical_estop=self.datastore.physical_estop,
                    process_fault=self.datastore._process_fault,
                )
            else:
                resp = ControlProtocol.build_error_response(req_id, f"Unsupported op: {op}")

        except Exception as e:
            logger.warning("UDS request handling error: %s", e)
            resp = ControlProtocol.build_error_response(req_id, str(e))

        try:
            payload = ControlProtocol.encode_response(resp)
            writer.write(payload)
            await writer.drain()
        except Exception as e:
            logger.error("Failed to send UDS response: %s", e)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    async def start_uds_server(self, sock_path: str) -> None:
        """Start a Unix domain socket server on specified path."""
        if not hasattr(socket, "AF_UNIX"):
            logger.warning("AF_UNIX not supported on this platform, skipping UDS on %s", sock_path)
            return

        sock_dir = os.path.dirname(sock_path)
        if sock_dir and not os.path.exists(sock_dir):
            try:
                os.makedirs(sock_dir, exist_ok=True)
            except Exception as e:
                logger.warning("Could not create directory for socket %s: %s", sock_path, e)
                return

        if os.path.exists(sock_path):
            try:
                os.remove(sock_path)
            except Exception:
                pass

        try:
            srv = await asyncio.start_unix_server(self.handle_uds_client, path=sock_path)
            # Ensure proper socket permissions for container sharing
            try:
                os.chmod(sock_path, 0o777)
            except Exception:
                pass
            self._uds_servers.append(srv)
            logger.info("UDS control server listening on %s", sock_path)
        except Exception as e:
            logger.error("Failed to start UDS server on %s: %s", sock_path, e)

    async def run(self) -> None:
        """Run all mock PLC subsystems."""
        # 1. Start scan loop
        scan_task = asyncio.create_task(self.scan_loop())
        self._tasks.append(scan_task)

        # 2. Start UDS control servers (both default and alias)
        await self.start_uds_server(self.control_sock)
        if self.control_sock != "/tmp/modbus_fault_proxy.sock":
            await self.start_uds_server("/tmp/modbus_fault_proxy.sock")

        # 3. Start FaultProxy (front-facing Modbus port)
        await self.proxy.start()

        # 4. Start backend Modbus TCP server (internal port)
        slave_ctx = PlcModbusSlaveContext(self.datastore)
        server_ctx = ModbusServerContext(slaves=slave_ctx, single=True)
        logger.info("Starting internal backend Modbus server on 127.0.0.1:%d...", self.backend_port)

        try:
            await StartAsyncTcpServer(
                context=server_ctx,
                address=("127.0.0.1", self.backend_port),
            )
        except asyncio.CancelledError:
            logger.info("Mock PLC backend server task cancelled.")
        finally:
            self.shutdown()

    def shutdown(self) -> None:
        """Clean shutdown of all subsystems."""
        for t in self._tasks:
            t.cancel()
        self.proxy.close()
        for s in self._uds_servers:
            s.close()
        for p in (self.control_sock, "/tmp/modbus_fault_proxy.sock"):
            if os.path.exists(p):
                try:
                    os.remove(p)
                except Exception:
                    pass
        logger.info("Mock PLC system shut down safely.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock Industrial PLC Modbus-TCP Server with Fault Proxy")
    parser.add_argument("--host", default="0.0.0.0", help="Host address for proxy (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=5020, help="Proxy Modbus port (default: 5020)")
    parser.add_argument("--backend-port", type=int, default=15020, help="Internal backend port (default: 15020)")
    parser.add_argument("--control-sock", default="/run/plc/control.sock", help="Path to UDS control socket")
    parser.add_argument("--scan-interval", type=float, default=0.010, help="Scan interval in seconds (default: 0.010)")
    args = parser.parse_args()

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    system = MockPlcSystem(
        host=args.host,
        port=args.port,
        backend_port=args.backend_port,
        control_sock=args.control_sock,
        scan_interval=args.scan_interval,
    )

    if sys.platform != "win32":
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, lambda: [task.cancel() for task in asyncio.all_tasks(loop)])

    try:
        loop.run_until_complete(system.run())
    except (KeyboardInterrupt, asyncio.CancelledError):
        logger.info("Interrupted by user or signal.")
    finally:
        system.shutdown()
        loop.close()


if __name__ == "__main__":
    main()
