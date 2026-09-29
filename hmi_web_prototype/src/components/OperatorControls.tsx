import React from 'react';
import { Power, RotateCcw, Sliders, XCircle } from 'lucide-react';
import { useI18n } from '../context/I18nContext';
import { Tooltip } from './Tooltip';

interface OperatorControlsProps {
  canStart: boolean;
  canStop: boolean;
  canClear: boolean;
  physicalEstop: boolean;
  isAlarm: boolean;
  onSelectAction: (action: 'START' | 'STOP' | 'RESET') => void;
}

export const OperatorControls: React.FC<OperatorControlsProps> = ({
  canStart,
  canStop,
  canClear,
  physicalEstop,
  isAlarm,
  onSelectAction,
}) => {
  const { t } = useI18n();

  const getStartTooltip = () => {
    return canStart ? t.controls.startTooltipActive : t.controls.startTooltipDisabled;
  };

  const getStopTooltip = () => {
    return canStop ? t.controls.stopTooltipActive : t.controls.stopTooltipDisabled;
  };

  const getResetTooltip = () => {
    if (physicalEstop) return t.controls.resetTooltipLocked;
    if (isAlarm) return t.controls.resetTooltipActive;
    return t.controls.resetTooltipNotNeeded;
  };

  return (
    <div className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg space-y-4">
      <div className="flex items-center justify-between">
        <h3 className="text-sm font-bold text-white flex items-center gap-2">
          <Sliders className="w-4 h-4 text-sky-400" />
          {t.controls.title}
        </h3>
        <span className="text-[11px] text-slate-400 font-mono">2-Step Interlocked</span>
      </div>

      <div className="space-y-3">
        {/* START Button */}
        <Tooltip content={getStartTooltip()} position="top" className="w-full">
          <button
            disabled={!canStart}
            onClick={() => onSelectAction('START')}
            className={`w-full min-h-[56px] rounded-xl font-bold text-base flex items-center justify-center gap-2.5 transition-all duration-150 shadow-md ${
              canStart
                ? 'bg-emerald-600 hover:bg-emerald-500 text-white cursor-pointer active:scale-98 shadow-emerald-900/30'
                : 'bg-slate-800 text-slate-500 border border-slate-700/60 cursor-not-allowed opacity-60'
            }`}
          >
            <Power className="w-5 h-5" />
            <span>{t.controls.start}</span>
          </button>
        </Tooltip>

        {/* STOP Button */}
        <Tooltip content={getStopTooltip()} position="top" className="w-full">
          <button
            disabled={!canStop}
            onClick={() => onSelectAction('STOP')}
            className={`w-full min-h-[56px] rounded-xl font-bold text-base flex items-center justify-center gap-2.5 transition-all duration-150 shadow-md ${
              canStop
                ? 'bg-amber-600 hover:bg-amber-500 text-white cursor-pointer active:scale-98 shadow-amber-900/30'
                : 'bg-slate-800 text-slate-500 border border-slate-700/60 cursor-not-allowed opacity-60'
            }`}
          >
            <XCircle className="w-5 h-5" />
            <span>{t.controls.stop}</span>
          </button>
        </Tooltip>

        {/* RESET / CLEAR FAULT Button */}
        <Tooltip content={getResetTooltip()} position="top" className="w-full">
          <button
            disabled={!canClear}
            onClick={() => onSelectAction('RESET')}
            className={`w-full min-h-[56px] rounded-xl font-bold text-base flex items-center justify-center gap-2.5 transition-all duration-150 shadow-md ${
              canClear
                ? 'bg-rose-600 hover:bg-rose-500 text-white cursor-pointer active:scale-98 animate-pulse shadow-rose-900/30'
                : 'bg-slate-800 text-slate-500 border border-slate-700/60 cursor-not-allowed opacity-60'
            }`}
          >
            <RotateCcw className="w-5 h-5" />
            <span>{t.controls.reset}</span>
          </button>
        </Tooltip>
      </div>
    </div>
  );
};
