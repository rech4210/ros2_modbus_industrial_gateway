import React from 'react';
import { useI18n } from '../context/I18nContext';
import { AlarmMarker } from './AlarmMarker';
import { Tooltip } from './Tooltip';

interface CommHealthMetricsProps {
  rttMs: number;
  jitterMs: number;
  heartbeat: number;
  linkState: number;
  linkStateText: string;
  consecutiveFailures: number;
  ioErrorCount: number;
  busFreqHz: number;
}

interface Row {
  label: string;
  tooltip: string;
  value: React.ReactNode;
  limit: string;
  ok: boolean;
}

export const CommHealthMetrics: React.FC<CommHealthMetricsProps> = ({
  rttMs,
  jitterMs,
  heartbeat,
  linkState,
  linkStateText,
  consecutiveFailures,
  ioErrorCount,
  busFreqHz,
}) => {
  const { t } = useI18n();

  const isRttNominal = rttMs <= 5.0;
  const isJitterNominal = jitterMs <= 2.0;
  const isLinkUp = linkState === 2;

  const rows: Row[] = [
    {
      label: t.qos.readRtt,
      tooltip: t.qos.rttTooltip,
      value: <>{rttMs.toFixed(2)} <span className="text-hmi-faint">ms</span></>,
      limit: '< 5.0 ms',
      ok: isRttNominal,
    },
    {
      label: t.qos.pollingJitter,
      tooltip: t.qos.jitterTooltip,
      value: <>{jitterMs.toFixed(3)} <span className="text-hmi-faint">ms</span></>,
      limit: '< 2.0 ms',
      ok: isJitterNominal,
    },
    {
      label: t.qos.heartbeat,
      tooltip: t.qos.heartbeatTooltip,
      value: <>{heartbeat} <span className="text-hmi-faint">/ 65535</span></>,
      limit: 'incrementing',
      ok: true,
    },
    {
      label: t.qos.linkState,
      tooltip: t.qos.linkTooltip,
      value: linkStateText,
      limit: 'OPERATIONAL',
      ok: isLinkUp,
    },
  ];

  return (
    <section className="panel">
      <div className="panel-head">
        <span>{t.qos.title}</span>
        <span className="panel-meta">{t.qos.deterministic}</span>
      </div>

      <table className="w-full text-[12px]">
        <tbody>
          {rows.map((r) => (
            <tr key={r.label} className="border-b border-hmi-line/60 last:border-b-0">
              <td className="pl-3 pr-2 py-1.5 w-6">
                {r.ok ? <span className="block w-[14px]" /> : <AlarmMarker priority={2} size={14} />}
              </td>
              <td className="pr-2 py-1.5 text-hmi-dim">
                <Tooltip content={r.tooltip} position="right">
                  <span className="cursor-help underline decoration-dotted decoration-hmi-line underline-offset-2">{r.label}</span>
                </Tooltip>
              </td>
              <td className={`pr-3 py-1.5 num text-right text-[14px] ${r.ok ? 'text-hmi-ink' : 'text-hmi-ink font-bold'}`}>
                {r.value}
              </td>
              <td className="pr-3 py-1.5 num text-right text-[11px] text-hmi-faint w-28 hidden sm:table-cell">{r.limit}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <div className="flex flex-wrap items-center justify-between gap-2 px-3 py-1.5 border-t border-hmi-line num text-[11px] text-hmi-faint">
        <div className="flex flex-wrap gap-x-4">
          <span>DDS {busFreqHz.toFixed(1)} Hz</span>
          <span>
            Failures <span className={consecutiveFailures > 0 ? 'text-hmi-ink font-bold' : 'text-hmi-dim'}>{consecutiveFailures}</span>
          </span>
          <span>
            I/O errors <span className={ioErrorCount > 0 ? 'text-hmi-ink font-bold' : 'text-hmi-dim'}>{ioErrorCount}</span>
          </span>
        </div>
        <span>Modbus FC03 bulk · 12 B</span>
      </div>
    </section>
  );
};
