/**
 * HMI Data Service & WebSocket Bridge Client.
 * Connects to FastAPI WebSocket endpoint /ws or falls back gracefully to REST / simulation.
 */

import {
  CMD_RESET,
  CMD_SET_SETPOINT,
  CMD_START,
  CMD_STOP,
  FaultMode,
  PlcState,
  PresentationState,
  SafetyAlarm,
} from '../types';

export type StateCallback = (
  pres: PresentationState,
  raw?: PlcState,
  alarm?: SafetyAlarm
) => void;

interface PendingRequest {
  resolve: (value: any) => void;
  reject: (reason: any) => void;
  timeoutId: number;
}

class HmiDataService {
  private ws: WebSocket | null = null;
  private stateListeners: Set<StateCallback> = new Set();
  private reconnectTimer: number | null = null;
  private isConnected = false;
  private isSimulating = false;
  private simInterval: number | null = null;
  private nextRequestId = 1;
  private pendingRequests: Map<number, PendingRequest> = new Map();

  // Local fallback simulation state
  private localState = {
    running: false,
    ready: true,
    physicalEstop: false,
    alarmActive: false,
    sensorRaw: 512,
    setpointRaw: 500,
    heartbeat: 1000,
    rttNs: 420000,
    jitterNs: 12000,
    faultMode: 'NORMAL' as FaultMode,
  };

  constructor() {
    this.connect();
  }

  public subscribe(cb: StateCallback): () => void {
    this.stateListeners.add(cb);
    return () => this.stateListeners.delete(cb);
  }

