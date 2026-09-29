# 🖥️ ROS 2 Modbus Industrial Gateway - HMI System

Production-ready, ISA-101 Situational Awareness HMI System with cross-platform OS portability (Linux Docker edge environments and Windows).

## 1. Architecture & Module Decoupling

- **Module Decoupling:** Completely decoupled from the C++ Gateway core process (`gateway_buffer.hpp`, `safety_monitor.hpp`, etc.). The only interaction point is the standard ROS 2 DDS middleware interface (`/plc/state`, `/safety/alarm`, `/plc/trigger_command`, `/plc/clear_fault`).
- **OS Portability:** Runnable seamlessly across Linux (Docker edge environments) and Windows without platform lock-in.
- **FastAPI + WebSocket Bridge (`hmi.bridge_server`):**
  - Connects to live ROS 2 node (`rclpy`) if running in ROS 2 environment.
  - Automatically falls back to high-fidelity simulated adapter (`MockHmiAdapter`) on non-ROS platforms (Windows, dev laptops, CI).
  - Streams 10Hz decimated presentation telemetry over WebSocket (`/ws`).
  - Directly serves pre-built React/TypeScript dashboard from `/` and `/assets`.

## 2. UI/UX Specifications (ISA-101 Situational Awareness)

- **1-Second Situational Awareness:** Calm neutral slate palette (`#1E1F22`, `#2B2D30`, `#3E4247`). Color is reserved exclusively for abnormal events (Red for E-Stop, Amber for Alarm/Warning, Green only for nominal running).
- **Visual Hierarchy:**
  1. **Top Safety Banner:** Latched Alarms, E-Stop alert, and 2-step actionable operator recovery guide (Step 1: Release physical button -> Step 2: HMI software reset).
  2. **Primary Machine State:** 1-second situational awareness badge (READY, RUNNING, PHYSICAL E-STOP, COMM FAULT, IDLE).
  3. **Process Variable & Setpoint Control:**
     - ISA-101 In-Range Analog Bar (0~1000 scale, shaded 400~600 normal band, needle pointer).
     - Setpoint Control Card with slider, numeric input, presets (0, 400, 500, 600, 1000), and boundary range validation.
  4. **Modbus/ROS Comm Health Metrics:** FC03 Read RTT (<5ms SLA), Polling Jitter (<2ms SLA), Heartbeat counter, Link state, DDS Bus frequency.
  5. **Dynamic i18n Language Toggle:** `[ KO | EN ]` toggle switch with instant translation for all labels, tooltips, alarm causes, and error codes.
  6. **Context-Aware Tooltips:** Hover tooltips across all interactive controls, data fields, and disabled buttons explaining operational reasons.

## 3. Engineering Test Bench (Tier 1 & Tier 2)

Dedicated diagnostic panel with clear demarcation of safety boundaries:
- **Tier 1 (Real-Time Inline Injection):**
  - Hardware E-Stop toggle (assert/release)
  - Process Fault toggle
  - Setpoint boundary verification (neg -1, 0, 500, 1000, overflow 1001)
  - 1-Shot ClearFault RPC invocation
- **Tier 2 (Benchmark & Batch Fault Scenarios):**
  - `DROP` (Modbus packet drop / 3 consecutive timeouts)
  - `DELAY` (30ms latency injection to test SLA violation)
  - `DISCONNECT` (TCP socket disconnection)
  - `FREEZE` (PLC scan loop freeze for stale heartbeat test)
  - Real-time RTT telemetry and test outcome feedback.

## 4. Quick Start

### Running with Python (FastAPI + WebSocket + Frontend)
```bash
# Production Launch (Auto-detects ROS 2 or runs Mock adapter)
python -m hmi.run --host 0.0.0.0 --port 8000

# Force Mock Adapter mode
python -m hmi.run --host 0.0.0.0 --port 8000 --adapter mock

# Access HMI in browser:
http://localhost:8000
```

### Running Frontend Development Server (Vite)
```bash
cd hmi_web_prototype
npm install
npm run dev
# Vite dev server starts at http://localhost:3000
```

### Building Frontend
```bash
cd hmi_web_prototype
npm run build
# Compiles to hmi_web_prototype/dist/
```

### Running Test Suite
```bash
python -m pytest tests/test_hmi.py
```
