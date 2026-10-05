import React, { useState, useEffect } from 'react';
import { useI18n } from '../context/I18nContext';
import { Tooltip } from './Tooltip';

interface SetpointControlProps {
  currentSetpoint: number;
  onApplySetpoint: (val: number) => void;
  disabled?: boolean;
}

export const SetpointControl: React.FC<SetpointControlProps> = ({
  currentSetpoint,
  onApplySetpoint,
  disabled = false,
}) => {
  const { t } = useI18n();
  const [targetVal, setTargetVal] = useState<number>(currentSetpoint);
  const [appliedNotice, setAppliedNotice] = useState(false);

  useEffect(() => {
    setTargetVal(currentSetpoint);
  }, [currentSetpoint]);

  const handleSliderChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = parseInt(e.target.value, 10);
    if (!isNaN(val)) {
      setTargetVal(Math.max(0, Math.min(1000, val)));
    }
  };

  const handlePreset = (val: number) => {
    setTargetVal(val);
  };

  const handleApply = () => {
    if (targetVal < 0 || targetVal > 1000) {
      alert(t.setpoint.boundaryAlert);
      return;
    }
    onApplySetpoint(targetVal);
    setAppliedNotice(true);
    setTimeout(() => setAppliedNotice(false), 2000);
  };

  const presets: { val: number; label: string }[] = [
    { val: 0, label: t.setpoint.presetMin },
    { val: 400, label: t.setpoint.presetBandMin },
    { val: 500, label: t.setpoint.presetCenter },
    { val: 600, label: t.setpoint.presetBandMax },
    { val: 1000, label: t.setpoint.presetMax },
  ];

  const pending = targetVal !== currentSetpoint;

  return (
    <section className="panel">
      <div className="panel-head">
        <Tooltip content={t.setpoint.tooltip} position="bottom">
          <span className="cursor-help">{t.setpoint.title}</span>
        </Tooltip>
        <span className="panel-meta">CMD 4 · HR[3]</span>
      </div>

      <div className="p-3 space-y-3">
        <p className="text-[12px] text-hmi-dim">{t.setpoint.subtitle}</p>

        <div className="grid grid-cols-1 sm:grid-cols-[auto_1fr_auto] items-center gap-x-4 gap-y-3">
          {/* Current vs. target: target in blue only while it differs from the live setpoint */}
          <dl className="grid grid-cols-[auto_auto] gap-x-3 gap-y-0.5 items-baseline">
            <dt className="text-[11px] text-hmi-faint">{t.setpoint.current}</dt>
            <dd className="num text-[18px] font-bold text-hmi-ink text-right">{currentSetpoint}</dd>
            <dt className="text-[11px] text-hmi-faint">{t.setpoint.target}</dt>
            <dd className={`num text-[18px] font-bold text-right ${pending ? 'text-sel' : 'text-hmi-faint'}`}>{targetVal}</dd>
          </dl>

          <div className="space-y-1">
            <input
              type="range"
              min="0"
              max="1000"
              step="1"
              value={targetVal}
              disabled={disabled}
              onChange={handleSliderChange}
              className="w-full h-2 cursor-pointer disabled:opacity-40 disabled:cursor-not-allowed"
            />
            <div className="flex justify-between num text-[10px] text-hmi-faint">
              <span>0</span>
              <span>250</span>
              <span>500</span>
              <span>750</span>
              <span>1000</span>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <input
              type="number"
              min="0"
              max="1000"
              value={targetVal}
              disabled={disabled}
              onChange={(e) => {
                const v = parseInt(e.target.value, 10);
                if (!isNaN(v)) setTargetVal(v);
              }}
              className="w-20 px-2 py-1.5 bg-white border border-hmi-dim num text-[14px] text-right font-semibold text-hmi-ink focus:outline-none focus:border-sel focus:ring-1 focus:ring-sel disabled:bg-hmi-panel disabled:text-hmi-faint"
            />
            <Tooltip content={t.setpoint.tooltip}>
              <button
                onClick={handleApply}
                disabled={disabled}
                className={`btn px-3 py-1.5 text-[13px] font-semibold min-w-[7rem] ${appliedNotice ? 'btn-sel' : ''}`}
              >
                {appliedNotice ? t.setpoint.appliedNotice : t.setpoint.applyBtn}
              </button>
            </Tooltip>
          </div>
        </div>

        <div className="flex flex-wrap items-center justify-between gap-2 pt-2 border-t border-hmi-line">
          <span className="text-[11px] text-hmi-faint">{t.setpoint.boundaryNotice}</span>
          <div className="flex flex-wrap gap-1">
            {presets.map((p) => (
              <button
                key={p.val}
                onClick={() => handlePreset(p.val)}
                disabled={disabled}
                className={`btn px-2 py-0.5 text-[11px] num ${targetVal === p.val ? 'border-sel text-sel' : ''}`}
              >
                {p.label}
              </button>
            ))}
          </div>
        </div>
      </div>
    </section>
  );
};
