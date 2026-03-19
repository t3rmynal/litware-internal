import { useState } from 'react'
import { motion } from 'framer-motion'

interface Props {
  value: boolean
  onChange: (v: boolean) => void
  label: string
  dimLabel?: boolean
}

export default function Toggle({ value, onChange, label, dimLabel }: Props) {
  const [hovered, setHovered] = useState(false)

  return (
    <button
      onClick={() => onChange(!value)}
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
        gap: 8,
        transition: 'background 0.12s'
      }}
    >
      <span style={{
        fontFamily: 'JetBrains Mono, monospace',
        fontSize: 11,
        color: 'var(--text)',
        letterSpacing: '0.03em',
        opacity: dimLabel ? 0.5 : 1,
        transition: 'opacity 0.12s'
      }}>
        {label}
      </span>

      <div style={{
        position: 'relative',
        width: 30,
        height: 15,
        borderRadius: 8,
        background: value ? 'var(--accent)' : 'var(--surface3)',
        transition: 'background 0.18s ease, box-shadow 0.18s ease',
        flexShrink: 0,
        border: `1px solid ${value ? 'var(--accent-border)' : 'var(--border2)'}`,
        boxShadow: value ? '0 0 8px var(--accent-glow)' : 'none'
      }}>
        <motion.div
          animate={{ x: value ? 16 : 1 }}
          transition={{ type: 'spring', stiffness: 520, damping: 38 }}
          style={{
            position: 'absolute',
            top: 2,
            width: 9,
            height: 9,
            borderRadius: '50%',
            background: value ? '#fff' : 'var(--text-muted)',
            boxShadow: value ? '0 1px 3px rgba(0,0,0,0.5)' : 'none'
          }}
        />
      </div>
    </button>
  )
}
