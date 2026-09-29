"""FastAPI + WebSocket ROS 2 Gateway HMI Bridge Server.

Serves REST endpoints, 10Hz decimated WebSocket telemetry, and static frontend dashboard.
Runs seamlessly across Linux (Docker edge environments) and Windows.
"""

import asyncio
import json
import logging
import os
from contextlib import asynccontextmanager
from typing import Any, Dict, List, Optional, Set

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from hmi.adapters import BaseHmiAdapter, MockHmiAdapter, Ros2HmiAdapter
from hmi.constants import CMD_SET_SETPOINT
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

logger = logging.getLogger("hmi.bridge_server")

# Global adapter instance
_adapter: Optional[BaseHmiAdapter] = None
_broadcast_task: Optional[asyncio.Task] = None
_active_websockets: Set[WebSocket] = set()


def create_adapter(mode: str = "auto") -> BaseHmiAdapter:
    """Instantiate adapter based on mode ('auto', 'ros2', 'mock')."""
    if mode == "ros2":
        return Ros2HmiAdapter()
    elif mode == "mock":
        return MockHmiAdapter()
    else:  # auto
        try:
            import rclpy  # Check if ROS 2 python is installed
            logger.info("rclpy found, attempting Ros2HmiAdapter")
            return Ros2HmiAdapter()
        except ImportError:
            logger.info("rclpy not found, using MockHmiAdapter for cross-platform execution")
            return MockHmiAdapter()


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Lifecycle manager to initialize and teardown adapter and broadcast loop."""
    global _adapter, _broadcast_task
    adapter_mode = os.environ.get("HMI_ADAPTER_MODE", "auto")
    _adapter = create_adapter(adapter_mode)
    await _adapter.start()
    _broadcast_task = asyncio.create_task(_state_broadcast_loop())
    logger.info("HMI Bridge Server started successfully with adapter: %s", type(_adapter).__name__)
    yield
    if _broadcast_task:
        _broadcast_task.cancel()
        try:
            await _broadcast_task
        except asyncio.CancelledError:
            pass
    if _adapter:
        await _adapter.stop()
    logger.info("HMI Bridge Server shut down safely.")


app = FastAPI(
    title="ROS 2 Modbus Gateway HMI Bridge",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


async def _state_broadcast_loop() -> None:
    """10Hz (100ms) decimated broadcast loop streaming ViewModel to all connected WebSockets."""
    try:
        while True:
            await asyncio.sleep(0.1)  # 10Hz
            if not _active_websockets or _adapter is None:
                continue

            pres = _adapter.get_presentation_state()
            raw = _adapter.get_plc_state()
            alarm = _adapter.get_safety_alarm()

            payload = json.dumps({
                "type": "STATE_UPDATE",
                "presentation": pres.model_dump(),
                "raw": raw.model_dump(),
                "alarm": alarm.model_dump(),
            })

            dead_sockets = set()
            for ws in list(_active_websockets):
                try:
                    await ws.send_text(payload)
                except Exception:
                    dead_sockets.add(ws)

            for ws in dead_sockets:
                _active_websockets.discard(ws)

    except asyncio.CancelledError:
        pass


@app.get("/api/health")
async def get_health() -> Dict[str, Any]:
    """Health check endpoint."""
    return {
        "status": "ok",
        "adapter": type(_adapter).__name__ if _adapter else "none",
        "connected_clients": len(_active_websockets),
    }


@app.get("/api/state")
async def get_state() -> Dict[str, Any]:
    """Fetch current raw PLC state, SafetyAlarm, and Presentation ViewModel."""
    if not _adapter:
        raise HTTPException(status_code=503, detail="Adapter not initialized")
    return {
        "presentation": _adapter.get_presentation_state().model_dump(),
        "raw": _adapter.get_plc_state().model_dump(),
        "alarm": _adapter.get_safety_alarm().model_dump(),
    }


@app.post("/api/trigger_command", response_model=TriggerCommandResponse)
async def post_trigger_command(req: TriggerCommandRequest) -> TriggerCommandResponse:
    """Trigger a machine control command (START=1, STOP=2, RESET=3, SET_SETPOINT=4)."""
    if not _adapter:
        raise HTTPException(status_code=503, detail="Adapter not initialized")
    return await _adapter.trigger_command(req)


@app.post("/api/clear_fault", response_model=ClearFaultResponse)
async def post_clear_fault(req: ClearFaultRequest) -> ClearFaultResponse:
    """Clear latched safety alarm / fault."""
    if not _adapter:
        raise HTTPException(status_code=503, detail="Adapter not initialized")
    return await _adapter.clear_fault(req)


@app.post("/api/fault_injection")
async def post_fault_injection(req: FaultInjectionRequest) -> Dict[str, Any]:
    """Test Bench diagnostic fault injection."""
    if not _adapter:
        raise HTTPException(status_code=503, detail="Adapter not initialized")
    return await _adapter.inject_fault(req)


@app.post("/api/setpoint", response_model=TriggerCommandResponse)
async def post_setpoint(data: Dict[str, int]) -> TriggerCommandResponse:
    """Convenience endpoint for setting setpoint."""
    if not _adapter:
        raise HTTPException(status_code=503, detail="Adapter not initialized")
    val = data.get("value", 500)
    req = TriggerCommandRequest(command=CMD_SET_SETPOINT, value=val)
    return await _adapter.trigger_command(req)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    """Bi-directional WebSocket for real-time telemetry streaming and command execution."""
    await websocket.accept()
    _active_websockets.add(websocket)
    logger.info("New WebSocket client connected. Active: %d", len(_active_websockets))

    # Send immediate state update on connect
    if _adapter:
        initial_payload = json.dumps({
            "type": "STATE_UPDATE",
            "presentation": _adapter.get_presentation_state().model_dump(),
            "raw": _adapter.get_plc_state().model_dump(),
            "alarm": _adapter.get_safety_alarm().model_dump(),
        })
        await websocket.send_text(initial_payload)

    try:
        while True:
            text = await websocket.receive_text()
            try:
                msg = json.loads(text)
                msg_type = msg.get("type", "")

                req_id = msg.get("request_id")

                if msg_type == "COMMAND":
                    cmd = int(msg.get("command", 0))
                    val = int(msg.get("value", 0))
                    res = await _adapter.trigger_command(TriggerCommandRequest(command=cmd, value=val))
                    resp = {
                        "type": "COMMAND_RESULT",
                        "result": res.model_dump(),
                    }
                    if req_id is not None:
                        resp["request_id"] = req_id
                    await websocket.send_text(json.dumps(resp))

                elif msg_type == "CLEAR_FAULT":
                    force = bool(msg.get("force_clear", False))
                    res = await _adapter.clear_fault(ClearFaultRequest(force_clear=force))
                    resp = {
                        "type": "CLEAR_FAULT_RESULT",
                        "result": res.model_dump(),
                    }
                    if req_id is not None:
                        resp["request_id"] = req_id
                    await websocket.send_text(json.dumps(resp))

                elif msg_type == "FAULT_INJECTION":
                    req = FaultInjectionRequest(
                        mode=msg.get("mode", "NORMAL"),
                        delay_ms=msg.get("delay_ms"),
                        physical_estop=msg.get("physical_estop"),
                        process_fault=msg.get("process_fault"),
                    )
                    res = await _adapter.inject_fault(req)
                    resp = {
                        "type": "FAULT_INJECTION_RESULT",
                        "result": res,
                    }
                    if req_id is not None:
                        resp["request_id"] = req_id
                    await websocket.send_text(json.dumps(resp))

                elif msg_type == "PING":
                    await websocket.send_text(json.dumps({"type": "PONG", "timestamp": msg.get("timestamp")}))

            except json.JSONDecodeError:
                await websocket.send_text(json.dumps({"type": "ERROR", "message": "Invalid JSON"}))
            except Exception as e:
                await websocket.send_text(json.dumps({"type": "ERROR", "message": str(e)}))

    except WebSocketDisconnect:
        _active_websockets.discard(websocket)
        logger.info("WebSocket client disconnected. Active: %d", len(_active_websockets))
    except Exception as e:
        _active_websockets.discard(websocket)
        logger.warning("WebSocket error: %s", e)


# Serve Static Assets if frontend dist exists
FRONTEND_DIST_PATHS = [
    os.path.join(os.path.dirname(__file__), "..", "hmi_web_prototype", "dist"),
    os.path.join(os.path.dirname(__file__), "dist"),
]

for dist_path in FRONTEND_DIST_PATHS:
    abs_path = os.path.abspath(dist_path)
    if os.path.exists(abs_path) and os.path.isdir(abs_path):
        app.mount("/assets", StaticFiles(directory=os.path.join(abs_path, "assets")), name="assets")

        @app.get("/")
        async def serve_index():
            return FileResponse(os.path.join(abs_path, "index.html"))

        logger.info("Mounted frontend static build from: %s", abs_path)
        break
