import React from 'react';
import { Activity, AlertTriangle, CheckCircle2, PauseCircle, ShieldAlert, Zap } from 'lucide-react';
import { useI18n } from '../context/I18nContext';
import { Tooltip } from './Tooltip';

interface MachineStateBadgeProps {
  statusText: string;
  statusColor: string;
  statusBadgeBg: string;
  isAlarm: boolean;
  physicalEstop: boolean;
  running: boolean;
  ready: boolean;
  stationId: number;
}

export const MachineStateBadge: React.FC<MachineStateBadgeProps> = ({
  statusColor,
  statusBadgeBg,
  isAlarm,
  physicalEstop,
  running,
  ready,
  stationId,
}) => {
  const { t } = useI18n();

  const getLocalizedStateText = () => {
    if (physicalEstop) return t.machineState.eStop;
    if (isAlarm) return t.machineState.commFault;
    if (running) return t.machineState.running;
    if (ready) return t.machineState.ready;
    return t.machineState.idle;
  };

  const getTooltipContent = () => {
    if (physicalEstop) return t.machineState.tooltipEstop;
    if (isAlarm) return t.machineState.tooltipAlarm;
    if (running) return t.machineState.tooltipRunning;
    if (ready) return t.machineState.tooltipReady;
    return t.machineState.tooltipIdle;
  };

  return (
    <div className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg flex flex-col justify-between">
      <div className="flex items-center justify-between mb-3">
        <span className="text-xs font-semibold text-slate-400 uppercase tracking-wider flex items-center gap-1.5">
          <Zap className="w-3.5 h-3.5 text-amber-400" />
          {t.machineState.title}
        </span>
        <span className="text-xs text-slate-500 font-mono">
          Station #{stationId.toString().padStart(2, '0')} • {t.machineState.isoNorm}
        </span>
      </div>

      <Tooltip content={getTooltipContent()} position="bottom" className="w-full">
        <div
          className={`w-full rounded-lg border px-5 py-4 flex items-center justify-between transition-all duration-300 cursor-help ${statusBadgeBg}`}
        >
          <div className="flex items-center space-x-4">
            {physicalEstop ? (
              <AlertTriangle className="w-9 h-9 text-red-400 animate-bounce" />
            ) : isAlarm ? (
              <AlertTriangle className="w-9 h-9 text-amber-400" />
            ) : running ? (
              <CheckCircle2 className="w-9 h-9 text-emerald-400" />
            ) : (
              <PauseCircle className="w-9 h-9 text-sky-400" />
            )}

            <div>
              <div className={`text-xl md:text-2xl font-black tracking-tight ${statusColor}`}>
                {getLocalizedStateText()}
              </div>
              <div className="text-xs text-slate-400 mt-0.5 font-mono">
                {t.machineState.subtext}
              </div>
            </div>
          </div>
        </div>
      </Tooltip>
    </div>
  );
};
