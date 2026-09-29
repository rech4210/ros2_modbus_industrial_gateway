"""Control Protocol JSON parser and validator for Mock PLC Fault Proxy.

Conforms strictly to spec_production.md Section 2.7.
"""

import json
import time
from typing import Any, Dict, List, Optional, Tuple

MAX_BUFFER_SIZE = 4096
ALLOWED_REQUEST_KEYS = {
    "request_id",
    "op",
    "mode",
    "delay_ms",
    "physical_estop",
    "process_fault",
}
ALLOWED_OPS = {"SET_FAULT", "SET_INPUTS", "GET_STATUS"}
ALLOWED_MODES = {
    "NORMAL",
    "DROP",
    "DELAY",
    "DISCONNECT",
    "FREEZE",
    "MALFORMED",
    "TRUNCATE",
    "BITFLIP",
    "DROP_ONE",
}


def _check_duplicate_keys(pairs: List[Tuple[str, Any]]) -> Dict[str, Any]:
    keys = set()
    result = {}
    for k, v in pairs:
        if k in keys:
            raise ValueError(f"Duplicate JSON key: {k}")
        keys.add(k)
        result[k] = v
    return result


class ControlProtocol:
    """Parses and validates JSON control messages over UDS according to spec."""

    @staticmethod
    def parse_request(payload: bytes) -> Dict[str, Any]:
        """Parse and strictly validate raw bytes request from UDS."""
        if len(payload) > MAX_BUFFER_SIZE:
            raise ValueError(f"Payload size {len(payload)} exceeds maximum {MAX_BUFFER_SIZE} bytes")

        try:
            text = payload.decode("utf-8").strip()
        except UnicodeDecodeError as e:
            raise ValueError(f"Invalid UTF-8 payload: {e}")

        if not text:
            raise ValueError("Empty request payload")

        try:
            data = json.loads(text, object_pairs_hook=_check_duplicate_keys)
        except Exception as e:
            raise ValueError(f"JSON parsing error: {e}")

        if not isinstance(data, dict):
            raise ValueError("Request payload must be a JSON object")

        # Unknown keys rejection
        extra_keys = set(data.keys()) - ALLOWED_REQUEST_KEYS
        if extra_keys:
            raise ValueError(f"Undefined keys rejected: {extra_keys}")

        # request_id validation
        if "request_id" not in data:
            raise ValueError("Missing mandatory field: request_id")
        req_id = data["request_id"]
        if type(req_id) is not int or isinstance(req_id, bool):
            raise ValueError("Field 'request_id' must be an integer, not boolean or string")
        if not (1 <= req_id <= (2**63 - 1)):
            raise ValueError(f"Field 'request_id' out of range [1, 2^63-1]: {req_id}")

        # op validation
        if "op" not in data:
            raise ValueError("Missing mandatory field: op")
        op = data["op"]
        if not isinstance(op, str) or op not in ALLOWED_OPS:
            raise ValueError(f"Invalid op: {op}. Allowed: {ALLOWED_OPS}")

        # Boolean field validation across all ops
        for field in ("physical_estop", "process_fault"):
            if field in data and data[field] is not None:
                val = data[field]
                if type(val) is not bool:
                    raise ValueError(f"Field '{field}' must be a boolean (true/false), got {type(val).__name__}")

        # op-specific validations
        if op == "SET_FAULT":
            if "mode" not in data:
                raise ValueError("Field 'mode' is mandatory when op is SET_FAULT")
            mode = data["mode"]
            if not isinstance(mode, str) or mode not in ALLOWED_MODES:
                raise ValueError(f"Invalid mode: {mode}. Allowed: {ALLOWED_MODES}")

            delay_ms = data.get("delay_ms")
            if mode == "DELAY":
                if delay_ms is None:
                    raise ValueError("Field 'delay_ms' is mandatory when mode is DELAY")
                if type(delay_ms) is not int or isinstance(delay_ms, bool):
                    raise ValueError("Field 'delay_ms' must be an integer")
                if not (1 <= delay_ms <= 1000):
                    raise ValueError(f"Field 'delay_ms' out of range [1, 1000]: {delay_ms}")
            else:
                if delay_ms is not None:
                    raise ValueError(f"Field 'delay_ms' must be null or omitted when mode is {mode}")

        elif op == "SET_INPUTS":
            pass

        elif op == "GET_STATUS":
            pass

        return data

    @staticmethod
    def encode_response(response: Dict[str, Any]) -> bytes:
        """Serialize response dict to UTF-8 JSON line."""
        # Ensure message is bounded to 160 bytes
        msg = str(response.get("message", ""))
        msg_bytes = msg.encode("utf-8")
        if len(msg_bytes) > 160:
            msg = msg_bytes[:160].decode("utf-8", errors="ignore")
            response = dict(response)
            response["message"] = msg

        encoded = json.dumps(response, separators=(",", ":")) + "\n"
        payload = encoded.encode("utf-8")
        if len(payload) > MAX_BUFFER_SIZE:
            raise ValueError(f"Serialized response size {len(payload)} exceeds {MAX_BUFFER_SIZE} bytes")
        return payload

    @staticmethod
    def build_error_response(request_id: Optional[int], message: str) -> Dict[str, Any]:
        """Build standard error response DTO."""
        return {
            "request_id": request_id,
            "ok": False,
            "error_code": 1,
            "message": message,
            "applied_ns": None,
            "mode": "UNKNOWN",
            "delay_ms": None,
            "physical_estop": False,
            "process_fault": False,
        }

    @staticmethod
    def build_success_response(
        request_id: int,
        message: str,
        applied_ns: Optional[int],
        mode: str,
        delay_ms: Optional[int] = None,
        physical_estop: bool = False,
        process_fault: bool = False,
    ) -> Dict[str, Any]:
        """Build standard success response DTO."""
        return {
            "request_id": request_id,
            "ok": True,
            "error_code": 0,
            "message": message,
            "applied_ns": applied_ns if applied_ns is not None else time.monotonic_ns(),
            "mode": mode,
            "delay_ms": delay_ms,
            "physical_estop": physical_estop,
            "process_fault": process_fault,
        }
