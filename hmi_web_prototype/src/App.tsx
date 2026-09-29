import React, { useState, useEffect } from 'react';
import {
  Activity,
  Clock,
  Globe,
  Radio,
  Sliders,
  Wrench,
  ShieldCheck,
} from 'lucide-react';
import { I18nProvider, useI18n } from './context/I18nContext';
import { TopSafetyBanner } from './components/TopSafetyBanner';
import { MachineStateBadge } from './components/MachineStateBadge';
import { ProcessSensorGauge } from './components/ProcessSensorGauge';
import { SetpointControl } from './components/SetpointControl';
import { CommHealthMetrics } from './components/CommHealthMetrics';
import { OperatorControls } from './components/OperatorControls';
import { ConfirmationModal } from './components/ConfirmationModal';
import { TestBench } from './components/TestBench';
import { Tooltip } from './components/Tooltip';
import { hmiService } from './services/api';
import {
  CMD_RESET,
  CMD_SET_SETPOINT,
  CMD_START,
  CMD_STOP,
  FaultMode,
  PresentationState,
} from './types';

function DashboardContent() {
  const { language, setLanguage, t } = useI18n();

  // Primary Decimated Presentation ViewModel (10Hz)
  const [vm, setVm] = useState<PresentationState>({
    statusText: '운전 준비 완료 (READY)',
    statusColor: 'text-[#90CAF9]',
    statusBadgeBg: 'bg-[#1565C0]/25 border-[#1E88E5]',
    stationId: 1,
    sensorVal: 512,
    sensorPct: 51.2,
    setpointVal: 500,
    isInRange: true,
    rttMs: 0.42,
    jitterMs: 0.012,
    heartbeat: 1000,
    isAlarm: false,
    alarmCause: 0,
    alarmCauseText: 'None',
    errorCode: 0,
    errorCodeText: 'OK',
    canStart: true,
    canStop: false,
    canClear: false,
    canSetSetpoint: true,
    alarmGuideMsg: '',
    physicalEstop: false,
    linkState: 2,
    linkStateText: 'OPERATIONAL',
    ioErrorCount: 0,
    consecutiveFailures: 0,
    busFreqHz: 50.0,
  });

  // UI Interactive States
  const [faultMode, setFaultMode] = useState<FaultMode>('NORMAL');
  const [modalOpen, setModalOpen] = useState(false);
  const [pendingAction, setPendingAction] = useState<'START' | 'STOP' | 'RESET' | null>(null);
  const [showTestBench, setShowTestBench] = useState(true);
  const [lastTestOutcome, setLastTestOutcome] = useState<string>('');

  // Subscribe to HMI WebSocket / Service Stream
  useEffect(() => {
    const unsubscribe = hmiService.subscribe((newVm) => {
      setVm(newVm);
    });
    return () => unsubscribe();
  }, []);

  // Action Dispatchers
  const handleOpenActionModal = (action: 'START' | 'STOP' | 'RESET') => {
    setPendingAction(action);
    setModalOpen(true);
  };

  const handleExecuteConfirmedAction = (action: 'START' | 'STOP' | 'RESET') => {
    if (action === 'START') {
      hmiService.sendCommand(CMD_START);
    } else if (action === 'STOP') {
      hmiService.sendCommand(CMD_STOP);
    } else if (action === 'RESET') {
      hmiService.sendCommand(CMD_RESET);
      hmiService.clearFault(false);
    }
    setModalOpen(false);
    setPendingAction(null);
  };

  const handleApplySetpoint = (val: number) => {
    hmiService.sendCommand(CMD_SET_SETPOINT, val);
  };

  const handleInjectFault = (mode: FaultMode, delayMs?: number) => {
    setFaultMode(mode);
    hmiService.injectFault(mode, delayMs);
    setLastTestOutcome(`Injected ${mode}${delayMs ? ` (${delayMs}ms)` : ''} successfully`);
  };

  const handleToggleEstop = (asserted: boolean) => {
    hmiService.injectFault(faultMode, undefined, asserted, undefined);
    setLastTestOutcome(`Physical E-Stop set to: ${asserted ? 'ACTIVE' : 'RELEASED'}`);
  };

  const handleToggleProcessFault = (asserted: boolean) => {
    hmiService.injectFault(faultMode, undefined, undefined, asserted);
    setLastTestOutcome(`PLC Process Fault set to: ${asserted ? 'ACTIVE' : 'RELEASED'}`);
  };

  const handleTestSetpointBoundary = (val: number) => {
    hmiService.sendCommand(CMD_SET_SETPOINT, val).then((res) => {
      if (!res.success || res.error_code || res.message?.includes('out of range')) {
        setLastTestOutcome(`Boundary Test val=${val}: REJECTED (${res.message || 'ERR_INVALID_ARGUMENT'}) as expected`);
      } else {
        setLastTestOutcome(`Boundary Test val=${val}: CONFIRMED`);
      }
    });
  };

  const handleOneShotClearFault = () => {
    hmiService.clearFault(false).then((res) => {
      setLastTestOutcome(
        `1-Shot ClearFault RPC: ${res.success ? 'CONFIRMED' : `REJECTED (${res.message || 'ERR_INTERLOCK_ACTIVE'})`}`
      );
    });
  };

  return (
    <div
      className={`min-h-screen p-4 md:p-6 transition-all duration-300 ${
        vm.physicalEstop
          ? 'ring-8 ring-red-600/70 ring-inset shadow-[inset_0_0_60px_rgba(220,38,38,0.3)]'
          : vm.isAlarm
          ? 'ring-4 ring-amber-600/50 ring-inset'
          : ''
      }`}
    >
      <div className="max-w-7xl mx-auto space-y-5">
        {/* ================================================================= */}
        {/* Top Header Bar */}
        {/* ================================================================= */}
        <header className="bg-[#2B2D30] border border-[#3E4247] rounded-xl px-5 py-4 flex flex-wrap items-center justify-between gap-4 shadow-lg">
          <div className="flex items-center space-x-3">
            <div className="w-10 h-10 rounded-lg bg-slate-700/60 border border-slate-600 flex items-center justify-center">
              <Activity className="w-5 h-5 text-sky-400" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className="text-lg font-bold text-white tracking-wide">
                  {t.header.title}
                </h1>
                <span className="text-xs px-2 py-0.5 rounded bg-slate-700 text-slate-300 font-mono font-semibold">
                  Station #{vm.stationId.toString().padStart(2, '0')}
                </span>
              </div>
              <p className="text-xs text-slate-400">
                {t.header.subTitle}
              </p>
            </div>
          </div>

          {/* Quick Metrics & Controls Header Right */}
          <div className="flex items-center flex-wrap gap-3 text-xs font-mono">
            <div className="flex items-center gap-1.5 bg-[#1E1F22] px-3 py-1.5 rounded-lg border border-[#3E4247]">
              <Radio className="w-3.5 h-3.5 text-emerald-400 animate-pulse" />
              <span className="text-slate-400">{t.header.ddsBus}</span>
              <span className="text-emerald-400 font-semibold">{vm.busFreqHz.toFixed(1)} Hz</span>
            </div>

            <div className="flex items-center gap-1.5 bg-[#1E1F22] px-3 py-1.5 rounded-lg border border-[#3E4247]">
              <Clock className="w-3.5 h-3.5 text-sky-400" />
              <span className="text-slate-400">{t.header.heartbeat}</span>
              <span className="text-white font-semibold">{vm.heartbeat}</span>
            </div>

            {/* Test Bench Toggle Button */}
            <Tooltip content={t.header.testBenchToggle}>
              <button
                onClick={() => setShowTestBench(!showTestBench)}
                className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg border transition-colors ${
                  showTestBench
                    ? 'bg-amber-500/20 text-amber-300 border-amber-500/50'
                    : 'bg-[#1E1F22] text-slate-400 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <Wrench className="w-3.5 h-3.5" />
                <span className="hidden sm:inline">Test Bench</span>
              </button>
            </Tooltip>

            {/* Dynamic i18n Language Switcher: KO | EN */}
            <div className="flex items-center bg-[#1E1F22] p-1 rounded-lg border border-slate-700">
              <Globe className="w-3.5 h-3.5 text-slate-400 ml-1.5 mr-1" />
              <button
                onClick={() => setLanguage('KO')}
                className={`px-2 py-0.5 rounded text-xs font-bold transition-colors ${
                  language === 'KO'
                    ? 'bg-sky-600 text-white shadow-sm'
                    : 'text-slate-400 hover:text-white'
                }`}
              >
                KO
              </button>
              <button
                onClick={() => setLanguage('EN')}
                className={`px-2 py-0.5 rounded text-xs font-bold transition-colors ${
                  language === 'EN'
                    ? 'bg-sky-600 text-white shadow-sm'
                    : 'text-slate-400 hover:text-white'
                }`}
              >
                EN
              </button>
            </div>
          </div>
        </header>

        {/* ================================================================= */}
        {/* Level 1: Top Safety Banner (E-Stop / Latched Alarms) */}
        {/* ================================================================= */}
        <TopSafetyBanner
          isAlarm={vm.isAlarm}
          physicalEstop={vm.physicalEstop}
          alarmCause={vm.alarmCause}
          alarmCauseText={vm.alarmCauseText}
          guideMsg={vm.alarmGuideMsg}
        />

        {/* ================================================================= */}
        {/* Level 2 & 3: Primary Machine State, Monitoring & Controls */}
        {/* ================================================================= */}
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">
          {/* Left Column (2 Cols): Machine State, Sensor Gauge, Setpoint, QoS */}
          <div className="lg:col-span-2 space-y-5">
            {/* 1-Second Primary Machine State Badge */}
            <MachineStateBadge
              statusText={vm.statusText}
              statusColor={vm.statusColor}
              statusBadgeBg={vm.statusBadgeBg}
              isAlarm={vm.isAlarm}
              physicalEstop={vm.physicalEstop}
              running={vm.canStop}
              ready={vm.canStart}
              stationId={vm.stationId}
            />

            {/* In-Range Analog Bar (0~1000 with 400~600 Band) */}
            <ProcessSensorGauge
              sensorVal={vm.sensorVal}
              sensorPct={vm.sensorPct}
              isInRange={vm.isInRange}
            />

            {/* Setpoint Control Card */}
            <SetpointControl
              currentSetpoint={vm.setpointVal}
              onApplySetpoint={handleApplySetpoint}
              disabled={vm.physicalEstop}
            />

            {/* Network QoS Telemetry Card */}
            <CommHealthMetrics
              rttMs={vm.rttMs}
              jitterMs={vm.jitterMs}
              heartbeat={vm.heartbeat}
              linkState={vm.linkState}
              linkStateText={vm.linkStateText}
              consecutiveFailures={vm.consecutiveFailures}
              ioErrorCount={vm.ioErrorCount}
              busFreqHz={vm.busFreqHz}
            />
          </div>

          {/* Right Column (1 Col): Operator Controls & System Info */}
          <div className="space-y-5">
            {/* Operator Control Panel (START, STOP, RESET) */}
            <OperatorControls
              canStart={vm.canStart}
              canStop={vm.canStop}
              canClear={vm.canClear}
              physicalEstop={vm.physicalEstop}
              isAlarm={vm.isAlarm}
              onSelectAction={handleOpenActionModal}
            />

            {/* Telemetry Summary & Architecture Details Card */}
            <div className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg space-y-3 font-mono text-xs">
              <div className="flex items-center justify-between border-b border-slate-700/60 pb-2">
                <span className="font-bold text-slate-300 flex items-center gap-1.5">
                  <ShieldCheck className="w-4 h-4 text-emerald-400" />
                  {t.systemArch.title}
                </span>
                <span className="text-[10px] text-slate-500">{t.systemArch.specBadge}</span>
              </div>

              <div className="space-y-2 text-slate-400">
                <div className="flex justify-between">
                  <span>{t.systemArch.modbusTransport}</span>
                  <span className="text-white font-semibold">TCP / Port 5020</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.functionCode}</span>
                  <span className="text-white font-semibold">FC03 (Bulk Read 6HR)</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.controlCoils}</span>
                  <span className="text-white font-semibold">FC05 (Run/Reset)</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.setpointRegister}</span>
                  <span className="text-white font-semibold">FC06 (HR Offset 3)</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.slaRttTarget}</span>
                  <span className="text-emerald-400 font-semibold">&lt; 5.0 ms</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.slaJitterTarget}</span>
                  <span className="text-emerald-400 font-semibold">&lt; 2.0 ms</span>
                </div>
                <div className="flex justify-between">
                  <span>{t.systemArch.activeErrorCode}</span>
                  <span className={vm.errorCode === 0 ? "text-slate-300" : "text-amber-400 font-bold"}>
                    {vm.errorCode} ({vm.errorCodeText})
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* ================================================================= */}
        {/* Dedicated Test Bench: Tier 1 & Tier 2 Diagnostic Panel */}
        {/* ================================================================= */}
        {showTestBench && (
          <TestBench
            currentFaultMode={faultMode}
            physicalEstop={vm.physicalEstop}
            rttMs={vm.rttMs}
            onInjectFault={handleInjectFault}
            onToggleEstop={handleToggleEstop}
            onToggleProcessFault={handleToggleProcessFault}
            onTestSetpointBoundary={handleTestSetpointBoundary}
            onOneShotClearFault={handleOneShotClearFault}
            lastTestOutcome={lastTestOutcome}
          />
        )}
      </div>

      {/* 2-Step Long-Press Confirmation Modal */}
      <ConfirmationModal
        isOpen={modalOpen}
        action={pendingAction}
        stationId={vm.stationId}
        onConfirm={handleExecuteConfirmedAction}
        onClose={() => setModalOpen(false)}
      />
    </div>
  );
}

export default function App() {
  return (
    <I18nProvider>
      <DashboardContent />
    </I18nProvider>
  );
}
