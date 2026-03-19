import { motion } from 'framer-motion'
import { Crosshair, Eye, Globe2, Sword, Settings2, Settings } from 'lucide-react'
import { useStore } from '../store/useStore'

const TABS = [
  { label: 'Aimbot',  icon: Crosshair },
  { label: 'Visuals', icon: Eye },
  { label: 'World',   icon: Globe2 },
  { label: 'Skins',   icon: Sword },
  { label: 'Misc',    icon: Settings2 },
  { label: 'Config',  icon: Settings }
]

const VERSION = 'v0.5.0'

export default function Sidebar() {
  const { activeTab, setTab } = useStore()

  return (
    <div style={{
      width: 144,
      flexShrink: 0,
      borderRight: '1px solid var(--border)',
      display: 'flex',
      flexDirection: 'column',
      padding: '10px 8px',
      gap: 1,
      background: 'rgba(0,0,0,0.1)'
    }}>
      {TABS.map((tab, i) => {
        const Icon = tab.icon
        const active = i === activeTab
        return (
          <div key={tab.label} style={{ position: 'relative', borderRadius: 5 }}
            onMouseEnter={e => { if (!active) (e.currentTarget as HTMLElement).style.background = 'rgba(255,255,255,0.03)' }}
            onMouseLeave={e => { if (!active) (e.currentTarget as HTMLElement).style.background = '' }}
          >
            {/* активный фон, двигается вместе с табом */}
            {active && (
              <motion.div
                layoutId="tab-bg"
                transition={{ type: 'spring', stiffness: 440, damping: 40 }}
                style={{
                  position: 'absolute',
                  inset: 0,
                  borderRadius: 5,
                  background: 'var(--accent-dim)',
                  border: '1px solid var(--accent-border)'
                }}
              />
            )}

            {/* левая акцентная полоска активного таба */}
            {active && (
              <motion.div
                layoutId="tab-stripe"
                transition={{ type: 'spring', stiffness: 440, damping: 40 }}
                style={{
                  position: 'absolute',
                  left: 0,
                  top: '22%',
                  bottom: '22%',
                  width: 2,
                  borderRadius: 1,
                  background: 'var(--accent)',
                  boxShadow: '0 0 5px var(--accent-glow)'
                }}
              />
            )}

            <button
              onClick={() => setTab(i)}
              style={{
                position: 'relative',
                zIndex: 1,
                display: 'flex',
                alignItems: 'center',
                gap: 8,
                width: '100%',
                padding: '7px 10px 7px 12px',
                background: 'none',
                border: 'none',
                borderRadius: 5,
                cursor: 'pointer'
              }}
            >
              <Icon
                size={13}
                color={active ? 'var(--accent)' : 'var(--text-dim)'}
                style={{ transition: 'color 0.15s', flexShrink: 0 }}
              />
              <span style={{
                fontSize: 11,
                fontFamily: 'JetBrains Mono, monospace',
                letterSpacing: '0.04em',
                color: active ? 'var(--text)' : 'var(--text-dim)',
                transition: 'color 0.15s'
              }}>
                {tab.label}
              </span>
            </button>
          </div>
        )
      })}

      <div style={{ marginTop: 'auto', paddingTop: 10, borderTop: '1px solid var(--border)' }}>
        <span style={{
          padding: '3px 10px 3px 12px',
          fontSize: 9,
          color: 'var(--text-muted)',
          letterSpacing: '0.08em',
          fontFamily: 'JetBrains Mono, monospace',
          display: 'block'
        }}>
          {VERSION}
        </span>
      </div>
    </div>
  )
}
