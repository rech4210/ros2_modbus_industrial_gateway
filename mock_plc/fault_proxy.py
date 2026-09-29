"""Modbus-TCP Fault Injection Proxy.

Sits between Modbus client (ROS gateway) and Mock PLC backend.
Conforms strictly to spec_production.md Section 2.7, Section 3.7 & Step 8.
"""

import asyncio
import logging
import time
from typing import Any, Callable, Dict, Optional, Set

logger = logging.getLogger("fault_proxy")


class FaultProxy:
    """Modbus-TCP traffic proxy with dynamic fault injection capabilities."""

    def __init__(
        self,
        listen_host: str = "0.0.0.0",
        listen_port: int = 5020,
        backend_host: str = "127.0.0.1",
        backend_port: int = 15020,
        on_freeze_change: Optional[Callable[[bool], None]] = None,
    ) -> None:
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.backend_host = backend_host
        self.backend_port = backend_port
        self.on_freeze_change = on_freeze_change

        self.mode: str = "NORMAL"
        self.delay_ms: Optional[int] = None
        self.applied_ns: int = time.monotonic_ns()

        self._server: Optional[asyncio.Server] = None
        self._active_writers: Set[asyncio.StreamWriter] = set()
        self._pending_tasks: Set[asyncio.Task] = set()
        self._lock = asyncio.Lock()

    async def start(self) -> None:
        """Start listening for incoming Modbus TCP client connections."""
        self._server = await asyncio.start_server(
            self.handle_client,
            self.listen_host,
            self.listen_port,
        )
        logger.info(
            "FaultProxy listening on %s:%d (forwarding to %s:%d, mode=%s)",
            self.listen_host,
            self.listen_port,
            self.backend_host,
            self.backend_port,
            self.mode,
        )

    async def set_mode(self, mode: str, delay_ms: Optional[int] = None) -> Dict[str, Any]:
        """Change fault injection mode and clean up existing connections if necessary."""
        async with self._lock:
            old_mode = self.mode
            self.mode = mode
            self.delay_ms = delay_ms
            self.applied_ns = time.monotonic_ns()

            logger.info("FaultProxy mode changed from %s to %s (delay_ms=%s)", old_mode, mode, delay_ms)

            # Cancel any pending delayed transmission tasks
            for task in list(self._pending_tasks):
                if not task.done():
                    task.cancel()
            self._pending_tasks.clear()

            # Handle FREEZE state transition
            if mode == "FREEZE":
                if self.on_freeze_change:
                    self.on_freeze_change(True)
            elif old_mode == "FREEZE":
                if self.on_freeze_change:
                    self.on_freeze_change(False)

            # Handle DISCONNECT: immediately abort active client connections with RST/FIN
            if mode == "DISCONNECT":
                writers_to_close = list(self._active_writers)
                for w in writers_to_close:
                    try:
                        w.close()
                    except Exception:
                        pass
                self._active_writers.clear()

            return {
                "mode": self.mode,
                "delay_ms": self.delay_ms,
                "applied_ns": self.applied_ns,
            }

    async def handle_client(
        self, client_reader: asyncio.StreamReader, client_writer: asyncio.StreamWriter
    ) -> None:
        """Handle an incoming client connection, relaying packets through fault filters."""
        # Check DISCONNECT rejection on new connection
        if self.mode == "DISCONNECT":
            logger.info("Rejecting new connection in DISCONNECT mode")
            client_writer.close()
            try:
                await client_writer.wait_closed()
            except Exception:
                pass
            return

        # Connect to backend
        try:
            backend_reader, backend_writer = await asyncio.open_connection(
                self.backend_host, self.backend_port
            )
        except Exception as e:
            logger.error("Failed to connect to Mock PLC backend %s:%d: %e", self.backend_host, self.backend_port, e)
            client_writer.close()
            return

        self._active_writers.add(client_writer)
        self._active_writers.add(backend_writer)

        try:
            while True:
                # 1. Read Modbus TCP MBAP Header (7 bytes)
                try:
                    header = await client_reader.readexactly(7)
                except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
                    break

                # MBAP: TransID (2B) + ProtoID (2B) + Length (2B) + UnitID (1B)
                length = int.from_bytes(header[4:6], byteorder="big")
                # Length includes UnitID (1 byte) + PDU
                pdu_len = length - 1
                if pdu_len < 0:
                    logger.warning("Invalid MBAP length field: %d", length)
                    break

                pdu = await client_reader.readexactly(pdu_len)
                full_request = header + pdu

                # Check Fault Mode for Client -> Backend
                if self.mode == "DISCONNECT":
                    logger.info("Fault DISCONNECT: closing client session")
                    break

                if self.mode == "DROP":
                    logger.info("Fault DROP: dropping incoming request (%d bytes)", len(full_request))
                    continue

                if self.mode == "DROP_ONE":
                    logger.info("Fault DROP_ONE: dropping single request, reverting to NORMAL")
                    self.mode = "NORMAL"
                    continue

                # Forward request to backend
                backend_writer.write(full_request)
                await backend_writer.drain()

                # 2. Read Response from Backend
                try:
                    resp_header = await backend_reader.readexactly(7)
                except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
                    break

                resp_length = int.from_bytes(resp_header[4:6], byteorder="big")
                resp_pdu_len = resp_length - 1
                if resp_pdu_len < 0:
                    logger.warning("Invalid backend MBAP response length: %d", resp_length)
                    break

                resp_pdu = await backend_reader.readexactly(resp_pdu_len)
                full_response = bytearray(resp_header + resp_pdu)

                # Check Fault Mode for Backend -> Client
                if self.mode == "DISCONNECT":
                    logger.info("Fault DISCONNECT: closing before response transmission")
                    break

                if self.mode == "DROP":
                    logger.info("Fault DROP: dropping backend response (%d bytes)", len(full_response))
                    continue

                if self.mode == "DELAY":
                    if self.delay_ms and self.delay_ms > 0:
                        delay_sec = self.delay_ms / 1000.0
                        logger.debug("Fault DELAY: sleeping %.3f seconds before response", delay_sec)
                        sleep_task = asyncio.create_task(asyncio.sleep(delay_sec))
                        self._pending_tasks.add(sleep_task)
                        try:
                            await sleep_task
                        except asyncio.CancelledError:
                            logger.info("DELAY task cancelled due to mode change or disconnect")
                            break
                        finally:
                            self._pending_tasks.discard(sleep_task)

                        if self.mode == "DISCONNECT":
                            break

                elif self.mode == "MALFORMED":
                    # Mutate Protocol ID in MBAP (bytes 2-3) to 0x0001
                    logger.info("Fault MALFORMED: corrupting Protocol ID in MBAP header")
                    full_response[2] = 0x00
                    full_response[3] = 0x01
                    # One-shot injection per spec
                    self.mode = "NORMAL"

                elif self.mode == "TRUNCATE":
                    logger.info("Fault TRUNCATE: truncating response to 5 bytes")
                    full_response = full_response[:5]
                    self.mode = "NORMAL"

                elif self.mode == "BITFLIP":
                    logger.info("Fault BITFLIP: flipping bit in payload")
                    if len(full_response) > 8:
                        full_response[8] ^= 0x01
                    self.mode = "NORMAL"

                # Send response to client
                client_writer.write(bytes(full_response))
                await client_writer.drain()

        except Exception as e:
            logger.debug("Client handler ended: %e", e)
        finally:
            self._active_writers.discard(client_writer)
            self._active_writers.discard(backend_writer)
            try:
                client_writer.close()
                await client_writer.wait_closed()
            except Exception:
                pass
            try:
                backend_writer.close()
                await backend_writer.wait_closed()
            except Exception:
                pass

    def close(self) -> None:
        """Shut down proxy server and close all remaining sockets."""
        if self._server:
            self._server.close()
        for w in list(self._active_writers):
            try:
                w.close()
            except Exception:
                pass
        self._active_writers.clear()
        for t in list(self._pending_tasks):
            t.cancel()
        self._pending_tasks.clear()
        logger.info("FaultProxy closed.")
