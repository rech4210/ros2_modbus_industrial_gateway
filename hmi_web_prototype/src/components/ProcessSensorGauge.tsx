import React from 'react';
import { useI18n } from '../context/I18nContext';
import { AlarmMarker } from './AlarmMarker';
import { Tooltip } from './Tooltip';

interface ProcessSensorGaugeProps {
  sensorVal: number;
  sensorPct: number;
  isInRange: boolean;
}

// Scale positions in raw units (0..1000) so ticks land where the values really are.
const BAND_LO = 400;
const BAND_HI = 600;

export const ProcessSensorGauge: React.FC<ProcessSensorGaugeProps> = ({
  sensorVal,
  sensorPct,
  isInRange,
}) => {
  const { t } = useI18n();
  const pct = Math.min(100, Math.max(0, sensorPct));

  const ticks: { pos: number; label: string; strong?: boolean }[] = [
    { pos: 0, label: t.process.scaleMin },
    { pos: BAND_LO / 10, label: t.process.scaleNormalStart, strong: true },
    { pos: 50, label: t.process.scaleTarget },
    { pos: BAND_HI / 10, label: t.process.scaleNormalEnd, strong: true },
    { pos: 100, label: t.process.scaleMax },
  ];

  return (
    <section className="panel">
      <div className="panel-head">
        <Tooltip content={t.process.tooltip} position="bottom">
          <span className="cursor-help">{t.process.title}</span>
        </Tooltip>
        <span className="panel-meta">HR[2] · 0–1000</span>
      </div>

      <div className="p-3 space-y-3">
        <div className="flex flex-wrap items-end justify-between gap-3">
          <p className="text-[12px] text-hmi-dim max-w-md">{t.process.subtitle}</p>

          <div className="flex items-center gap-3">
            <span className="text-[11px] text-hmi-faint">{t.process.currentValue}</span>
            <span className="num text-[28px] font-bold leading-none text-hmi-ink">{sensorVal}</span>
            <span className="num text-[12px] text-hmi-faint">{sensorPct.toFixed(1)}%</span>
            <span
              className={`flex items-center gap-1.5 text-[12px] px-2 py-0.5 border ${
                isInRange ? 'border-hmi-line text-hmi-dim' : 'border-p2 text-hmi-ink font-semibold bg-hmi-well'
              }`}
            >
              {!isInRange && <AlarmMarker priority={2} size={14} />}
              {isInRange ? t.process.inRange : t.process.outOfRange}
            </span>
          </div>
        </div>

        {/* Analog bar: pointer on a grey scale; the normal band is a darker grey zone. */}
        <div className="pt-4 pb-1 px-1">
          <div className="relative h-6 well">
            <div
              className="absolute inset-y-0 bg-hmi-soft border-x border-hmi-dim flex items-center justify-center overflow-hidden"
              style={{ left: `${BAND_LO / 10}%`, width: `${(BAND_HI - BAND_LO) / 10}%` }}
            >
              <span className="hidden sm:inline text-[10px] text-hmi-dim whitespace-nowrap px-1 truncate">
                {t.process.normalBand}
              </span>
            </div>

            {/* Value fill: thin strip along the bottom so the band stays readable */}
            <div
              className={`absolute left-0 bottom-0 h-1.5 transition-[width] duration-150 ${isInRange ? 'bg-hmi-dim' : 'bg-p2'}`}
              style={{ width: `${pct}%` }}
            />

            {/* Pointer */}
            <div
              className="absolute -top-3 bottom-0 transition-[left] duration-150 pointer-events-none"
              style={{ left: `${pct}%` }}
            >
              <svg width="12" height="10" viewBox="0 0 12 10" className="-ml-[6px] block" aria-hidden="true">
                <polygon points="0,0 12,0 6,10" fill={isInRange ? '#141414' : '#D97500'} stroke="#141414" strokeWidth="1" />
              </svg>
              <div className={`w-0.5 -ml-px h-[calc(100%-10px)] ${isInRange ? 'bg-hmi-ink' : 'bg-p2'}`} />
            </div>
          </div>

          {/* Scale */}
          <div className="relative h-8 mt-1">
            {ticks.map((tk, i) => (
              <div
                key={i}
                className={`absolute top-0 flex flex-col ${
                  i === 0 ? 'items-start' : i === ticks.length - 1 ? 'items-end -translate-x-full' : 'items-center -translate-x-1/2'
                }`}
                style={{ left: `${tk.pos}%` }}
              >
                <span className="w-px h-1.5 bg-hmi-dim" />
                <span className={`num text-[10px] whitespace-nowrap ${tk.strong ? 'text-hmi-ink' : 'text-hmi-faint'}`}>
                  {tk.label}
                </span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </section>
  );
};
