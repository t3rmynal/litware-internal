import { useRef } from 'react'
import Logo from './Logo'

const VERSION = 'v0.5.0'

export default function TitleBar() {
  const dragOrigin = useRef<{ mx: number; my: number } | null>(null)

  const onMouseDown = (e: React.MouseEvent) => {
    dragOrigin.current = { mx: e.screenX, my: e.screenY }
    const onMove = (ev: MouseEvent) => {
      if (!dragOrigin.current) return
      const dx = ev.screenX - dragOrigin.current.mx
      const dy = ev.screenY - dragOrigin.current.my
      dragOrigin.current = { mx: ev.screenX, my: ev.screenY }
      ;(window as any).electron?.moveWindow(dx, dy)
    }
    const onUp = () => {
      dragOrigin.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  return (
    <div
      onMouseDown={onMouseDown}
      style={{
        height: 56,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '0 20px',
        borderBottom: '1px solid var(--border)',
        cursor: 'move',
        flexShrink: 0,
        background: 'linear-gradient(180deg, rgba(255,255,255,0.018) 0%, transparent 100%)'
      }}
    >
      {/* branding */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <Logo size={3} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
          <div style={{ display: 'flex', alignItems: 'baseline' }}>
            <span style={{ fontFamily: 'VCR OSD Mono, monospace', fontSize: 15, letterSpacing: '0.14em', color: 'var(--text)' }}>LIT</span>
            <span style={{ fontFamily: 'VCR OSD Mono, monospace', fontSize: 15, letterSpacing: '0.14em', color: 'var(--accent)', textShadow: '0 0 14px var(--accent-glow)' }}>WARE</span>
          </div>
          <div style={{ fontSize: 9, color: 'var(--text-dim)', letterSpacing: '0.1em', display: 'flex', gap: 4, alignItems: 'center' }}>
            <span>INTERNAL</span>
            <span style={{ opacity: 0.35 }}>·</span>
            <span style={{ color: 'var(--accent)', opacity: 0.65 }}>t3rmynal</span>
          </div>
        </div>
      </div>

      {/* version */}
      <div style={{
        padding: '3px 9px',
        background: 'var(--surface2)',
        border: '1px solid var(--border)',
        borderRadius: 4,
        fontSize: 9,
        color: 'var(--text-dim)',
        letterSpacing: '0.1em',
        fontFamily: 'JetBrains Mono, monospace'
      }}>
        {VERSION}
      </div>
    </div>
  )
}
