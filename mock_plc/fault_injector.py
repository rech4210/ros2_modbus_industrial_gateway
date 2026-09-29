"""Fault Injector client and CLI tool for Mock PLC Fault Proxy.

Conforms strictly to spec_production.md Section 2.7, Section 3.7 & Step 8.
"""

import argparse
import json
import os
import socket
import sys
import time
from typing import Any, Dict, Optional

from mock_plc.control_protocol import MAX_BUFFER_SIZE, ControlProtocol

DEFAULT_SOCK_PATHS = [
    os.environ.get("FAULT_CONTROL_SOCK", ""),
    "/run/plc/control.sock",
    "/tmp/modbus_fault_proxy.sock",
]


class FaultInjector:
    """Sends control commands to FaultProxy via Unix Domain Socket."""

    def __init__(self, sock_path: Optional[str] = None) -> None:
        self.sock_path = sock_path or self._resolve_sock_path()
        self._request_counter = 1000

    @staticmethod
    def _resolve_sock_path() -> str:
        for p in DEFAULT_SOCK_PATHS:
            if p and os.path.exists(p):
                return p
        return "/run/plc/control.sock"

    def _next_request_id(self) -> int:
        self._request_counter += 1
        return self._request_counter

    def send_command(
        self, request: Dict[str, Any], timeout_sec: float = 1.0, sock_path: Optional[str] = None
    ) -> Dict[str, Any]:
        """Send a single request to UDS server and return the parsed response."""
        path = sock_path or self.sock_path
        if not hasattr(socket, "AF_UNIX"):
            raise RuntimeError("AF_UNIX is not supported on this platform")

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout_sec)
        try:
            sock.connect(path)
            # Ensure request_id
            if "request_id" not in request:
                request["request_id"] = self._next_request_id()

            # Encode and send
            payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
            sock.sendall(payload)

            # Read response
            buffer = bytearray()
            while True:
                chunk = sock.recv(MAX_BUFFER_SIZE)
                if not chunk:
                    break
                buffer.extend(chunk)
                if b"\n" in buffer or len(buffer) >= MAX_BUFFER_SIZE:
                    break

            if not buffer:
                raise RuntimeError("No response received from proxy control socket (connection closed)")

            line = buffer.split(b"\n")[0].decode("utf-8")
            response = json.loads(line)
            return response
        finally:
            sock.close()

    def set_fault(self, mode: str, delay_ms: Optional[int] = None) -> Dict[str, Any]:
        """Set proxy fault injection mode."""
        req: Dict[str, Any] = {
            "request_id": self._next_request_id(),
            "op": "SET_FAULT",
            "mode": mode,
            "delay_ms": delay_ms,
        }
        return self.send_command(req)

    def set_inputs(
        self,
        physical_estop: Optional[bool] = None,
        process_fault: Optional[bool] = None,
    ) -> Dict[str, Any]:
        """Manipulate mock PLC physical inputs."""
        req: Dict[str, Any] = {
            "request_id": self._next_request_id(),
            "op": "SET_INPUTS",
        }
        if physical_estop is not None:
            req["physical_estop"] = bool(physical_estop)
        if process_fault is not None:
            req["process_fault"] = bool(process_fault)
        return self.send_command(req)

    def get_status(self) -> Dict[str, Any]:
        """Retrieve current proxy and datastore status."""
        req: Dict[str, Any] = {
            "request_id": self._next_request_id(),
            "op": "GET_STATUS",
        }
        return self.send_command(req)

    def interactive_cli(self) -> None:
        """Run interactive terminal menu for fault injection testing."""
        print("======================================================")
        print(" Industrial Gateway Mock PLC - Fault Injector CLI")
        print(f" Target Control Socket: {self.sock_path}")
        print("======================================================")

        while True:
            try:
                print("\nAvailable Actions:")
                print("  [1] NORMAL       - Restore normal communication")
                print("  [2] DROP         - Drop all Modbus requests")
                print("  [3] DROP_ONE     - Drop single request (1-shot)")
                print("  [4] DELAY        - Introduce response delay (ms)")
                print("  [5] DISCONNECT   - Abort TCP session (RST/FIN)")
                print("  [6] FREEZE       - Freeze PLC scan loop (HEARTBEAT_STALE)")
                print("  [7] MALFORMED    - Corrupt MBAP Protocol ID (1-shot)")
                print("  [8] TRUNCATE     - Truncate response frame (1-shot)")
                print("  [9] BITFLIP      - Corrupt payload bit (1-shot)")
                print("  [e] Toggle E-Stop")
                print("  [s] Get Status")
                print("  [q] Quit")

                choice = input("\nSelect action > ").strip()
                if choice == "q":
                    print("Exiting CLI.")
                    break
                elif choice == "1":
                    res = self.set_fault("NORMAL")
                elif choice == "2":
                    res = self.set_fault("DROP")
                elif choice == "3":
                    res = self.set_fault("DROP_ONE")
                elif choice == "4":
                    ms_str = input("Enter delay in milliseconds (1~1000) [200]: ").strip()
                    delay = int(ms_str) if ms_str else 200
                    res = self.set_fault("DELAY", delay_ms=delay)
                elif choice == "5":
                    res = self.set_fault("DISCONNECT")
                elif choice == "6":
                    res = self.set_fault("FREEZE")
                elif choice == "7":
                    res = self.set_fault("MALFORMED")
                elif choice == "8":
                    res = self.set_fault("TRUNCATE")
                elif choice == "9":
                    res = self.set_fault("BITFLIP")
                elif choice.lower() == "e":
                    cur = self.get_status()
                    new_estop = not cur.get("physical_estop", False)
                    res = self.set_inputs(physical_estop=new_estop)
                    print(f"Toggled physical_estop to {new_estop}")
                elif choice.lower() == "s":
                    res = self.get_status()
                else:
                    print("Invalid selection.")
                    continue

                print(f"Response: {json.dumps(res, indent=2)}")

            except Exception as e:
                print(f"Error: {e}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock PLC Fault Injection CLI Tool")
    parser.add_argument("--sock-path", default=None, help="Path to UDS control socket")
    parser.add_argument("--interactive", action="store_true", help="Launch interactive CLI menu")
    parser.add_argument(
        "--mode",
        choices=[
            "NORMAL",
            "DROP",
            "DROP_ONE",
            "DELAY",
            "DISCONNECT",
            "FREEZE",
            "MALFORMED",
            "TRUNCATE",
            "BITFLIP",
        ],
        help="Set fault injection mode",
    )
    parser.add_argument("--delay", type=int, default=None, help="Delay in ms for DELAY mode")
    parser.add_argument("--estop", action="store_true", help="Set physical E-Stop active")
    parser.add_argument("--clear-estop", action="store_true", help="Clear physical E-Stop")
    parser.add_argument("--fault", action="store_true", help="Set process fault active")
    parser.add_argument("--clear-fault", action="store_true", help="Clear process fault")
    parser.add_argument("--status", action="store_true", help="Query proxy and PLC status")

    args = parser.parse_args()
    injector = FaultInjector(sock_path=args.sock_path)

    if args.interactive:
        injector.interactive_cli()
        return

    if args.status:
        res = injector.get_status()
        print(json.dumps(res, indent=2))
        return

    if args.mode:
        res = injector.set_fault(args.mode, delay_ms=args.delay)
        print(json.dumps(res, indent=2))
        return

    if args.estop:
        res = injector.set_inputs(physical_estop=True)
        print(json.dumps(res, indent=2))
        return

    if args.clear_estop:
        res = injector.set_inputs(physical_estop=False)
        print(json.dumps(res, indent=2))
        return

    if args.fault:
        res = injector.set_inputs(process_fault=True)
        print(json.dumps(res, indent=2))
        return

    if args.clear_fault:
        res = injector.set_inputs(process_fault=False)
        print(json.dumps(res, indent=2))
        return

    parser.print_help()


if __name__ == "__main__":
    main()
