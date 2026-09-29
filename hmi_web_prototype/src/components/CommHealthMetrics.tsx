import React from 'react';
import { Activity, Clock, Heart, Radio, Wifi, WifiOff } from 'lucide-react';
import { useI18n } from '../context/I18nContext';
import { Tooltip } from './Tooltip';

interface CommHealthMetricsProps {
  rttMs: number;
  jitterMs: number;
  heartbeat: number;
  linkState: number;
  linkStateText: string;
  consecutiveFailures: number;
  ioErrorCount: number;
  busFreqHz: number;
}

export const CommHealthMetrics: React.FC<CommHealthMetricsProps> = ({
  rttMs,
  jitterMs,
  heartbeat,
  linkState,
  linkStateText,
  consecutiveFailures,
  ioErrorCount,
  busFreqHz,
}) => {
  const { t } = useI18n();

  const isRttNominal = rttMs <= 5.0;
  const isJitterNominal = jitterMs <= 2.0;

  return (
    <div className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg space-y-3">
      <div className="flex items-center justify-between">
        <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider flex items-center gap-1.5">
          <Activity className="w-3.5 h-3.5 text-sky-400" />
          {t.qos.title}
        </span>
        <span className="text-[11px] text-emerald-400 font-mono bg-emerald-500/10 px-2 py-0.5 rounded border border-emerald-500/20">
          {t.qos.deterministic}
        </span>
      </div>

      <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 font-mono">
        {/* FC03 Read RTT */}
        <Tooltip content={t.qos.rttTooltip} className="w-full">
          <div className="w-full bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 cursor-help">
            <div className="text-[11px] text-slate-400 flex items-center justify-between">
              <span>{t.qos.readRtt}</span>
              <span className="text-[10px] text-slate-500 font-sans">&lt;5.0ms</span>
            </div>
            <div
              className={`text-lg font-black mt-1 ${
                isRttNominal ? 'text-emerald-400' : 'text-amber-400'
              }`}
            >
              {rttMs.toFixed(2)} <span className="text-xs font-normal text-slate-400">ms</span>
            </div>
          </div>
        </Tooltip>

        {/* Polling Jitter */}
        <Tooltip content={t.qos.jitterTooltip} className="w-full">
          <div className="w-full bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 cursor-help">
            <div className="text-[11px] text-slate-400 flex items-center justify-between">
              <span>{t.qos.pollingJitter}</span>
              <span className="text-[10px] text-slate-500 font-sans">&lt;2.0ms</span>
            </div>
            <div
              className={`text-lg font-black mt-1 ${
                isJitterNominal ? 'text-emerald-400' : 'text-amber-400'
              }`}
            >
              {jitterMs.toFixed(3)} <span className="text-xs font-normal text-slate-400">ms</span>
            </div>
          </div>
        </Tooltip>

        {/* Heartbeat Counter */}
        <Tooltip content={t.qos.heartbeatTooltip} className="w-full">
          <div className="w-full bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 cursor-help">
            <div className="text-[11px] text-slate-400 flex items-center justify-between">
              <span>{t.qos.heartbeat}</span>
              <Heart className="w-3.5 h-3.5 text-rose-400 animate-pulse" />
            </div>
            <div className="text-lg font-black mt-1 text-white">
              {heartbeat}{' '}
              <span className="text-[10px] font-normal text-slate-400">/ 65535</span>
            </div>
          </div>
        </Tooltip>

        {/* Link State */}
        <Tooltip content={t.qos.linkTooltip} className="w-full">
          <div className="w-full bg-[#1E1F22] p-3 rounded-lg border border-slate-700/60 cursor-help">
            <div className="text-[11px] text-slate-400 flex items-center justify-between">
              <span>{t.qos.linkState}</span>
              {linkState === 2 ? (
                <Wifi className="w-3.5 h-3.5 text-emerald-400" />
              ) : (
                <WifiOff className="w-3.5 h-3.5 text-red-400" />
              )}
            </div>
            <div
              className={`text-sm font-bold mt-1.5 truncate ${
                linkState === 2 ? 'text-emerald-400' : 'text-rose-400'
              }`}
            >
              {linkStateText}
            </div>
          </div>
        </Tooltip>
      </div>

      {/* Sub-status strip */}
      <div className="flex flex-wrap items-center justify-between gap-2 text-xs font-mono text-slate-400 pt-1">
        <div className="flex items-center gap-2">
          <span>DDS Topic: <strong className="text-emerald-400">{busFreqHz.toFixed(1)} Hz</strong></span>
          <span>•</span>
          <span>Failures: <strong className={consecutiveFailures > 0 ? "text-amber-400" : "text-slate-300"}>{consecutiveFailures}</strong></span>
          <span>•</span>
          <span>I/O Errors: <strong className={ioErrorCount > 0 ? "text-amber-400" : "text-slate-300"}>{ioErrorCount}</strong></span>
        </div>
        <div className="text-slate-500 text-[11px]">
          Modbus Bulk FC03 (12 Bytes)
        </div>
      </div>
    </div>
  );
};
