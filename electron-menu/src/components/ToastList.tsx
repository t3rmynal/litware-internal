import { useEffect, useState } from 'react'
import { AnimatePresence, motion } from 'framer-motion'

interface Toast {
  id: number
  text: string
}

let _nextId = 1

export default function ToastList() {
  const [toasts, setToasts] = useState<Toast[]>([])

  // слушаем websocket уведомления
  useEffect(() => {
    const litware = (window as any).litware as { on: (cb: (key: string, value: unknown) => void) => () => void } | undefined
    if (!litware?.on) return

    const unsub = litware.on((key, value) => {
      if (key !== 'notification') return
      const text = String(value)
      const id = _nextId++
      setToasts(prev => [...prev, { id, text }])
      // убираем тост через 4.5 секунды
      setTimeout(() => {
        setToasts(prev => prev.filter(t => t.id !== id))
      }, 4500)
    })

    return unsub
  }, [])

  return (
    <div
      style={{
        position: 'fixed',
        bottom: 24,
        right: 24,
        display: 'flex',
        flexDirection: 'column',
        gap: 10,
        zIndex: 9999,
        pointerEvents: 'none',
      }}
    >
      <AnimatePresence mode="sync">
        {toasts.map(t => (
          <motion.div
            key={t.id}
            layout
            initial={{ opacity: 0, x: 60, scale: 0.92 }}
            animate={{ opacity: 1, x: 0, scale: 1 }}
            exit={{ opacity: 0, x: 40, scale: 0.9 }}
            transition={{ duration: 0.35, ease: [0.16, 1, 0.3, 1] }}
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: 10,
              padding: '10px 16px',
              background: 'rgba(12, 12, 16, 0.88)',
              border: '1px solid rgba(255,255,255,0.07)',
              borderLeft: '3px solid var(--accent)',
              borderRadius: 8,
              backdropFilter: 'blur(12px)',
              boxShadow: '0 4px 24px rgba(0,0,0,0.45)',
              minWidth: 240,
              maxWidth: 360,
            }}
          >
            {/* иконка чекмарка */}
            <svg width={16} height={16} viewBox="0 0 16 16" fill="none" style={{ flexShrink: 0 }}>
              <circle cx={8} cy={8} r={7.5} stroke="var(--accent)" strokeOpacity={0.3} />
              <path
                d="M4.5 8.2 L6.8 10.5 L11.5 5.5"
                stroke="var(--accent)"
                strokeWidth={1.6}
                strokeLinecap="round"
                strokeLinejoin="round"
              />
            </svg>

            <span
              style={{
                fontFamily: 'JetBrains Mono, monospace',
                fontSize: 12,
                color: 'var(--text)',
                letterSpacing: '0.02em',
                lineHeight: 1.4,
              }}
            >
              {t.text}
            </span>

            {/* прогресс-бар снизу */}
            <motion.div
              initial={{ scaleX: 1 }}
              animate={{ scaleX: 0 }}
              transition={{ duration: 4.5, ease: 'linear' }}
              style={{
                position: 'absolute',
                bottom: 0,
                left: 0,
                right: 0,
                height: 2,
                background: 'var(--accent)',
                opacity: 0.35,
                borderRadius: '0 0 8px 8px',
                transformOrigin: 'left',
              }}
            />
          </motion.div>
        ))}
      </AnimatePresence>
    </div>
  )
}
