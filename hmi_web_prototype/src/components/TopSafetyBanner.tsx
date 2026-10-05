import React from 'react';
import { useI18n } from '../context/I18nContext';
import { AlarmMarker } from './AlarmMarker';
import { Tooltip } from './Tooltip';

interface TopSafetyBannerProps {
  isAlarm: boolean;
  physicalEstop: boolean;
  alarmCause: number;
  alarmCauseText: string;
  guideMsg: string;
}

export const TopSafetyBanner: React.FC<TopSafetyBannerProps> = ({
  isAlarm,
  physicalEstop,
  alarmCause,
  alarmCauseText,
  guideMsg,
}) => {
  const { t } = useI18n();

  const getLocalizedAlarmCause = () => {
    switch (alarmCause) {
      case 0:
        return t.alarmCauses.none;
      case 1:
        return t.alarmCauses.startup;
      case 2:
        return t.alarmCauses.commTimeout;
      case 3:
        return t.alarmCauses.heartbeatStale;
      case 4:
        return t.alarmCauses.plcInterlock;
      case 5:
        return t.alarmCauses.manualStop;
      case 6:
        return t.alarmCauses.shutdown;
      case 7:
        return t.alarmCauses.internal;
      default:
        return alarmCauseText || t.alarmCauses.unknown;
    }
  };

  const priority: 0 | 1 | 2 = physicalEstop ? 1 : isAlarm ? 2 : 0;
  const edge = priority === 1 ? 'border-l-p1' : priority === 2 ? 'border-l-p2' : 'border-l-hmi-line';
  const active = priority > 0;

  return (
    <section className={`panel border-l-[6px] ${edge}`} aria-live="polite">
      <div className="flex flex-col md:flex-row md:items-stretch">
        {/* Alarm summary */}
        <div className="flex-1 flex items-start gap-3 px-3 py-2.5">
          <div className="pt-0.5">
            <AlarmMarker priority={priority} size={22} />
          </div>
          <div className="min-w-0 flex-1">
            <div className="flex flex-wrap items-baseline gap-x-3 gap-y-0.5">
              <span className={`text-[13px] font-bold tracking-wide ${active ? 'text-hmi-ink' : 'text-hmi-dim'}`}>
                {physicalEstop
                  ? t.safetyBanner.emergencyStopBadge
                  : isAlarm
                  ? t.safetyBanner.safetyAlarmBadge
                  : t.safetyBanner.systemHealthyBadge}
              </span>
              {isAlarm && !physicalEstop && (
                <span className="text-[13px] text-hmi-ink">{getLocalizedAlarmCause()}</span>
              )}
            </div>

            <p className={`mt-0.5 text-[13px] leading-snug ${active ? 'text-hmi-ink' : 'text-hmi-dim'}`}>
              {physicalEstop
                ? t.safetyBanner.eStopEmergency
                : isAlarm
                ? t.safetyBanner.alarmActive
                : guideMsg || t.safetyBanner.normal}
            </p>

            {/* Recovery procedure: current step in bold, finished step struck through */}
            {active && (
              <ol className="mt-2 grid gap-1 text-[12px] text-hmi-dim">
                <li className={`flex gap-2 ${physicalEstop ? 'text-hmi-ink font-semibold' : 'line-through'}`}>
                  <span className="num w-4 text-right">1</span>
                  <span>{t.safetyBanner.eStopStep1}</span>
                </li>
                <li className={`flex gap-2 ${!physicalEstop && isAlarm ? 'text-hmi-ink font-semibold' : ''}`}>
                  <span className="num w-4 text-right">2</span>
                  <span>{t.safetyBanner.eStopStep2}</span>
                </li>
              </ol>
            )}
          </div>
        </div>

        {/* Reference info: kept quiet, right aligned */}
        <div className="flex md:flex-col items-center md:items-end justify-between md:justify-center gap-1 px-3 py-2 border-t md:border-t-0 md:border-l border-hmi-line text-[11px] text-hmi-faint">
          <Tooltip content={t.safetyBanner.latchedNotice} position="bottom">
            <span className="cursor-help underline decoration-dotted underline-offset-2">ISO 13849-1 latch</span>
          </Tooltip>
          <Tooltip content={t.safetyBanner.monitoredCategoryTooltip} position="bottom">
            <span className="cursor-help num">{t.safetyBanner.monitoredCategory}</span>
          </Tooltip>
        </div>
      </div>
    </section>
  );
};
