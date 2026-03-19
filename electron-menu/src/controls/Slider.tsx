import { useState, useRef } from 'react'
import { motion, AnimatePresence } from 'framer-motion'

interface Props {
  label: string
  value: number
  min: number
  max: number
  step?: number
  decimals?: number
  onChange: (v: number) => void
}

export default function Slider({ label, value, min, max, step = 0.1, decimals = 1, onChange }: Props) {
  const [hovered, setHovered] = useState(false)
  const [dragging, setDragging] = useState(false)
  const prevVal = useRef(value)
  const dir = value > prevVal.current ? 1 : value < prevVal.current ? -1 : 0
  prevVal.current = value
  const pct = ((value - min) / (max - min)) * 100
  const active = hovered || dragging
  const displayVal = value.toFixed(decimals)

  return (
    <div style={{ padding: '6px 0' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 7, alignItems: 'baseline' }}>
        <span style={{ fontSize: 11, color: 'var(--text)', letterSpacing: '0.03em' }}>
          {label}
        </span>
        <div style={{
          position: 'relative',
          overflow: 'hidden',
          minWidth: 32,
          height: 14,
          display: 'flex',
          justifyContent: 'flex-end',
          alignItems: 'center'
        }}>
          <AnimatePresence mode="popLayout" initial={false}>
            <motion.span
              key={displayVal}
              initial={{ y: dir * 6, opacity: 0 }}
              animate={{ y: 0, opacity: 1 }}
              exit={{ y: dir * -6, opacity: 0 }}
              transition={{ duration: 0.12, ease: 'easeOut' }}
              style={{
                fontSize: 10,
                color: active ? 'var(--accent)' : 'var(--text-dim)',
                fontVariantNumeric: 'tabular-nums',
                fontFamily: 'JetBrains Mono, monospace',
                transition: 'color 0.15s',
                display: 'block',
                textAlign: 'right' as const
              }}
            >
              {displayVal}
            </motion.span>
          </AnimatePresence>
        </div>
      </div>

      <div
        onMouseEnter={() => setHovered(true)}
        onMouseLeave={() => { setHovered(false); setDragging(false) }}
        style={{ position: 'relative', height: 18, display: 'flex', alignItems: 'center' }}
      >
        {/* трек фон */}
        <div style={{
          position: 'absolute',
          left: 0, right: 0,
          height: 2,
          borderRadius: 1,
          background: 'var(--surface3)'
        }}>
          {/* заполнение */}
          <div style={{
            width: `${pct}%`,
            height: '100%',
            background: active
              ? 'linear-gradient(90deg, rgba(204,17,17,0.55) 0%, var(--accent) 100%)'
              : 'rgba(204,17,17,0.55)',
            borderRadius: 1,
            boxShadow: active ? '0 0 5px var(--accent-glow)' : 'none',
            transition: 'width 0.04s, box-shadow 0.15s, background 0.15s'
          }} />
        </div>

        {/* прозрачный инпут */}
        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(parseFloat(e.target.value))}
          onMouseDown={() => setDragging(true)}
          onMouseUp={() => setDragging(false)}
          style={{
            position: 'absolute',
            width: '100%',
            height: '100%',
            opacity: 0,
            cursor: 'pointer',
            margin: 0
          }}
        />

        {/* ползунок */}
        <div style={{
          position: 'absolute',
          left: `calc(${pct}% - 5px)`,
          width: dragging ? 12 : active ? 10 : 8,
          height: dragging ? 12 : active ? 10 : 8,
          marginLeft: dragging ? -1 : active ? 0 : 1,
          borderRadius: '50%',
          background: active ? 'var(--accent)' : '#C8C8D4',
          boxShadow: dragging
            ? '0 0 10px var(--accent-glow), 0 0 0 2px var(--accent-dim)'
            : active
              ? '0 0 6px var(--accent-glow)'
              : '0 1px 3px rgba(0,0,0,0.55)',
          transition: 'width 0.12s, height 0.12s, background 0.15s, box-shadow 0.15s',
          pointerEvents: 'none'
        }} />
      </div>
    </div>
  )
}
