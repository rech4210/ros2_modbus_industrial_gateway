import React from 'react';
import { Gauge, Info } from 'lucide-react';
import { useI18n } from '../context/I18nContext';
import { Tooltip } from './Tooltip';

interface ProcessSensorGaugeProps {
  sensorVal: number;
  sensorPct: number;
  isInRange: boolean;
}

export const ProcessSensorGauge: React.FC<ProcessSensorGaugeProps> = ({
  sensorVal,
  sensorPct,
  isInRange,
}) => {
  const { t } = useI18n();

  return (
    <section className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg space-y-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div>
          <h2 className="text-sm font-bold text-white flex items-center gap-2">
            <Gauge className="w-4 h-4 text-sky-400" />
            {t.process.title}
            <Tooltip content={t.process.tooltip}>
              <Info className="w-3.5 h-3.5 text-slate-500 cursor-help" />
            </Tooltip>
          </h2>
          <p className="text-xs text-slate-400 mt-0.5">
            {t.process.subtitle}
          </p>
        </div>

        <div className="flex items-center gap-3 font-mono">
          <span className="text-xs text-slate-400">{t.process.currentValue}</span>
          <span className="text-2xl font-black text-white tracking-tight">
            {sensorVal}{' '}
            <span className="text-xs text-slate-400 font-normal">
              ({sensorPct.toFixed(1)}%)
            </span>
          </span>
          <span
            className={`text-xs px-2.5 py-1 rounded font-sans font-semibold border ${
              isInRange
                ? 'bg-emerald-500/20 text-emerald-400 border-emerald-500/40'
                : 'bg-amber-500/20 text-amber-400 border-amber-500/40'
            }`}
          >
            {isInRange ? t.process.inRange : t.process.outOfRange}
          </span>
        </div>
      </div>

      {/* ISA-101 Analog Bar */}
      <div className="relative pt-6 pb-2">
        {/* Scale Base Track */}
        <div className="relative h-7 bg-[#1E1F22] rounded-lg border border-slate-700 overflow-hidden">
          {/* Shaded Normal Band (40% to 60%) */}
          <div
            className="absolute top-0 bottom-0 bg-emerald-600/25 border-x-2 border-emerald-500/60 flex items-center justify-center z-10"
            style={{ left: '40%', width: '20%' }}
          >
            <span className="text-[10px] text-emerald-300 font-bold tracking-wider opacity-85 hidden sm:inline select-none">
              {t.process.normalBand}
            </span>
          </div>

          {/* Active Level Fill Bar */}
          <div
            className={`h-full transition-all duration-150 rounded-l ${
              isInRange ? 'bg-sky-500/60' : 'bg-amber-500/60'
            }`}
            style={{ width: `${Math.min(100, Math.max(0, sensorPct))}%` }}
          />
        </div>

        {/* Current Value Needle Pointer */}
        <div
          className="absolute top-2 transition-all duration-150 flex flex-col items-center -ml-2.5 z-20 pointer-events-none"
          style={{ left: `${Math.min(99, Math.max(1, sensorPct))}%` }}
        >
          <div className="w-5 h-5 bg-white border-2 border-sky-400 rounded-full shadow-lg shadow-sky-500/60 flex items-center justify-center">
            <div className="w-1.5 h-1.5 bg-sky-600 rounded-full" />
          </div>
          <div className="w-0.5 h-7 bg-white/80" />
        </div>

        {/* Scale Graduation Ticks */}
        <div className="flex justify-between text-[11px] text-slate-400 font-mono mt-2 px-1">
          <span>{t.process.scaleMin}</span>
          <span className="text-emerald-400 font-bold">{t.process.scaleNormalStart}</span>
          <span className="text-sky-300 font-bold">{t.process.scaleTarget}</span>
          <span className="text-emerald-400 font-bold">{t.process.scaleNormalEnd}</span>
          <span>{t.process.scaleMax}</span>
        </div>
      </div>
    </section>
  );
};
