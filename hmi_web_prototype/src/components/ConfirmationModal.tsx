import React, { useState, useRef, useEffect } from 'react';
import { AlertTriangle, Lock } from 'lucide-react';
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
    <div className="fixed inset-0 bg-black/80 backdrop-blur-sm z-50 flex items-center justify-center p-4">
      <div className="bg-[#2B2D30] border border-[#3E4247] rounded-2xl max-w-md w-full p-6 shadow-2xl space-y-5 animate-in fade-in zoom-in-95 duration-150">
        <div className="flex items-center space-x-3">
          <div className="w-12 h-12 rounded-xl bg-amber-500/20 border border-amber-500/40 flex items-center justify-center text-amber-400">
            <AlertTriangle className="w-6 h-6" />
          </div>
          <div>
            <h3 className="text-lg font-bold text-white tracking-tight">
              {t.modal.title}
            </h3>
            <p className="text-xs text-slate-400 mt-0.5">
              {t.modal.subtitle}
            </p>
          </div>
        </div>

        <div className="bg-[#1E1F22] rounded-xl p-4 border border-slate-700/80 space-y-2.5 text-xs">
          <div className="flex justify-between items-center text-slate-400">
            <span>{t.modal.actionLabel}</span>
            <span className="font-bold text-white text-sm bg-slate-800 px-2 py-0.5 rounded border border-slate-700">
              {getActionLabel()}
            </span>
          </div>
          <div className="flex justify-between items-center text-slate-400">
            <span>{t.modal.targetStation}</span>
            <span className="text-slate-300 font-mono">Station #{stationId.toString().padStart(2, '0')}</span>
          </div>
          <div className="text-slate-300 border-t border-slate-800 pt-2.5 space-y-1">
            <div className="flex items-center gap-1.5">
              <span>⚠️</span>
              <span>{t.modal.safetyCheck1}</span>
            </div>
            <div className="flex items-center gap-1.5 text-slate-400">
              <span>🔒</span>
              <span>{t.modal.safetyCheck2}</span>
            </div>
          </div>
        </div>

        {/* 1.5s Long-Press Confirmation Button */}
        <div className="space-y-3">
          <button
            onMouseDown={handleStartHold}
            onMouseUp={handleEndHold}
            onTouchStart={handleStartHold}
            onTouchEnd={handleEndHold}
            className="relative w-full h-14 bg-slate-700 hover:bg-slate-650 rounded-xl overflow-hidden font-bold text-white flex items-center justify-center transition-all select-none cursor-pointer active:scale-98 shadow-lg border border-slate-600"
          >
            {/* Progress bar filling up */}
            <div
              className="absolute left-0 top-0 bottom-0 bg-emerald-600 transition-all duration-75 ease-linear"
              style={{ width: `${pressProgress}%` }}
            />
            <span className="relative z-10 flex items-center gap-2">
              <Lock className="w-4 h-4" />
              <span>
                {pressProgress > 0
                  ? `${t.modal.holdingMsg} (${Math.round(pressProgress)}%)`
                  : t.modal.holdPrompt}
              </span>
            </span>
          </button>

          <button
            onClick={onClose}
            className="w-full py-2.5 rounded-lg text-xs font-semibold text-slate-400 hover:text-white hover:bg-slate-700/50 transition-colors"
          >
            {t.modal.cancelBtn}
          </button>
        </div>
      </div>
    </div>
  );
};
