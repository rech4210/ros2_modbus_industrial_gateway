"""Unit tests for ControlProtocol and Fault Proxy control protocol."""

import json
import pytest
from mock_plc.control_protocol import ControlProtocol, MAX_BUFFER_SIZE


def test_valid_set_fault_request():
    payload = json.dumps({
        "request_id": 1001,
        "op": "SET_FAULT",
        "mode": "DROP",
        "delay_ms": None,
        "physical_estop": False,
        "process_fault": False,
    }).encode("utf-8") + b"\n"

    parsed = ControlProtocol.parse_request(payload)
    assert parsed["request_id"] == 1001
    assert parsed["op"] == "SET_FAULT"
    assert parsed["mode"] == "DROP"


def test_valid_delay_request():
    payload = json.dumps({
        "request_id": 1002,
        "op": "SET_FAULT",
        "mode": "DELAY",
        "delay_ms": 200,
    }).encode("utf-8") + b"\n"

    parsed = ControlProtocol.parse_request(payload)
    assert parsed["request_id"] == 1002
    assert parsed["mode"] == "DELAY"
    assert parsed["delay_ms"] == 200


def test_delay_without_delay_ms_rejected():
    payload = json.dumps({
        "request_id": 1003,
        "op": "SET_FAULT",
        "mode": "DELAY",
    }).encode("utf-8") + b"\n"

    with pytest.raises(ValueError, match="mandatory when mode is DELAY"):
        ControlProtocol.parse_request(payload)


def test_delay_ms_out_of_range_rejected():
    payload = json.dumps({
        "request_id": 1004,
        "op": "SET_FAULT",
        "mode": "DELAY",
        "delay_ms": 1500,
    }).encode("utf-8") + b"\n"

    with pytest.raises(ValueError, match="out of range"):
        ControlProtocol.parse_request(payload)


def test_duplicate_keys_rejected():
    # Construct raw JSON string with duplicate key
    raw = b'{"request_id": 1005, "request_id": 1006, "op": "GET_STATUS"}\n'
    with pytest.raises(ValueError, match="Duplicate JSON key"):
        ControlProtocol.parse_request(raw)


def test_unknown_keys_rejected():
    payload = json.dumps({
        "request_id": 1007,
        "op": "GET_STATUS",
        "extra_unknown": 42,
    }).encode("utf-8") + b"\n"

    with pytest.raises(ValueError, match="Undefined keys rejected"):
        ControlProtocol.parse_request(payload)


def test_boolean_cannot_be_integer():
    payload1 = b'{"request_id": 1008, "op": "SET_INPUTS", "physical_estop": 1}\n'
    with pytest.raises(ValueError, match="must be a boolean"):
        ControlProtocol.parse_request(payload1)

    payload2 = b'{"request_id": 1009, "op": "SET_FAULT", "mode": "DROP", "physical_estop": 1}\n'
    with pytest.raises(ValueError, match="must be a boolean"):
        ControlProtocol.parse_request(payload2)

    payload3 = b'{"request_id": 1010, "op": "SET_INPUTS", "process_fault": 0}\n'
    with pytest.raises(ValueError, match="must be a boolean"):
        ControlProtocol.parse_request(payload3)


def test_oversized_payload_rejected():
    huge_data = "a" * (MAX_BUFFER_SIZE + 10)
    raw = f'{{"request_id": 1009, "op": "GET_STATUS", "mode": "{huge_data}"}}\n'.encode("utf-8")
    with pytest.raises(ValueError, match="exceeds maximum"):
        ControlProtocol.parse_request(raw)


def test_build_success_and_error_responses():
    err = ControlProtocol.build_error_response(1010, "Test error message")
    assert err["request_id"] == 1010
    assert err["ok"] is False
    assert err["error_code"] == 1
    encoded_err = ControlProtocol.encode_response(err)
    assert encoded_err.endswith(b"\n")
    assert len(encoded_err) <= MAX_BUFFER_SIZE

    succ = ControlProtocol.build_success_response(
        request_id=1011,
        message="Success",
        applied_ns=123456789,
        mode="NORMAL",
    )
    assert succ["request_id"] == 1011
    assert succ["ok"] is True
    assert succ["error_code"] == 0
    assert succ["mode"] == "NORMAL"
    encoded_succ = ControlProtocol.encode_response(succ)
    assert encoded_succ.endswith(b"\n")
