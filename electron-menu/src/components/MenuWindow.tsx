import { motion, AnimatePresence } from 'framer-motion'
import TitleBar from './TitleBar'
import Sidebar from './Sidebar'
import { useStore } from '../store/useStore'
import AimbotTab from './tabs/AimbotTab'
import VisualsTab from './tabs/VisualsTab'
import WorldTab from './tabs/WorldTab'
import SkinsTab from './tabs/SkinsTab'
import MiscTab from './tabs/MiscTab'
import ConfigTab from './tabs/ConfigTab'

const TABS = [AimbotTab, VisualsTab, WorldTab, SkinsTab, MiscTab, ConfigTab]

export default function MenuWindow() {
  const activeTab = useStore(s => s.activeTab)
  const TabComponent = TABS[activeTab] ?? TABS[0]

  return (
    <div style={{
      width: 880,
      height: 580,
      background: 'var(--bg)',
      borderRadius: 10,
      border: '1px solid var(--border)',
      boxShadow: '0 32px 96px rgba(0,0,0,0.9), 0 0 0 1px rgba(255,255,255,0.035)',
      display: 'flex',
      flexDirection: 'column',
      overflow: 'hidden',
      position: 'relative'
    }}>
      {/* акцентное свечение в правом верхнем углу */}
      <div style={{
        position: 'absolute',
        top: -80,
        right: -80,
        width: 240,
        height: 240,
        borderRadius: '50%',
        background: 'var(--accent-glow)',
        filter: 'blur(70px)',
        pointerEvents: 'none',
        opacity: 0.45
      }} />

      {/* тонкая акцентная полоска по центру верхнего края */}
      <div style={{
        position: 'absolute',
        top: 0,
        left: '25%',
        right: '25%',
        height: 1,
        background: 'linear-gradient(90deg, transparent, var(--accent-border), transparent)',
        pointerEvents: 'none'
      }} />

      <TitleBar />

      <div style={{
        display: 'flex',
        flex: 1,
        overflow: 'hidden'
      }}>
        <Sidebar />

        {/* контент таба */}
        <div style={{ flex: 1, overflow: 'hidden', position: 'relative', background: 'rgba(255,255,255,0.006)' }}>
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              initial={{ opacity: 0, x: 10 }}
              animate={{ opacity: 1, x: 0 }}
              exit={{ opacity: 0, x: -10 }}
              transition={{ duration: 0.13, ease: [0.16, 1, 0.3, 1] }}
              style={{
                position: 'absolute',
                inset: 0,
                overflowY: 'auto',
                overflowX: 'hidden',
                padding: '12px 14px',
              }}
            >
              <TabComponent />
            </motion.div>
          </AnimatePresence>
        </div>
      </div>
    </div>
  )
}
