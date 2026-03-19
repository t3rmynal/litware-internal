import { useEffect, useState } from 'react'
import { motion, AnimatePresence } from 'framer-motion'
import MenuWindow from './components/MenuWindow'
import HudOverlays from './components/HudOverlays'
import ToastList from './components/ToastList'
import { useStore } from './store/useStore'

function hexToRgba(hex: string, alpha: number) {
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  return `rgba(${r},${g},${b},${alpha})`
}

export default function App() {
  const accent  = useStore(s => s.accentColor)
  const opacity = useStore(s => s.settings.menuOpacity)
  const [menuOpen, setMenuOpen] = useState(import.meta.env.DEV)

  // dll шлёт menu_open по websocket
  useEffect(() => {
    const litware = (window as any).litware as { on: (cb: (key: string, value: unknown) => void) => () => void } | undefined
    if (!litware?.on) return
    const unsub = litware.on((key, value) => {
      if (key !== 'menu_open') return
      const open = value === true || value === 'true'
      setMenuOpen(open)
      // сообщаем main process чтобы переключил ignoreMouseEvents
      if (open) {
        ;(window as any).electron?.showWindow?.()
      } else {
        ;(window as any).electron?.hideWindow?.()
      }
    })
    return unsub
  }, [])

  // css переменные акцента
  useEffect(() => {
    const d = document.documentElement.style
    d.setProperty('--accent',        accent)
    d.setProperty('--accent-dim',    hexToRgba(accent, 0.13))
    d.setProperty('--accent-border', hexToRgba(accent, 0.22))
    d.setProperty('--accent-glow',   hexToRgba(accent, 0.28))
  }, [accent])

  return (
    <div style={{ width: '100vw', height: '100vh', display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'transparent' }}>
      <HudOverlays />
      <ToastList />
      <AnimatePresence>
        {menuOpen && (
          <motion.div
            key="menu"
            initial={{ opacity: 0, scale: 0.92 }}
            animate={{ opacity: opacity, scale: 1 }}
            exit={{ opacity: 0, scale: 0.92 }}
            transition={{ duration: 0.22, ease: [0.16, 1, 0.3, 1] }}
          >
            <MenuWindow />
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}
