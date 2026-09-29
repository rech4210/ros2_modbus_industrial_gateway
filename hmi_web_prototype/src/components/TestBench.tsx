import React, { useState } from 'react';
import {
  AlertOctagon,
  AlertTriangle,
  CheckCircle2,
  ChevronDown,
  ChevronUp,
  Clock,
  Flame,
  Radio,
  RefreshCw,
  RotateCcw,
  Sliders,
  Unplug,
  Wrench,
  Zap,
} from 'lucide-react';
import { useI18n } from '../context/I18nContext';
import { FaultMode } from '../types';
import { Tooltip } from './Tooltip';

interface TestBenchProps {
  currentFaultMode: FaultMode;
  physicalEstop: boolean;
  rttMs: number;
  onInjectFault: (mode: FaultMode, delayMs?: number) => void;
  onToggleEstop: (asserted: boolean) => void;
  onToggleProcessFault: (asserted: boolean) => void;
  onTestSetpointBoundary: (val: number) => void;
  onOneShotClearFault: () => void;
  lastTestOutcome?: string;
}

export const TestBench: React.FC<TestBenchProps> = ({
  currentFaultMode,
  physicalEstop,
  rttMs,
  onInjectFault,
  onToggleEstop,
  onToggleProcessFault,
  onTestSetpointBoundary,
  onOneShotClearFault,
  lastTestOutcome = '',
}) => {
  const { t } = useI18n();
  const [isExpanded, setIsExpanded] = useState(true);
  const [processFaultActive, setProcessFaultActive] = useState(false);

  const handleToggleProcessFault = () => {
    const next = !processFaultActive;
    setProcessFaultActive(next);
    onToggleProcessFault(next);
  };

  return (
    <section className="border-2 border-dashed border-amber-500/50 bg-[#1E1F22] rounded-2xl p-5 shadow-2xl space-y-4">
      {/* Test Bench Header & Safety Demarcation */}
      <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-700/80 pb-3">
        <div className="flex items-center space-x-3">
          <div className="w-9 h-9 rounded-lg bg-amber-500/20 border border-amber-500/40 flex items-center justify-center text-amber-400">
            <Wrench className="w-5 h-5" />
          </div>
          <div>
            <h2 className="text-base font-bold text-amber-300 flex items-center gap-2">
              {t.testBench.title}
            </h2>
            <p className="text-xs text-amber-400/80 font-medium">
              {t.testBench.demarcationWarning}
            </p>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <div className="text-xs font-mono bg-slate-800 px-3 py-1.5 rounded-lg border border-slate-700 text-slate-300">
            {t.testBench.rttDisplay} <strong className="text-emerald-400">{rttMs.toFixed(2)} ms</strong>
          </div>

          <button
            onClick={() => setIsExpanded(!isExpanded)}
            className="p-1.5 rounded-lg bg-slate-800 hover:bg-slate-750 text-slate-300 transition-colors"
          >
            {isExpanded ? <ChevronUp className="w-5 h-5" /> : <ChevronDown className="w-5 h-5" />}
          </button>
        </div>
      </div>

      {isExpanded && (
        <div className="space-y-5 animate-in fade-in duration-200">
          {/* =============================================================== */}
          {/* TIER 1: Real-Time Inline Injection */}
          {/* =============================================================== */}
          <div className="bg-[#2B2D30] border border-slate-700/80 rounded-xl p-4 space-y-3">
            <div className="flex flex-wrap items-center justify-between gap-2 border-b border-slate-700/60 pb-2">
              <div>
                <h3 className="text-sm font-bold text-white flex items-center gap-2">
                  <Flame className="w-4 h-4 text-rose-400" />
                  {t.testBench.tier1Title}
                </h3>
                <p className="text-xs text-slate-400">{t.testBench.tier1Subtitle}</p>
              </div>
              <span className="text-[10px] font-mono uppercase bg-rose-500/10 text-rose-300 px-2 py-0.5 rounded border border-rose-500/30">
                {t.testBench.safetyTier1Badge}
              </span>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
              {/* E-Stop Injection Card */}
              <div className="bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 space-y-2">
                <span className="text-xs font-semibold text-slate-300 flex items-center justify-between">
                  <span>{t.testBench.estopToggle}</span>
                  <span className={physicalEstop ? 'text-red-400 font-bold' : 'text-slate-500'}>
                    {physicalEstop ? t.testBench.statusActive : t.testBench.statusInactive}
                  </span>
                </span>
                <button
                  onClick={() => onToggleEstop(!physicalEstop)}
                  className={`w-full py-2.5 px-3 rounded-lg text-xs font-bold transition-all shadow-md flex items-center justify-center gap-2 ${
                    physicalEstop
                      ? 'bg-red-700 hover:bg-red-600 text-white animate-pulse'
                      : 'bg-slate-800 hover:bg-slate-750 text-slate-300 border border-slate-700'
                  }`}
                >
                  <AlertOctagon className="w-4 h-4" />
                  <span>{physicalEstop ? t.testBench.estopToggleOff : t.testBench.estopToggleOn}</span>
                </button>
              </div>

              {/* Process Fault Injection Card */}
              <div className="bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 space-y-2">
                <span className="text-xs font-semibold text-slate-300 flex items-center justify-between">
                  <span>{t.testBench.processFaultToggle}</span>
                  <span className={processFaultActive ? 'text-amber-400 font-bold' : 'text-slate-500'}>
                    {processFaultActive ? t.testBench.statusActive : t.testBench.statusInactive}
                  </span>
                </span>
                <button
                  onClick={handleToggleProcessFault}
                  className={`w-full py-2.5 px-3 rounded-lg text-xs font-bold transition-all shadow-md flex items-center justify-center gap-2 ${
                    processFaultActive
                      ? 'bg-amber-600 hover:bg-amber-500 text-white'
                      : 'bg-slate-800 hover:bg-slate-750 text-slate-300 border border-slate-700'
                  }`}
                >
                  <AlertTriangle className="w-4 h-4" />
                  <span>{processFaultActive ? t.testBench.processFaultOff : t.testBench.processFaultOn}</span>
                </button>
              </div>

              {/* Clear Fault 1-Shot Reset Card */}
              <div className="bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 space-y-2">
                <span className="text-xs font-semibold text-slate-300">
                  {t.testBench.oneShotReset}
                </span>
                <button
                  onClick={onOneShotClearFault}
                  className="w-full py-2.5 px-3 rounded-lg text-xs font-bold bg-sky-600 hover:bg-sky-500 text-white transition-all shadow-md flex items-center justify-center gap-2 active:scale-95"
                >
                  <RotateCcw className="w-4 h-4" />
                  <span>{t.testBench.triggerClearFaultBtn}</span>
                </button>
              </div>
            </div>

            {/* Setpoint Boundary Range Testing Buttons */}
            <div className="bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 space-y-2">
              <span className="text-xs font-semibold text-slate-300">
                {t.testBench.boundaryTestTitle}
              </span>
              <div className="grid grid-cols-2 sm:grid-cols-5 gap-2">
                <button
                  onClick={() => onTestSetpointBoundary(-1)}
                  className="px-2.5 py-1.5 rounded bg-rose-950/50 hover:bg-rose-900/60 text-rose-300 text-xs font-mono border border-rose-800/80 transition-colors"
                >
                  {t.testBench.testBoundaryNeg}
                </button>
                <button
                  onClick={() => onTestSetpointBoundary(0)}
                  className="px-2.5 py-1.5 rounded bg-slate-800 hover:bg-slate-750 text-slate-200 text-xs font-mono border border-slate-700 transition-colors"
                >
                  {t.testBench.testBoundary0}
                </button>
                <button
                  onClick={() => onTestSetpointBoundary(500)}
                  className="px-2.5 py-1.5 rounded bg-slate-800 hover:bg-slate-750 text-sky-300 text-xs font-mono border border-slate-700 transition-colors"
                >
                  {t.testBench.testBoundary500}
                </button>
                <button
                  onClick={() => onTestSetpointBoundary(1000)}
                  className="px-2.5 py-1.5 rounded bg-slate-800 hover:bg-slate-750 text-slate-200 text-xs font-mono border border-slate-700 transition-colors"
                >
                  {t.testBench.testBoundary1000}
                </button>
                <button
                  onClick={() => onTestSetpointBoundary(1001)}
                  className="px-2.5 py-1.5 rounded bg-rose-950/50 hover:bg-rose-900/60 text-rose-300 text-xs font-mono border border-rose-800/80 transition-colors"
                >
                  {t.testBench.testBoundary1001}
                </button>
              </div>
            </div>
          </div>

          {/* =============================================================== */}
          {/* TIER 2: Benchmark & Batch Fault Scenarios */}
          {/* =============================================================== */}
          <div className="bg-[#2B2D30] border border-slate-700/80 rounded-xl p-4 space-y-3">
            <div className="flex flex-wrap items-center justify-between gap-2 border-b border-slate-700/60 pb-2">
              <div>
                <h3 className="text-sm font-bold text-white flex items-center gap-2">
                  <Zap className="w-4 h-4 text-amber-400" />
                  {t.testBench.tier2Title}
                </h3>
                <p className="text-xs text-slate-400">{t.testBench.tier2Subtitle}</p>
              </div>
              <span className="text-[10px] font-mono uppercase bg-amber-500/10 text-amber-300 px-2 py-0.5 rounded border border-amber-500/30">
                {t.testBench.networkTier2Badge}
              </span>
            </div>

            <div className="grid grid-cols-2 sm:grid-cols-5 gap-2.5">
              <button
                onClick={() => onInjectFault('NORMAL')}
                className={`py-2.5 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center gap-1.5 ${
                  currentFaultMode === 'NORMAL'
                    ? 'bg-emerald-600 text-white border-emerald-500 shadow-md'
                    : 'bg-[#1E1F22] text-slate-300 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <CheckCircle2 className="w-3.5 h-3.5" />
                <span>{t.testBench.modeNormal}</span>
              </button>

              <button
                onClick={() => onInjectFault('DROP')}
                className={`py-2.5 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center gap-1.5 ${
                  currentFaultMode === 'DROP'
                    ? 'bg-amber-600 text-white border-amber-500 shadow-md'
                    : 'bg-[#1E1F22] text-slate-300 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <AlertTriangle className="w-3.5 h-3.5" />
                <span>{t.testBench.modeDrop}</span>
              </button>

              <button
                onClick={() => onInjectFault('DELAY', 30)}
                className={`py-2.5 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center gap-1.5 ${
                  currentFaultMode === 'DELAY'
                    ? 'bg-amber-600 text-white border-amber-500 shadow-md'
                    : 'bg-[#1E1F22] text-slate-300 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <Clock className="w-3.5 h-3.5" />
                <span>{t.testBench.modeDelay}</span>
              </button>

              <button
                onClick={() => onInjectFault('DISCONNECT')}
                className={`py-2.5 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center gap-1.5 ${
                  currentFaultMode === 'DISCONNECT'
                    ? 'bg-rose-600 text-white border-rose-500 shadow-md'
                    : 'bg-[#1E1F22] text-slate-300 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <Unplug className="w-3.5 h-3.5" />
                <span>{t.testBench.modeDisconnect}</span>
              </button>

              <button
                onClick={() => onInjectFault('FREEZE')}
                className={`py-2.5 px-3 rounded-lg text-xs font-bold border transition-all flex items-center justify-center gap-1.5 ${
                  currentFaultMode === 'FREEZE'
                    ? 'bg-sky-700 text-white border-sky-600 shadow-md'
                    : 'bg-[#1E1F22] text-slate-300 border-slate-700 hover:bg-slate-750'
                }`}
              >
                <Radio className="w-3.5 h-3.5" />
                <span>{t.testBench.modeFreeze}</span>
              </button>
            </div>

            {/* Test Feedback Notice */}
            {lastTestOutcome && (
              <div className="bg-[#1E1F22] p-2.5 rounded-lg border border-slate-700/80 text-xs font-mono text-slate-300 flex items-center justify-between">
                <span>{t.testBench.lastResult}</span>
                <span className="text-amber-400 font-bold">{lastTestOutcome}</span>
              </div>
            )}
          </div>
        </div>
      )}
    </section>
  );
};
