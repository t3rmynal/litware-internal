import { useState, useRef, useEffect } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import { ChevronDown } from 'lucide-react'

interface Props {
  label: string
  value: number
  options: string[]
  onChange: (index: number) => void
}

export default function Dropdown({ label, value, options, onChange }: Props) {
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) {
        setOpen(false)
      }
    }
    document.addEventListener('mousedown', handler)
    return () => document.removeEventListener('mousedown', handler)
  }, [])

  return (
    <div style={{ padding: '5px 0' }} ref={ref}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 5 }}>
        <span style={{ fontSize: 11, color: 'var(--text)', letterSpacing: '0.03em' }}>{label}</span>
      </div>

      <div style={{ position: 'relative' }}>
        <button
          onClick={() => setOpen(!open)}
          style={{
            width: '100%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
            padding: '6px 10px',
            background: open ? 'var(--surface2)' : 'var(--surface3)',
            border: `1px solid ${open ? 'var(--accent-border)' : 'var(--border)'}`,
            borderRadius: 5,
            cursor: 'pointer',
            color: 'var(--text)',
            fontSize: 11,
            letterSpacing: '0.03em',
            transition: 'border-color 0.15s, background 0.15s'
          }}
        >
          <span>{options[value] ?? '—'}</span>
          <motion.div animate={{ rotate: open ? 180 : 0 }} transition={{ duration: 0.15 }}>
            <ChevronDown size={11} color={open ? 'var(--accent)' : 'var(--text-dim)'} style={{ transition: 'color 0.15s' }} />
          </motion.div>
        </button>

        <AnimatePresence>
          {open && (
            <motion.div
              initial={{ opacity: 0, y: -3, scaleY: 0.92 }}
              animate={{ opacity: 1, y: 0, scaleY: 1 }}
              exit={{ opacity: 0, y: -3, scaleY: 0.92 }}
              transition={{ duration: 0.1 }}
              style={{
                position: 'absolute',
                top: 'calc(100% + 4px)',
                left: 0, right: 0,
                background: 'var(--surface2)',
                border: '1px solid var(--border2)',
                borderRadius: 5,
                overflow: 'auto',
                maxHeight: '150px',
                zIndex: 1000,
                transformOrigin: 'top',
                boxShadow: '0 8px 24px rgba(0,0,0,0.5)'
              }}
            >
              {options.map((opt, i) => (
                <button
                  key={i}
                  onClick={() => { onChange(i); setOpen(false) }}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: 6,
                    width: '100%',
                    padding: '7px 10px',
                    textAlign: 'left' as const,
                    background: i === value ? 'var(--accent-dim)' : 'transparent',
                    border: 'none',
                    cursor: 'pointer',
                    color: i === value ? 'var(--accent)' : 'var(--text)',
                    fontSize: 11,
                    letterSpacing: '0.03em',
                    transition: 'background 0.1s'
                  }}
                  onMouseEnter={e => {
                    if (i !== value) (e.currentTarget as HTMLElement).style.background = 'rgba(255,255,255,0.04)'
                  }}
                  onMouseLeave={e => {
                    if (i !== value) (e.currentTarget as HTMLElement).style.background = 'transparent'
                  }}
                >
                  {i === value && (
                    <div style={{ width: 3, height: 3, borderRadius: '50%', background: 'var(--accent)', flexShrink: 0 }} />
                  )}
                  <span style={{ marginLeft: i === value ? 0 : 9 }}>{opt}</span>
                </button>
              ))}
            </motion.div>
          )}
        </AnimatePresence>
      </div>
    </div>
  )
}
