import React from 'react';

/**
 * ISA-101 style alarm priority marker.
 * Priority is coded redundantly by color, shape and number so it stays
 * readable for color-blind operators and on monochrome displays.
 *   1 = red square, 2 = orange triangle, 3 = yellow diamond, 0 = hollow (normal).
 */
interface AlarmMarkerProps {
  priority: 0 | 1 | 2 | 3;
  size?: number;
}

export const AlarmMarker: React.FC<AlarmMarkerProps> = ({ priority, size = 18 }) => {
  const s = size;
  const label = priority > 0 ? String(priority) : '';
  return (
    <svg width={s} height={s} viewBox="0 0 20 20" aria-hidden="true" className="flex-shrink-0">
      {priority === 1 && <rect x="1" y="1" width="18" height="18" fill="#C80000" stroke="#141414" strokeWidth="1" />}
      {priority === 2 && <polygon points="10,1 19,19 1,19" fill="#D97500" stroke="#141414" strokeWidth="1" />}
      {priority === 3 && <polygon points="10,1 19,10 10,19 1,10" fill="#C9A800" stroke="#141414" strokeWidth="1" />}
      {priority === 0 && <rect x="3" y="3" width="14" height="14" fill="none" stroke="#6B6B6B" strokeWidth="1.5" />}
      {label && (
        <text
          x="10"
          y={priority === 2 ? 16.5 : 14.5}
          textAnchor="middle"
          fontSize="11"
          fontWeight="700"
          fontFamily="Consolas, monospace"
          fill={priority === 1 ? '#FFFFFF' : '#141414'}
        >
          {label}
        </text>
      )}
    </svg>
  );
};
