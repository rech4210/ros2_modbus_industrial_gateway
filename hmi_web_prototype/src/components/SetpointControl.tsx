import React, { useState, useEffect } from 'react';
import { Check, Info, Send, SlidersHorizontal } from 'lucide-react';
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

  return (
    <section className="bg-[#2B2D30] border border-[#3E4247] rounded-xl p-5 shadow-lg space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div>
          <h3 className="text-sm font-bold text-white flex items-center gap-2">
            <SlidersHorizontal className="w-4 h-4 text-sky-400" />
            {t.setpoint.title}
            <Tooltip content={t.setpoint.tooltip}>
              <Info className="w-3.5 h-3.5 text-slate-500 cursor-help" />
            </Tooltip>
          </h3>
          <p className="text-xs text-slate-400 mt-0.5">
            {t.setpoint.subtitle}
          </p>
        </div>

        <div className="flex items-center gap-4 font-mono">
          <div className="text-xs">
            <span className="text-slate-400">{t.setpoint.current} </span>
            <span className="text-base font-bold text-emerald-400">{currentSetpoint}</span>
          </div>
          <div className="text-xs">
            <span className="text-slate-400">{t.setpoint.target} </span>
            <span className="text-base font-bold text-sky-400">{targetVal}</span>
          </div>
        </div>
      </div>

      {/* Slider & Input Row */}
      <div className="flex flex-col sm:flex-row items-center gap-4 pt-1">
        <div className="flex-1 w-full space-y-1">
          <input
            type="range"
            min="0"
            max="1000"
            step="1"
            value={targetVal}
            disabled={disabled}
            onChange={handleSliderChange}
            className="w-full h-2.5 bg-[#1E1F22] rounded-lg appearance-none cursor-pointer accent-sky-500 border border-slate-700 disabled:opacity-50 disabled:cursor-not-allowed"
          />
          <div className="flex justify-between text-[10px] text-slate-500 font-mono">
            <span>0</span>
            <span>250</span>
            <span>500</span>
            <span>750</span>
            <span>1000</span>
          </div>
        </div>

        {/* Numeric Input & Step Buttons */}
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
            className="w-20 px-2.5 py-1.5 bg-[#1E1F22] border border-slate-700 rounded-lg text-sm text-center font-mono font-bold text-white focus:outline-none focus:border-sky-500 disabled:opacity-50"
          />

          <Tooltip content={t.setpoint.tooltip}>
            <button
              onClick={handleApply}
              disabled={disabled}
              className={`px-4 py-2 rounded-lg font-bold text-xs flex items-center gap-1.5 transition-all shadow-md ${
                appliedNotice
                  ? 'bg-emerald-600 text-white'
                  : 'bg-sky-600 hover:bg-sky-500 text-white active:scale-95 disabled:bg-slate-800 disabled:text-slate-500 disabled:border disabled:border-slate-700'
              }`}
            >
              {appliedNotice ? <Check className="w-4 h-4" /> : <Send className="w-4 h-4" />}
              <span>{appliedNotice ? t.setpoint.appliedNotice : t.setpoint.applyBtn}</span>
            </button>
          </Tooltip>
        </div>
      </div>

      {/* Quick Presets */}
      <div className="flex flex-wrap items-center justify-between gap-2 pt-1 border-t border-slate-700/50">
        <span className="text-[11px] text-slate-500">{t.setpoint.boundaryNotice}</span>
        <div className="flex flex-wrap gap-1.5">
          <button
            onClick={() => handlePreset(0)}
            disabled={disabled}
            className="px-2.5 py-1 rounded bg-[#1E1F22] hover:bg-slate-750 text-slate-300 text-xs font-mono border border-slate-700 transition-colors"
          >
            {t.setpoint.presetMin}
          </button>
          <button
            onClick={() => handlePreset(400)}
            disabled={disabled}
            className="px-2.5 py-1 rounded bg-[#1E1F22] hover:bg-slate-750 text-emerald-300 text-xs font-mono border border-slate-700 transition-colors"
          >
            {t.setpoint.presetBandMin}
          </button>
          <button
            onClick={() => handlePreset(500)}
            disabled={disabled}
            className="px-2.5 py-1 rounded bg-[#1E1F22] hover:bg-slate-750 text-sky-300 text-xs font-mono border border-slate-700 transition-colors"
          >
            {t.setpoint.presetCenter}
          </button>
          <button
            onClick={() => handlePreset(600)}
            disabled={disabled}
            className="px-2.5 py-1 rounded bg-[#1E1F22] hover:bg-slate-750 text-emerald-300 text-xs font-mono border border-slate-700 transition-colors"
          >
            {t.setpoint.presetBandMax}
          </button>
          <button
            onClick={() => handlePreset(1000)}
            disabled={disabled}
            className="px-2.5 py-1 rounded bg-[#1E1F22] hover:bg-slate-750 text-slate-300 text-xs font-mono border border-slate-700 transition-colors"
          >
            {t.setpoint.presetMax}
          </button>
        </div>
      </div>
    </section>
  );
};
