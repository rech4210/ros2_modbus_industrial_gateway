import React from 'react';
import { AlertOctagon, AlertTriangle, CheckCircle2, Info, ShieldAlert } from 'lucide-react';
import { useI18n } from '../context/I18nContext';
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

  return (
    <div
      className={`rounded-xl border transition-all duration-300 p-4 shadow-lg ${
        physicalEstop
          ? 'bg-rose-950/40 border-red-500/80 ring-2 ring-red-500/50 shadow-[0_0_20px_rgba(239,68,68,0.3)] animate-pulse'
          : isAlarm
          ? 'bg-amber-950/40 border-amber-500/80 ring-1 ring-amber-500/40'
          : 'bg-[#2B2D30] border-[#3E4247]'
      }`}
    >
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3">
        <div className="flex items-start space-x-3">
          <div
            className={`w-10 h-10 rounded-lg flex items-center justify-center flex-shrink-0 mt-0.5 ${
              physicalEstop
                ? 'bg-red-500/20 text-red-400 border border-red-500/40'
                : isAlarm
                ? 'bg-amber-500/20 text-amber-400 border border-amber-500/40'
                : 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/30'
            }`}
          >
            {physicalEstop ? (
              <AlertOctagon className="w-6 h-6 animate-bounce" />
            ) : isAlarm ? (
              <AlertTriangle className="w-6 h-6" />
            ) : (
              <CheckCircle2 className="w-6 h-6" />
            )}
          </div>

          <div>
            <div className="flex items-center gap-2">
              <span
                className={`text-xs font-bold uppercase tracking-wider px-2 py-0.5 rounded ${
                  physicalEstop
                    ? 'bg-red-500 text-white'
                    : isAlarm
                    ? 'bg-amber-600 text-white'
                    : 'bg-emerald-700/60 text-emerald-200'
                }`}
              >
                {physicalEstop
                  ? t.safetyBanner.emergencyStopBadge
                  : isAlarm
                  ? `${t.safetyBanner.safetyAlarmBadge} (${getLocalizedAlarmCause()})`
                  : t.safetyBanner.systemHealthyBadge}
              </span>
              <Tooltip content={t.safetyBanner.latchedNotice}>
                <span className="text-[11px] text-slate-400 flex items-center gap-1 cursor-help underline decoration-dotted">
                  <Info className="w-3.5 h-3.5" />
                  ISO 13849-1 Latch
                </span>
              </Tooltip>
            </div>

            <p className="text-sm font-semibold text-slate-100 mt-1">
              {physicalEstop
                ? t.safetyBanner.eStopEmergency
                : isAlarm
                ? t.safetyBanner.alarmActive
                : guideMsg || t.safetyBanner.normal}
            </p>

            {/* Step-by-Step Operator Action Guidance for Alarms */}
            {(physicalEstop || isAlarm) && (
              <div className="mt-2 pt-2 border-t border-slate-700/60 flex flex-wrap gap-y-1 gap-x-4 text-xs font-medium">
                <div
                  className={`flex items-center gap-1.5 ${
                    physicalEstop ? 'text-red-300 font-bold' : 'text-slate-400 line-through'
                  }`}
                >
                  <span className="w-4 h-4 rounded-full bg-slate-800 border border-slate-600 flex items-center justify-center text-[10px]">
                    1
                  </span>
                  <span>{t.safetyBanner.eStopStep1}</span>
                </div>
                <div
                  className={`flex items-center gap-1.5 ${
                    !physicalEstop && isAlarm ? 'text-amber-300 font-bold' : 'text-slate-500'
                  }`}
                >
                  <span className="w-4 h-4 rounded-full bg-slate-800 border border-slate-600 flex items-center justify-center text-[10px]">
                    2
                  </span>
                  <span>{t.safetyBanner.eStopStep2}</span>
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Safety Boundary Category Pill */}
        <Tooltip content={t.safetyBanner.monitoredCategoryTooltip}>
          <div className="flex-shrink-0 self-end sm:self-center font-mono text-[11px] text-slate-400 bg-[#1E1F22] px-3 py-1.5 rounded-lg border border-slate-700 cursor-help">
            {t.safetyBanner.monitoredCategory}
          </div>
        </Tooltip>
      </div>
    </div>
  );
};
