# ROS 2 Modbus Industrial Gateway - HMI System

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

![HMI screen](../docs/assets/hmi_bolt_preview.png)

- **Grey-scale by default:** Mid-grey background (`#CDCDCD`) and grey panels. Normal operation carries no color. Saturated color is reserved for alarm priority: red `#C80000` (priority 1, E-Stop), orange `#D97500` (priority 2, safety alarm / fault / out of range), yellow `#C9A800` (priority 3, marker defined, no trigger yet). Blue `#1F4E9C` marks only the value being edited or the selected option.
- **Not color alone:** Alarm priority is shown by color, shape and number together (red square 1, orange triangle 2, yellow diamond 3). Machine state is told apart by shape and text: solid square = running, hollow square = ready, dashed square = idle.
- **No decorative motion:** No blinking, glow, bounce or emoji. An abnormal state draws a static colored frame around the screen.
- **Visual hierarchy (top to bottom):**
  1. **Alarm summary:** active alarm or "no active alarms", cause, and the 2-step recovery guide (Step 1: release the physical E-Stop, Step 2: HMI reset). The finished step is struck through.
  2. **Unit state:** RUNNING / READY / IDLE / FAULT (latched) / E-STOP.
  3. **Process value (PV) and setpoint (SP):**
     - Analog bar (0-1000 scale, grey 400-600 normal band, pointer, band bracket below the scale).
     - Setpoint slider, numeric input, presets (0, 400, 500, 600, 1000) and range validation. The new value turns blue only while it differs from the live setpoint.
  4. **Communication:** FC03 round trip (< 5 ms), polling jitter (< 2 ms), PLC heartbeat, link state, DDS frequency, failure counters. A row gets an orange marker only when it leaves its limit.
  5. **Operation:** START / STOP / RESET, all grey. RESET gets an orange edge only while an alarm waits for it, and shows `LOCKED` while the E-Stop is pressed. Every action needs a 1.5 s hold in the confirmation dialog.
  6. **Configuration:** static transport, function codes and limits.
- **Language toggle:** `KO | EN` in the title bar switches all labels, tooltips, alarm causes and error codes.
- **Tooltips:** Hover or focus on data labels and buttons, including the reason a button is disabled.
- **Presentation hints from the server:** `statusColor` and `statusBadgeBg` are still sent in the `presentation` payload, but the frontend does not use them. The state is rendered from `physicalEstop`, `isAlarm`, `canStart` and `canStop`.

## 3. Engineering Test Bench (Tier 1 & Tier 2)

Dedicated diagnostic panel, shown or hidden with the `Test Bench` button in the title bar. It is fenced off with a hatched border and a dark title bar so it is never mistaken for the operator screen:
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