  private connect(): void {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host || 'localhost:8000';
    const url = `${protocol}//${host}/ws`;

    try {
      this.ws = new WebSocket(url);

      this.ws.onopen = () => {
        this.isConnected = true;
        this.stopLocalSimulation();
      };

      this.ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type === 'STATE_UPDATE' && data.presentation) {
            this.notify(data.presentation, data.raw, data.alarm);
          } else if (
            data.type === 'COMMAND_RESULT' ||
            data.type === 'CLEAR_FAULT_RESULT' ||
            data.type === 'FAULT_INJECTION_RESULT'
          ) {
            const reqId = data.request_id;
            if (reqId && this.pendingRequests.has(reqId)) {
              const pending = this.pendingRequests.get(reqId)!;
              window.clearTimeout(pending.timeoutId);
              this.pendingRequests.delete(reqId);
              pending.resolve(data.result);
            } else if (this.pendingRequests.size > 0) {
              const firstKey = this.pendingRequests.keys().next().value;
              if (firstKey !== undefined) {
                const pending = this.pendingRequests.get(firstKey)!;
                window.clearTimeout(pending.timeoutId);
                this.pendingRequests.delete(firstKey);
                pending.resolve(data.result);
              }
            }
          }
        } catch {
          // Ignore invalid messages
        }
      };

      this.ws.onclose = () => {
        this.isConnected = false;
        this.startLocalSimulation();
        this.scheduleReconnect();
      };

      this.ws.onerror = () => {
        this.isConnected = false;
        this.startLocalSimulation();
      };
    } catch {
      this.isConnected = false;
      this.startLocalSimulation();
      this.scheduleReconnect();
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.isConnected) {
        this.connect();
      }
    }, 3000);
  }

  private notify(pres: PresentationState, raw?: PlcState, alarm?: SafetyAlarm): void {
    this.stateListeners.forEach((cb) => cb(pres, raw, alarm));
  }

  private async fallbackRestRequest(type: string, payload: any): Promise<any> {
    const origin = window.location.origin || 'http://localhost:8000';
    try {
      if (type === 'COMMAND') {
        const resp = await fetch(`${origin}/api/trigger_command`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ command: payload.command, value: payload.value || 0 }),
        });
        if (resp.ok) return await resp.json();
      } else if (type === 'CLEAR_FAULT') {
        const resp = await fetch(`${origin}/api/clear_fault`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ force_clear: payload.force_clear || false }),
        });
        if (resp.ok) return await resp.json();
      } else if (type === 'FAULT_INJECTION') {
        const resp = await fetch(`${origin}/api/fault_injection`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload),
        });
        if (resp.ok) return await resp.json();
      }
    } catch {
      // Fall through to simulation
    }
    return null;
  }

  private sendWsRequest(type: string, payload: any): Promise<any> {
    return new Promise((resolve, reject) => {
      const requestId = this.nextRequestId++;
      const timeoutId = window.setTimeout(() => {
        this.pendingRequests.delete(requestId);
        this.fallbackRestRequest(type, payload)
          .then((res) => {
            if (res !== null) resolve(res);
            else resolve({ success: false, message: 'Request timeout' });
          })
          .catch(reject);
      }, 3000);

      this.pendingRequests.set(requestId, { resolve, reject, timeoutId });

      try {
        this.ws!.send(JSON.stringify({ type, request_id: requestId, ...payload }));
      } catch (e) {
        window.clearTimeout(timeoutId);
        this.pendingRequests.delete(requestId);
        this.fallbackRestRequest(type, payload)
          .then((res) => {
            if (res !== null) resolve(res);
            else reject(e);
          })
          .catch(reject);
      }
    });
  }

  public sendCommand(cmd: number, value: number = 0): Promise<any> {
    if (this.isConnected && this.ws && this.ws.readyState === WebSocket.OPEN) {
      return this.sendWsRequest('COMMAND', { command: cmd, value });
    }

    // Local simulation fallback
    const s = this.localState;
    if (s.physicalEstop) {
      return Promise.resolve({
        success: false,
        outcome: 3, // REJECTED
        error_code: 6, // ERR_INTERLOCK_ACTIVE
        message: 'Physical E-Stop is active; command rejected',
      });
    }

    if (cmd === CMD_START) {
      if (s.alarmActive) {
        return Promise.resolve({
          success: false,
          outcome: 3, // REJECTED
          error_code: 5, // ERR_ALARM_ACTIVE
          message: 'Safety alarm is active; cannot start equipment',
        });
      }
      s.running = true;
      s.ready = false;
      this.publishSimulatedState();
      return Promise.resolve({ success: true, outcome: 1, error_code: 0, message: 'Command confirmed' });
    } else if (cmd === CMD_STOP) {
      s.running = false;
      s.ready = true;
      this.publishSimulatedState();
      return Promise.resolve({ success: true, outcome: 1, error_code: 0, message: 'Command confirmed' });
    } else if (cmd === CMD_RESET) {
      s.alarmActive = false;
      s.ready = true;
      s.running = false;
      this.publishSimulatedState();
      return Promise.resolve({ success: true, outcome: 1, error_code: 0, message: 'Command confirmed' });
    } else if (cmd === CMD_SET_SETPOINT) {
      if (value < 0 || value > 1000) {
        return Promise.resolve({
          success: false,
          outcome: 3, // REJECTED
          error_code: 1, // ERR_INVALID_ARGUMENT
          message: `Setpoint ${value} out of range [0, 1000]`,
        });
      }
      s.setpointRaw = value;
      this.publishSimulatedState();
      return Promise.resolve({ success: true, outcome: 1, error_code: 0, message: 'Command confirmed' });
    }

    this.publishSimulatedState();
    return Promise.resolve({ success: true, outcome: 1, error_code: 0 });
  }

  public clearFault(forceClear: boolean = false): Promise<any> {
    if (this.isConnected && this.ws && this.ws.readyState === WebSocket.OPEN) {
      return this.sendWsRequest('CLEAR_FAULT', { force_clear: forceClear });
    }

    if (this.localState.physicalEstop && !forceClear) {
      return Promise.resolve({
        success: false,
        error_code: 6, // ERR_INTERLOCK_ACTIVE
        message: 'Physical E-Stop is asserted; ClearFault rejected (hardware interlock)',
      });
    }

    this.localState.alarmActive = false;
    this.localState.ready = true;
    this.localState.running = false;
    this.localState.faultMode = 'NORMAL';
    this.publishSimulatedState();
    return Promise.resolve({ success: true, error_code: 0, message: 'Fault latch successfully cleared' });
  }

  public injectFault(
    mode: FaultMode,
    delayMs?: number,
    physicalEstop?: boolean,
    processFault?: boolean
  ): Promise<any> {
    if (this.isConnected && this.ws && this.ws.readyState === WebSocket.OPEN) {
      return this.sendWsRequest('FAULT_INJECTION', {
        mode,
        delay_ms: delayMs,
        physical_estop: physicalEstop,
        process_fault: processFault,
      });
    }

    // Local fallback
    this.localState.faultMode = mode;
    if (physicalEstop !== undefined) {
      this.localState.physicalEstop = physicalEstop;
      if (physicalEstop) {
        this.localState.alarmActive = true;
        this.localState.running = false;
        this.localState.ready = false;
      }
    }
    if (processFault !== undefined && processFault) {
      this.localState.alarmActive = true;
      this.localState.running = false;
      this.localState.ready = false;
    }
    if (mode === 'DROP' || mode === 'DISCONNECT' || mode === 'FREEZE') {
      this.localState.alarmActive = true;
      this.localState.running = false;
      this.localState.ready = false;
    } else if (mode === 'NORMAL') {
      if (!this.localState.physicalEstop) {
        this.localState.alarmActive = false;
      }
    }

    this.publishSimulatedState();
    return Promise.resolve({ success: true, mode });
  }

  private startLocalSimulation(): void {
    if (this.isSimulating) return;
    this.isSimulating = true;

    this.simInterval = window.setInterval(() => {
      const s = this.localState;
      s.heartbeat = (s.heartbeat + 1) % 65536;

      if (s.running) {
        const drift = Math.sin(Date.now() / 1500) * 45;
        const noise = (Math.random() - 0.5) * 6;
        s.sensorRaw = Math.round(s.setpointRaw + drift + noise);
      }

      this.publishSimulatedState();
    }, 100);
  }

  private stopLocalSimulation(): void {
    if (this.simInterval) {
      clearInterval(this.simInterval);
      this.simInterval = null;
    }
    this.isSimulating = false;
  }

  private publishSimulatedState(): void {
    const s = this.localState;
    const isAlarm = s.alarmActive || s.physicalEstop;

    let statusText = 'READY';
    let statusColor = 'text-[#90CAF9]';
    let statusBadgeBg = 'bg-[#1565C0]/25 border-[#1E88E5]';

    if (s.physicalEstop) {
      statusText = 'PHYSICAL E-STOP';
      statusColor = 'text-[#EF5350]';
      statusBadgeBg = 'bg-[#C62828]/25 border-[#E53935] shadow-[0_0_15px_rgba(229,57,53,0.3)]';
    } else if (isAlarm) {
      statusText = 'COMM FAULT / LATCHED';
      statusColor = 'text-[#FF7043]';
      statusBadgeBg = 'bg-[#D84315]/25 border-[#F4511E]';
    } else if (s.running) {
      statusText = 'RUNNING';
      statusColor = 'text-[#81C784]';
      statusBadgeBg = 'bg-[#2E7D32]/25 border-[#388E3C] shadow-[0_0_12px_rgba(56,142,60,0.2)]';
    }

    const pres: PresentationState = {
      statusText,
      statusColor,
      statusBadgeBg,
      stationId: 1,
      sensorVal: s.sensorRaw,
      sensorPct: Math.min(100, Math.max(0, (s.sensorRaw / 1000) * 100)),
      setpointVal: s.setpointRaw,
      isInRange: s.sensorRaw >= 400 && s.sensorRaw <= 600,
      rttMs: 0.42,
      jitterMs: 0.012,
      heartbeat: s.heartbeat,
      isAlarm,
      alarmCause: isAlarm ? 4 : 0,
      alarmCauseText: isAlarm ? 'Interlock' : 'None',
      errorCode: 0,
      errorCodeText: 'OK',
      canStart: s.ready && !s.running && !isAlarm,
      canStop: s.running,
      canClear: isAlarm && !s.physicalEstop,
      canSetSetpoint: !s.physicalEstop,
      alarmGuideMsg: '',
      physicalEstop: s.physicalEstop,
      linkState: 2,
      linkStateText: 'OPERATIONAL',
      ioErrorCount: 0,
      consecutiveFailures: 0,
      busFreqHz: 50.0,
    };

    this.notify(pres);
  }
}

export const hmiService = new HmiDataService();
