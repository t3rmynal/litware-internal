import { useState, useRef } from 'react'

interface Props {
  label: string
  value: string
  onChange: (v: string) => void
}

export default function ColorPicker({ label, value, onChange }: Props) {
  const [hovered, setHovered] = useState(false)
  const inputRef = useRef<HTMLInputElement>(null)

  return (
    <button
      onClick={() => inputRef.current?.click()}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        width: 'calc(100% + 12px)',
        margin: '0 -6px',
        padding: '6px 6px',
        background: hovered ? 'rgba(255,255,255,0.025)' : 'none',
        border: 'none',
        borderRadius: 4,
        cursor: 'pointer',
        transition: 'background 0.12s'
      }}
    >
      <span style={{ fontSize: 11, color: 'var(--text)', letterSpacing: '0.03em' }}>{label}</span>

      <div style={{ display: 'flex', alignItems: 'center', gap: 7, position: 'relative' }}>
        <span style={{
          fontSize: 10,
          color: 'var(--text-dim)',
          fontVariantNumeric: 'tabular-nums',
          fontFamily: 'JetBrains Mono, monospace',
          letterSpacing: '0.05em'
        }}>
          {value.toUpperCase()}
        </span>
        <div style={{
          width: 18,
          height: 18,
          borderRadius: 4,
          background: value,
          border: `1px solid ${hovered ? 'rgba(255,255,255,0.22)' : 'var(--border2)'}`,
          boxShadow: `0 0 7px ${value}55`,
          transition: 'border-color 0.15s, box-shadow 0.15s',
          flexShrink: 0
        }} />
        <input
          ref={inputRef}
          type="color"
          value={value}
          onChange={e => onChange(e.target.value)}
          style={{ position: 'absolute', opacity: 0, pointerEvents: 'none', width: 0, height: 0 }}
        />
      </div>
    </button>
  )
}
