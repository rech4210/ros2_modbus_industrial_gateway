import React, { useState, useRef, useEffect } from 'react';
import { useI18n } from '../context/I18nContext';

interface ConfirmationModalProps {
  isOpen: boolean;
  action: 'START' | 'STOP' | 'RESET' | null;
  stationId: number;
  onConfirm: (action: 'START' | 'STOP' | 'RESET') => void;
  onClose: () => void;
}

export const ConfirmationModal: React.FC<ConfirmationModalProps> = ({
  isOpen,
  action,
  stationId,
  onConfirm,
  onClose,
}) => {
  const { t } = useI18n();
  const [pressProgress, setPressProgress] = useState(0);
  const timerRef = useRef<number | null>(null);

  useEffect(() => {
    if (!isOpen) {
      setPressProgress(0);
      if (timerRef.current) {
        clearInterval(timerRef.current);
        timerRef.current = null;
      }
    }
  }, [isOpen]);

  if (!isOpen || !action) return null;

  const handleStartHold = () => {
    const startTime = Date.now();
    const duration = 1500; // 1.5 seconds

    timerRef.current = window.setInterval(() => {
      const elapsed = Date.now() - startTime;
      const pct = Math.min(100, (elapsed / duration) * 100);
      setPressProgress(pct);

      if (pct >= 100) {
        if (timerRef.current) clearInterval(timerRef.current);
        onConfirm(action);
      }
    }, 20);
  };

  const handleEndHold = () => {
    if (timerRef.current) {
      clearInterval(timerRef.current);
      timerRef.current = null;
    }
    if (pressProgress < 100) {
      setPressProgress(0);
    }
  };

  const getActionLabel = () => {
    if (action === 'START') return t.controls.start;
    if (action === 'STOP') return t.controls.stop;
    if (action === 'RESET') return t.controls.reset;
    return '';
  };

  return (
    <div className="fixed inset-0 bg-black/45 z-50 flex items-center justify-center p-4">
      <div role="dialog" aria-modal="true" className="panel w-full max-w-md shadow-[0_8px_24px_rgba(0,0,0,0.35)]">
        <div className="flex items-center justify-between px-3 py-2 bg-hmi-head text-white text-[13px] font-semibold">
          <span>{t.modal.title}</span>
          <span className="num text-[11px] font-normal text-white/70">ST{stationId.toString().padStart(2, '0')}</span>
        </div>

        <div className="p-4 space-y-4">
          <p className="text-[12px] text-hmi-dim">{t.modal.subtitle}</p>

          <dl className="well grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 px-3 py-2.5 text-[13px]">
            <dt className="text-hmi-faint">{t.modal.actionLabel}</dt>
            <dd className="font-bold text-hmi-ink">{getActionLabel()}</dd>
            <dt className="text-hmi-faint">{t.modal.targetStation}</dt>
            <dd className="num text-hmi-ink">Station {stationId.toString().padStart(2, '0')}</dd>
          </dl>

          <ol className="space-y-1.5 text-[12px] text-hmi-ink">
            <li className="flex gap-2">
              <span className="num text-hmi-faint">1.</span>
              <span>{t.modal.safetyCheck1}</span>
            </li>
            <li className="flex gap-2">
              <span className="num text-hmi-faint">2.</span>
              <span>{t.modal.safetyCheck2}</span>
            </li>
          </ol>

          {/* Hold-to-confirm: the fill shows how far along the 1.5 s hold is */}
          <button
            onMouseDown={handleStartHold}
            onMouseUp={handleEndHold}
            onTouchStart={handleStartHold}
            onTouchEnd={handleEndHold}
            className="btn relative w-full h-12 overflow-hidden text-[14px] font-semibold select-none"
          >
            <span
              className="absolute left-0 top-0 bottom-0 bg-sel"
              style={{ width: `${pressProgress}%` }}
            />
            <span className={`relative z-10 ${pressProgress > 50 ? 'text-white' : ''}`}>
              {pressProgress > 0
                ? `${t.modal.holdingMsg} ${Math.round(pressProgress)}%`
                : t.modal.holdPrompt}
            </span>
          </button>

          <div className="flex justify-end">
            <button onClick={onClose} className="btn px-4 py-1.5 text-[13px]">
              {t.modal.cancelBtn}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
