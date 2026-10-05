import React, { useState } from 'react';
import { useI18n } from '../context/I18nContext';
import { FaultMode } from '../types';

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

/**
 * Engineering-only area. It is fenced off with a hatched border and a dark title bar so it
 * never reads as part of the operator screen above it.
 */
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

  const modes: { mode: FaultMode; delay?: number; label: string }[] = [
    { mode: 'NORMAL', label: t.testBench.modeNormal },
    { mode: 'DROP', label: t.testBench.modeDrop },
    { mode: 'DELAY', delay: 30, label: t.testBench.modeDelay },
    { mode: 'DISCONNECT', label: t.testBench.modeDisconnect },
    { mode: 'FREEZE', label: t.testBench.modeFreeze },
  ];

  const boundaries: { val: number; label: string; reject?: boolean }[] = [
    { val: -1, label: t.testBench.testBoundaryNeg, reject: true },
    { val: 0, label: t.testBench.testBoundary0 },
    { val: 500, label: t.testBench.testBoundary500 },
    { val: 1000, label: t.testBench.testBoundary1000 },
    { val: 1001, label: t.testBench.testBoundary1001, reject: true },
  ];

  const toggleState = (on: boolean) => (
    <span className={`num text-[11px] ${on ? 'text-hmi-ink font-bold' : 'text-hmi-faint'}`}>
      {on ? t.testBench.statusActive : t.testBench.statusInactive}
    </span>
  );

  return (
    <section
      className="p-[6px]"
      style={{
        background: 'repeating-linear-gradient(135deg, #8A8A8A 0 8px, #CDCDCD 8px 16px)',
      }}
    >
      <div className="bg-hmi-panel">
        <div className="flex flex-wrap items-center justify-between gap-2 px-3 py-2 bg-hmi-head text-white">
          <div className="min-w-0">
            <h2 className="text-[13px] font-semibold">{t.testBench.title}</h2>
            <p className="text-[11px] text-white/70">{t.testBench.demarcationWarning}</p>
          </div>
          <div className="flex items-center gap-3">
            <span className="num text-[11px] text-white/80">
              {t.testBench.rttDisplay} <span className="text-white">{rttMs.toFixed(2)} ms</span>
            </span>
            <button
              onClick={() => setIsExpanded(!isExpanded)}
              aria-expanded={isExpanded}
              className="px-2 py-0.5 text-[11px] border border-white/40 text-white hover:bg-white/10"
            >
              {isExpanded ? '▲' : '▼'}
            </button>
          </div>
        </div>

        {isExpanded && (
          <div className="p-3 grid gap-3 lg:grid-cols-2">
            {/* Tier 1 */}
            <div className="panel">
              <div className="panel-head">
                <span>{t.testBench.tier1Title}</span>
                <span className="panel-meta">{t.testBench.safetyTier1Badge}</span>
              </div>
              <div className="p-3 space-y-3">
                <p className="text-[11px] text-hmi-faint">{t.testBench.tier1Subtitle}</p>

                <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
                  <div className="space-y-1">
                    <div className="flex items-center justify-between text-[12px] text-hmi-dim">
                      <span>{t.testBench.estopToggle}</span>
                      {toggleState(physicalEstop)}
                    </div>
                    <button
                      onClick={() => onToggleEstop(!physicalEstop)}
                      className={`btn w-full py-2 text-[12px] font-semibold ${
                        physicalEstop ? '!bg-p1 !text-white !border-p1 shadow-none' : ''
                      }`}
                    >
                      {physicalEstop ? t.testBench.estopToggleOff : t.testBench.estopToggleOn}
                    </button>
                  </div>

                  <div className="space-y-1">
                    <div className="flex items-center justify-between text-[12px] text-hmi-dim">
                      <span>{t.testBench.processFaultToggle}</span>
                      {toggleState(processFaultActive)}
                    </div>
                    <button
                      onClick={handleToggleProcessFault}
                      className={`btn w-full py-2 text-[12px] font-semibold ${
                        processFaultActive ? '!bg-p2 !text-hmi-ink !border-p2 shadow-none' : ''
                      }`}
                    >
                      {processFaultActive ? t.testBench.processFaultOff : t.testBench.processFaultOn}
                    </button>
                  </div>

                  <div className="space-y-1">
                    <div className="text-[12px] text-hmi-dim truncate">{t.testBench.oneShotReset}</div>
                    <button onClick={onOneShotClearFault} className="btn w-full py-2 text-[12px] font-semibold">
                      {t.testBench.triggerClearFaultBtn}
                    </button>
                  </div>
                </div>

                <div className="space-y-1">
                  <div className="text-[12px] text-hmi-dim">{t.testBench.boundaryTestTitle}</div>
                  <div className="grid grid-cols-2 sm:grid-cols-5 gap-1">
                    {boundaries.map((b) => (
                      <button
                        key={b.val}
                        onClick={() => onTestSetpointBoundary(b.val)}
                        className={`btn px-1.5 py-1 text-[11px] num ${b.reject ? 'border-dashed' : ''}`}
                      >
                        {b.label}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            </div>

            {/* Tier 2 */}
            <div className="panel flex flex-col">
              <div className="panel-head">
                <span>{t.testBench.tier2Title}</span>
                <span className="panel-meta">{t.testBench.networkTier2Badge}</span>
              </div>
              <div className="p-3 space-y-3 flex-1 flex flex-col">
                <p className="text-[11px] text-hmi-faint">{t.testBench.tier2Subtitle}</p>

                <div role="radiogroup" className="grid grid-cols-2 sm:grid-cols-5 lg:grid-cols-3 xl:grid-cols-5 gap-1">
                  {modes.map((m) => {
                    const selected = currentFaultMode === m.mode;
                    return (
                      <button
                        key={m.mode}
                        role="radio"
                        aria-checked={selected}
                        onClick={() => onInjectFault(m.mode, m.delay)}
                        className={`btn px-1.5 py-2 text-[11px] font-semibold ${selected ? 'btn-sel' : ''}`}
                      >
                        {m.label}
                      </button>
                    );
                  })}
                </div>

                {lastTestOutcome && (
                  <div className="mt-auto well px-2.5 py-1.5 text-[11px] flex flex-wrap justify-between gap-2">
                    <span className="text-hmi-faint">{t.testBench.lastResult}</span>
                    <span className="num text-hmi-ink font-semibold">{lastTestOutcome}</span>
                  </div>
                )}
              </div>
            </div>
          </div>
        )}
      </div>
    </section>
  );
};
