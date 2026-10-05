import React from 'react';
import { useI18n } from '../context/I18nContext';
import { AlarmMarker } from './AlarmMarker';
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

/**
 * Normal states (RUNNING / READY / IDLE) are told apart by shape and text,
 * not by color. Color appears only when the machine is in an abnormal state.
 * The backend's statusColor/statusBadgeBg hints are intentionally not used here.
 */
export const MachineStateBadge: React.FC<MachineStateBadgeProps> = ({
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

  const indicator = () => {
    if (physicalEstop) return <AlarmMarker priority={1} size={30} />;
    if (isAlarm) return <AlarmMarker priority={2} size={30} />;
    // Running: solid block. Ready: outlined block. Idle: dashed outline.
    if (running) return <span className="block w-[30px] h-[30px] bg-hmi-ink" />;
    if (ready) return <span className="block w-[30px] h-[30px] border-[3px] border-hmi-ink" />;
    return <span className="block w-[30px] h-[30px] border-2 border-dashed border-hmi-faint" />;
  };

  const edge = physicalEstop ? 'border-l-p1' : isAlarm ? 'border-l-p2' : 'border-l-hmi-well';

  return (
    <section className="panel">
      <div className="panel-head">
        <span>{t.machineState.title}</span>
        <span className="panel-meta">
          ST{stationId.toString().padStart(2, '0')} · {t.machineState.isoNorm}
        </span>
      </div>

      <div className="p-3">
      <Tooltip content={getTooltipContent()} position="bottom" className="w-full">
        <div className={`w-full well border-l-[6px] ${edge} px-4 py-3 flex items-center gap-4 cursor-help`}>
          {indicator()}
          <div className="min-w-0">
            <div className="text-[22px] md:text-[26px] font-bold leading-tight text-hmi-ink">
              {getLocalizedStateText()}
            </div>
            <div className="text-[11px] text-hmi-faint mt-0.5 num">{t.machineState.subtext}</div>
          </div>
        </div>
      </Tooltip>
      </div>
    </section>
  );
};
