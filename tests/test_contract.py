"""Automated Architectural Contract Verification Suite.

Verifies 1:1 parity between C++ domain definitions (register_map.hpp, types.hpp)
and Python mock/test definitions (mock_plc/constants.py).

Prevents multi-language drift without requiring heavyweight code generation tooling.
"""

import re
import sys
from pathlib import Path
import pytest

# Ensure repository root is in python path
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

import mock_plc.constants as py_const

REGISTER_MAP_HEADER = REPO_ROOT / "include" / "ros2_modbus_gateway" / "register_map.hpp"
TYPES_HEADER = REPO_ROOT / "include" / "ros2_modbus_gateway" / "types.hpp"


def parse_cpp_constants(header_path: Path) -> dict[str, int]:
    """Parse constexpr uint16_t NAME = VALUE; from C++ header."""
    assert header_path.exists(), f"Header file not found: {header_path}"
    content = header_path.read_text(encoding="utf-8")
    pattern = re.compile(r"constexpr\s+uint16_t\s+([A-Za-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;")
    constants = {}
    for match in pattern.finditer(content):
        name = match.group(1)
        raw_val = match.group(2)
        val = int(raw_val, 16) if raw_val.startswith("0x") else int(raw_val)
        constants[name] = val
    return constants


def parse_cpp_enum(header_path: Path, enum_name: str) -> dict[str, int]:
    """Parse an enum class with explicit values from C++ header."""
    assert header_path.exists(), f"Header file not found: {header_path}"
    content = header_path.read_text(encoding="utf-8")
    enum_pattern = re.compile(rf"enum\s+class\s+{enum_name}\s*(?::\s*[A-Za-z0-9_]+)?\s*\{{([^}}]+)\}};", re.MULTILINE)
    match = enum_pattern.search(content)
    assert match is not None, f"Enum {enum_name} not found in {header_path}"
    body = match.group(1)
    
    entries = {}
    entry_pattern = re.compile(r"([A-Za-z0-9_]+)\s*=\s*(\d+)")
    for em in entry_pattern.finditer(body):
        entries[em.group(1)] = int(em.group(2))
    return entries


def test_register_map_header_exists():
    assert REGISTER_MAP_HEADER.exists()


def test_contract_all_register_map_constants_match():
    """Verify that every constant in register_map.hpp has an exact twin in mock_plc.constants."""
    cpp_constants = parse_cpp_constants(REGISTER_MAP_HEADER)
    assert len(cpp_constants) >= 20, f"Expected >= 20 constants in {REGISTER_MAP_HEADER}, found {len(cpp_constants)}"

    mismatches = []
    missing_in_python = []

    for name, cpp_val in cpp_constants.items():
        if not hasattr(py_const, name):
            missing_in_python.append(name)
        else:
            py_val = getattr(py_const, name)
            if py_val != cpp_val:
                mismatches.append(f"{name}: C++={cpp_val} (hex: {hex(cpp_val)}) != Python={py_val} (hex: {hex(py_val)})")

    assert not missing_in_python, f"Constants missing in mock_plc.constants: {missing_in_python}"
    assert not mismatches, f"Constant value mismatches between C++ and Python:\n" + "\n".join(mismatches)


def test_contract_commands_match():
    """Verify that Command enum in types.hpp matches CMD_* in mock_plc.constants."""
    cpp_commands = parse_cpp_enum(TYPES_HEADER, "Command")
    assert "START" in cpp_commands
    assert "STOP" in cpp_commands
    assert "RESET" in cpp_commands
    assert "SET_SETPOINT" in cpp_commands

    for cmd_name, cmd_val in cpp_commands.items():
        py_symbol = f"CMD_{cmd_name}"
        assert hasattr(py_const, py_symbol), f"Missing {py_symbol} in mock_plc.constants"
        assert getattr(py_const, py_symbol) == cmd_val, f"Mismatch for {py_symbol}: C++={cmd_val}, Python={getattr(py_const, py_symbol)}"


def test_contract_error_codes_match():
    """Verify that ErrorCode enum in types.hpp matches ERR_* in mock_plc.constants."""
    cpp_errors = parse_cpp_enum(TYPES_HEADER, "ErrorCode")
    assert len(cpp_errors) >= 20

    for err_name, err_val in cpp_errors.items():
        py_symbol = f"ERR_{err_name}"
        assert hasattr(py_const, py_symbol), f"Missing {py_symbol} in mock_plc.constants"
        assert getattr(py_const, py_symbol) == err_val, f"Mismatch for {py_symbol}: C++={err_val}, Python={getattr(py_const, py_symbol)}"


def test_contract_status_flags_bitmasks_are_disjoint():
    """Verify that the 6 status bits occupy mutually disjoint bit positions within 0x003F."""
    bits = [
        py_const.STATUS_BIT_RUN_REQUESTED,
        py_const.STATUS_BIT_READY,
        py_const.STATUS_BIT_RUNNING,
        py_const.STATUS_BIT_PHYSICAL_ESTOP,
        py_const.STATUS_BIT_RESET_REQUESTED,
        py_const.STATUS_BIT_ALARM_TRIPPED,
    ]
    combined = 0
    for b in bits:
        assert (combined & b) == 0, f"Bitmask overlap detected for bit: {hex(b)}"
        combined |= b

    assert combined == 0x003F
    assert (combined & py_const.STATUS_BIT_RESERVED_MASK) == 0


def test_contract_holding_register_indices_are_sequential():
    """Verify that the 6 holding registers have sequential offsets from 0 to 5."""
    offsets = [
        py_const.HR_HEARTBEAT,
        py_const.HR_STATUS_FLAGS,
        py_const.HR_SENSOR_RAW,
        py_const.HR_SETPOINT_RAW,
        py_const.HR_FAULT_CODE,
        py_const.HR_APPLIED_COMMAND_COUNTER,
    ]
    assert offsets == list(range(py_const.HOLDING_REGISTER_COUNT))
