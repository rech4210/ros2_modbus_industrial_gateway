import React from 'react';
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

  // Buttons are grey and identical in form; availability is shown by the enabled state,
  // and the disabled reason is one hover away. RESET gets an orange edge only when an
  // alarm is waiting on it, which ties the button to the alarm in the banner.
  const buttons: {
    action: 'START' | 'STOP' | 'RESET';
    label: string;
    enabled: boolean;
    tooltip: string;
    cue?: string;
  }[] = [
    { action: 'START', label: t.controls.start, enabled: canStart, tooltip: getStartTooltip() },
    { action: 'STOP', label: t.controls.stop, enabled: canStop, tooltip: getStopTooltip() },
    {
      action: 'RESET',
      label: t.controls.reset,
      enabled: canClear,
      tooltip: getResetTooltip(),
      cue: canClear ? 'border-l-[6px] border-l-p2' : '',
    },
  ];

  return (
    <section className="panel">
      <div className="panel-head">
        <span>{t.controls.title}</span>
        <span className="panel-meta">hold 1.5 s to confirm</span>
      </div>

      <div className="p-3 space-y-2">
        {buttons.map((b) => (
          <Tooltip key={b.action} content={b.tooltip} position="left" className="w-full">
            <button
              disabled={!b.enabled}
              onClick={() => onSelectAction(b.action)}
              className={`btn w-full min-h-[52px] px-4 flex items-center justify-between text-[15px] font-semibold ${b.cue ?? ''}`}
            >
              <span>{b.label}</span>
              <span className="num text-[11px] font-normal text-hmi-faint">
                {b.enabled ? '' : physicalEstop && b.action === 'RESET' ? 'LOCKED' : '—'}
              </span>
            </button>
          </Tooltip>
        ))}
      </div>
    </section>
  );
};
