import React, { useState, useEffect } from 'react';
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

  const lang = (code: 'KO' | 'EN') => (
    <button
      onClick={() => setLanguage(code)}
      aria-pressed={language === code}
      className={`px-2 py-0.5 text-[11px] font-semibold ${
        language === code ? 'bg-white text-hmi-ink' : 'text-white/70 hover:text-white'
      }`}
    >
      {code}
    </button>
  );

  // Abnormal states draw a single colored frame around the whole screen; normal operation has none.
  const frame = vm.physicalEstop
    ? 'border-[6px] border-p1'
    : vm.isAlarm
    ? 'border-[6px] border-p2'
    : 'border-[6px] border-transparent';

  return (
    <div className={`min-h-screen ${frame}`}>
      {/* Title bar */}
      <header className="bg-hmi-head text-white">
        <div className="max-w-7xl mx-auto px-3 md:px-4 py-2 flex flex-wrap items-center justify-between gap-x-6 gap-y-2">
          <div className="flex items-baseline gap-3 min-w-0">
            <h1 className="text-[15px] font-semibold truncate">{t.header.title}</h1>
            <span className="num text-[12px] text-white/70">
              ST{vm.stationId.toString().padStart(2, '0')}
            </span>
            <span className="hidden md:inline text-[12px] text-white/60 truncate">{t.header.subTitle}</span>
          </div>

          <div className="flex items-center flex-wrap gap-x-4 gap-y-1 num text-[12px]">
            <span className="text-white/60">
              {t.header.ddsBus} <span className="text-white">{vm.busFreqHz.toFixed(1)} Hz</span>
            </span>
            <span className="text-white/60">
              {t.header.heartbeat} <span className="text-white inline-block min-w-[3.5rem]">{vm.heartbeat}</span>
            </span>

            <Tooltip content={t.header.testBenchToggle} position="bottom">
              <button
                onClick={() => setShowTestBench(!showTestBench)}
                aria-pressed={showTestBench}
                className={`px-2 py-0.5 border text-[11px] font-sans ${
                  showTestBench
                    ? 'bg-white text-hmi-ink border-white'
                    : 'border-white/40 text-white/80 hover:text-white'
                }`}
              >
                Test Bench
              </button>
            </Tooltip>

            <div className="flex border border-white/40 font-sans" role="group" aria-label="Language">
              {lang('KO')}
              {lang('EN')}
            </div>
          </div>
        </div>
      </header>

      <main className="max-w-7xl mx-auto p-3 md:p-4 space-y-3">
        {/* Level 1: alarm summary and recovery steps */}
        <TopSafetyBanner
          isAlarm={vm.isAlarm}
          physicalEstop={vm.physicalEstop}
          alarmCause={vm.alarmCause}
          alarmCauseText={vm.alarmCauseText}
          guideMsg={vm.alarmGuideMsg}
        />

        {/* Level 2: unit state, process value and setpoint (left); operator actions (right) */}
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-3">
          <div className="lg:col-span-2 space-y-3">
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

            <ProcessSensorGauge
              sensorVal={vm.sensorVal}
              sensorPct={vm.sensorPct}
              isInRange={vm.isInRange}
            />

            <SetpointControl
              currentSetpoint={vm.setpointVal}
              onApplySetpoint={handleApplySetpoint}
              disabled={vm.physicalEstop}
            />

            {/* Level 3: communication diagnostics */}
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

          <div className="space-y-3">
            <OperatorControls
              canStart={vm.canStart}
              canStop={vm.canStop}
              canClear={vm.canClear}
              physicalEstop={vm.physicalEstop}
              isAlarm={vm.isAlarm}
              onSelectAction={handleOpenActionModal}
            />

            {/* Level 4: static configuration reference */}
            <section className="panel">
              <div className="panel-head">
                <span>{t.systemArch.title}</span>
                <span className="panel-meta">{t.systemArch.specBadge}</span>
              </div>
              <dl className="grid grid-cols-[1fr_auto] gap-x-3 gap-y-1 px-3 py-2 text-[12px]">
                <dt className="text-hmi-faint">{t.systemArch.modbusTransport}</dt>
                <dd className="num text-hmi-dim text-right">TCP / 5020</dd>
                <dt className="text-hmi-faint">{t.systemArch.functionCode}</dt>
                <dd className="num text-hmi-dim text-right">FC03 · 6 HR</dd>
                <dt className="text-hmi-faint">{t.systemArch.controlCoils}</dt>
                <dd className="num text-hmi-dim text-right">FC05 · Run/Reset</dd>
                <dt className="text-hmi-faint">{t.systemArch.setpointRegister}</dt>
                <dd className="num text-hmi-dim text-right">FC06 · HR[3]</dd>
                <dt className="text-hmi-faint">{t.systemArch.slaRttTarget}</dt>
                <dd className="num text-hmi-dim text-right">&lt; 5.0 ms</dd>
                <dt className="text-hmi-faint">{t.systemArch.slaJitterTarget}</dt>
                <dd className="num text-hmi-dim text-right">&lt; 2.0 ms</dd>
                <dt className="text-hmi-faint">{t.systemArch.activeErrorCode}</dt>
                <dd className={`num text-right ${vm.errorCode === 0 ? 'text-hmi-dim' : 'text-hmi-ink font-bold'}`}>
                  {vm.errorCode} ({vm.errorCodeText})
                </dd>
              </dl>
            </section>
          </div>
        </div>

        {/* Engineering test bench (hidden from operators via the title-bar toggle) */}
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
      </main>

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
