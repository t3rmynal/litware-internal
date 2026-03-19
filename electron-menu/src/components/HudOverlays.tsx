import { useEffect, useRef, useState } from 'react'
import { useStore } from '../store/useStore'

function hasAnyKeybinds(aimbot: { key: string; triggerKey: string }, world: { keybindsEnabled: boolean }) {
  if (!world.keybindsEnabled) return false
  const keys = [aimbot.key, aimbot.triggerKey]
  return keys.some(k => k && k !== 'None' && k !== '')
}

function useTime() {
  const [time, setTime] = useState(() => new Date())
  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000)
    return () => clearInterval(id)
  }, [])
  return time.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function useFps() {
  const [fps, setFps] = useState(0)
  const frames = useRef(0)
  const last = useRef(performance.now())
  const raf = useRef(0)
  useEffect(() => {
    const tick = () => {
      frames.current++
      const now = performance.now()
      if (now - last.current >= 1000) {
        setFps(frames.current)
        frames.current = 0
        last.current = now
      }
      raf.current = requestAnimationFrame(tick)
    }
    raf.current = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(raf.current)
  }, [])
  return fps
}

export default function HudOverlays() {
  const world = useStore(s => s.world)
  const aimbot = useStore(s => s.aimbot)
  const time = useTime()
  const fps = useFps()

  const showKeybinds = hasAnyKeybinds(aimbot, world)

  return (
    <div style={{
      position: 'fixed',
      inset: 0,
      pointerEvents: 'none',
      zIndex: 1000
    }}>
      {/* Watermark */}
      {world.watermarkEnabled && (
        <div style={{
          position: 'absolute',
          top: 12,
          left: 12,
          display: 'flex',
          alignItems: 'center',
          gap: 8,
          padding: '5px 10px',
          background: 'rgba(0,0,0,0.45)',
          border: '1px solid rgba(255,255,255,0.06)',
          borderRadius: 6,
          backdropFilter: 'blur(4px)'
        }}>
          {/* inline logo */}
          <svg width={28} height={13} viewBox="0 0 44 20" fill="none">
            <line x1={2} y1={2} x2={2} y2={17} stroke="#E4E4EC" strokeWidth={2} strokeLinecap="square" />
            <line x1={2} y1={17} x2={11} y2={17} stroke="#E4E4EC" strokeWidth={2} strokeLinecap="square" />
            <polyline points="17,3 21,17 25,7 29,17 33,3" stroke="var(--accent)" strokeWidth={2} fill="none" strokeLinecap="round" strokeLinejoin="miter" />
          </svg>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <span style={{ fontFamily: 'VCR OSD Mono, monospace', fontSize: 10, letterSpacing: '0.12em', color: 'var(--text)' }}>
              LIT<span style={{ color: 'var(--accent)' }}>WARE</span>
            </span>
            <div style={{ display: 'flex', gap: 6, alignItems: 'center', fontFamily: 'JetBrains Mono, monospace', fontSize: 8, letterSpacing: '0.06em' }}>
              <span style={{ color: 'var(--accent)', opacity: 0.9 }}>{fps} fps</span>
              <span style={{ color: 'var(--text-muted)', opacity: 0.35 }}>·</span>
              <span style={{ color: 'var(--text-dim)', opacity: 0.7 }}>{time}</span>
            </div>
          </div>
        </div>
      )}

      {/* Spectator list */}
      {world.spectatorListEnabled && (
        <div style={{
          position: 'absolute',
          top: 40,
          right: 12,
          minWidth: 140,
          padding: '8px 10px',
          background: 'rgba(0,0,0,0.5)',
          borderRadius: 6,
          border: '1px solid var(--border)',
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 11,
          color: 'var(--text)'
        }}>
          <div style={{ color: 'var(--accent)', marginBottom: 4 }}>Spectators</div>
          <div style={{ color: 'var(--text-dim)', fontSize: 10 }}>—</div>
        </div>
      )}

      {/* Bomb timer */}
      {world.bombTimerEnabled && (
        <div style={{
          position: 'absolute',
          bottom: 80,
          left: '50%',
          transform: 'translateX(-50%)',
          padding: '6px 14px',
          background: 'rgba(0,0,0,0.6)',
          borderRadius: 6,
          border: '1px solid var(--border)',
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 13,
          color: 'var(--text)'
        }}>
          C4 · —
        </div>
      )}

      {/* Keybinds — только если есть привязки */}
      {showKeybinds && (
        <div style={{
          position: 'absolute',
          bottom: 12,
          left: 12,
          display: 'flex',
          flexDirection: 'column',
          gap: 4,
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 10,
          color: 'var(--text-dim)'
        }}>
          {aimbot.key && aimbot.key !== 'None' && <span>Aimbot · {aimbot.key}</span>}
          {aimbot.triggerKey && aimbot.triggerKey !== 'None' && <span>Trigger · {aimbot.triggerKey}</span>}
        </div>
      )}
    </div>
  )
}
