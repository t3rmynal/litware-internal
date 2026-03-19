import { useState, useEffect } from 'react'
import { motion, AnimatePresence } from 'framer-motion'

interface Props {
  label: string
  value: string
  onChange: (key: string) => void
}

export default function Keybind({ label, value, onChange }: Props) {
  const [listening, setListening] = useState(false)

  useEffect(() => {
    if (!listening) return

    const handler = (e: KeyboardEvent) => {
      e.preventDefault()
      if (e.key === 'Escape') { setListening(false); return }
      const key = e.code.replace('Key', '').replace('Digit', '')
      onChange(key)
      setListening(false)
    }

    const mouseHandler = (e: MouseEvent) => {
      e.preventDefault()
      const map: Record<number, string> = { 0: 'MOUSE1', 1: 'MOUSE2', 2: 'MOUSE3', 3: 'MOUSE4', 4: 'MOUSE5' }
      onChange(map[e.button] ?? `MOUSE${e.button + 1}`)
      setListening(false)
    }

    window.addEventListener('keydown', handler)
    window.addEventListener('mousedown', mouseHandler)
    return () => {
      window.removeEventListener('keydown', handler)
      window.removeEventListener('mousedown', mouseHandler)
    }
  }, [listening, onChange])

  return (
    <div style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      padding: '6px 0'
    }}>
      <span style={{ fontSize: 11, color: 'var(--text)', letterSpacing: '0.03em' }}>{label}</span>

      <motion.button
        whileTap={{ scale: 0.96 }}
        onClick={() => setListening(l => !l)}
        style={{
          position: 'relative',
          padding: '3px 10px',
          background: listening ? 'var(--accent-dim)' : 'var(--surface3)',
          border: `1px solid ${listening ? 'var(--accent-border)' : 'var(--border2)'}`,
          borderRadius: 4,
          cursor: 'pointer',
          color: listening ? 'var(--accent)' : 'var(--text-dim)',
          fontSize: 10,
          fontFamily: 'JetBrains Mono, monospace',
          letterSpacing: '0.07em',
          minWidth: 64,
          textAlign: 'center' as const,
          transition: 'background 0.15s, border-color 0.15s, color 0.15s',
          boxShadow: listening ? '0 0 8px var(--accent-glow)' : 'none',
          overflow: 'hidden'
        }}
      >
        <AnimatePresence mode="wait">
          {listening ? (
            <motion.span
              key="listening"
              initial={{ opacity: 0, y: 4 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.1 }}
              style={{ display: 'block' }}
            >
              · · ·
            </motion.span>
          ) : (
            <motion.span
              key="value"
              initial={{ opacity: 0, y: 4 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -4 }}
              transition={{ duration: 0.1 }}
              style={{ display: 'block' }}
            >
              {value}
            </motion.span>
          )}
        </AnimatePresence>
      </motion.button>
    </div>
  )
}
