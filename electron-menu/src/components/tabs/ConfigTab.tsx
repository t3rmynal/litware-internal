import { useState, useEffect } from 'react'
import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'
import ColorPicker from '../../controls/ColorPicker'
import Slider from '../../controls/Slider'
import Dropdown from '../../controls/Dropdown'
import { Save, RotateCcw, FolderOpen, Trash2, Check } from 'lucide-react'
import { motion, AnimatePresence } from 'framer-motion'

const STORAGE_PREFIX = 'litware-cfg-'

function ActionBtn({ icon: Icon, label, onClick, variant = 'default', small = false }: {
  icon: React.ElementType
  label: string
  onClick: () => void
  variant?: 'default' | 'danger' | 'accent'
  small?: boolean
}) {
  return (
    <motion.button
      whileHover={{ scale: 1.02 }}
      whileTap={{ scale: 0.97 }}
      onClick={onClick}
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: small ? 4 : 6,
        padding: small ? '5px 9px' : '7px 13px',
        background: variant === 'accent' ? 'var(--accent-dim)' : 'var(--surface3)',
        border: `1px solid ${variant === 'accent' ? 'var(--accent-border)' : 'var(--border2)'}`,
        borderRadius: 5,
        cursor: 'pointer',
        color: variant === 'accent' ? 'var(--accent)' : 'var(--text)',
        fontSize: small ? 10 : 11,
        fontFamily: 'JetBrains Mono, monospace',
        letterSpacing: '0.04em',
        transition: 'border-color 0.15s, background 0.15s'
      }}
      onMouseEnter={e => {
        const el = e.currentTarget as HTMLElement
        if (variant === 'danger') {
          el.style.borderColor = 'var(--accent-border)'
          el.style.background = 'var(--accent-dim)'
        } else {
          el.style.borderColor = 'var(--accent-border)'
          el.style.background = 'rgba(255,255,255,0.04)'
        }
      }}
      onMouseLeave={e => {
        const el = e.currentTarget as HTMLElement
        el.style.borderColor = variant === 'accent' ? 'var(--accent-border)' : 'var(--border2)'
        el.style.background = variant === 'accent' ? 'var(--accent-dim)' : 'var(--surface3)'
      }}
    >
      <Icon size={small ? 10 : 11} color="var(--accent)" />
      {label}
    </motion.button>
  )
}

function ConfigListItem({ name, onLoad, onDelete }: { name: string; onLoad: () => void; onDelete: () => void }) {
  const [hovered, setHovered] = useState(false)

  return (
    <motion.div
      initial={{ opacity: 0, x: -6 }}
      animate={{ opacity: 1, x: 0 }}
      exit={{ opacity: 0, x: 6, height: 0 }}
      transition={{ duration: 0.15 }}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '5px 8px',
        borderRadius: 4,
        background: hovered ? 'rgba(255,255,255,0.03)' : 'transparent',
        transition: 'background 0.12s',
        cursor: 'pointer'
      }}
      onClick={onLoad}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 7 }}>
        <div style={{ width: 4, height: 4, borderRadius: '50%', background: 'var(--accent)', opacity: 0.6, flexShrink: 0 }} />
        <span style={{ fontSize: 10, color: 'var(--text)', fontFamily: 'JetBrains Mono, monospace', letterSpacing: '0.04em' }}>
          {name}
        </span>
      </div>
      <div style={{ display: 'flex', gap: 4 }}>
        <motion.button
          whileTap={{ scale: 0.9 }}
          onClick={e => { e.stopPropagation(); onLoad() }}
          style={{ background: 'none', border: 'none', cursor: 'pointer', padding: '1px 4px', borderRadius: 3, color: 'var(--text-dim)', fontSize: 9, fontFamily: 'JetBrains Mono, monospace', letterSpacing: '0.04em' }}
          onMouseEnter={e => (e.currentTarget as HTMLElement).style.color = 'var(--accent)'}
          onMouseLeave={e => (e.currentTarget as HTMLElement).style.color = 'var(--text-dim)'}
        >
          <Check size={10} />
        </motion.button>
        <motion.button
          whileTap={{ scale: 0.9 }}
          onClick={e => { e.stopPropagation(); onDelete() }}
          style={{ background: 'none', border: 'none', cursor: 'pointer', padding: '1px 4px', borderRadius: 3, color: 'var(--text-muted)', fontSize: 9 }}
          onMouseEnter={e => (e.currentTarget as HTMLElement).style.color = 'var(--accent)'}
          onMouseLeave={e => (e.currentTarget as HTMLElement).style.color = 'var(--text-muted)'}
        >
          <Trash2 size={10} />
        </motion.button>
      </div>
    </motion.div>
  )
}

export default function ConfigTab() {
  const { settings, accentColor, world, misc, set, setAccentColor } = useStore()
  const upd = (v: Partial<typeof settings>) => set('settings', v)
  const updWorld = (v: Partial<typeof world>) => set('world', v)
  const updMisc = (v: Partial<typeof misc>) => set('misc', v)

  const [savedConfigs, setSavedConfigs] = useState<string[]>([])
  const [saveName, setSaveName] = useState('')
  const [saveFlash, setSaveFlash] = useState(false)

  useEffect(() => {
    const keys = Object.keys(localStorage).filter(k => k.startsWith(STORAGE_PREFIX))
    setSavedConfigs(keys.map(k => k.replace(STORAGE_PREFIX, '')))
  }, [])

  const getSnapshot = () => {
    const s = useStore.getState()
    return { aimbot: s.aimbot, visuals: s.visuals, world: s.world, skins: s.skins, misc: s.misc, accentColor: s.accentColor }
  }

  const saveToList = () => {
    const name = saveName.trim() || `config_${Date.now()}`
    localStorage.setItem(STORAGE_PREFIX + name, JSON.stringify(getSnapshot()))
    setSavedConfigs(prev => prev.includes(name) ? prev : [...prev, name])
    setSaveName('')
    setSaveFlash(true)
    setTimeout(() => setSaveFlash(false), 1200)
  }

  const loadFromList = (name: string) => {
    try {
      const raw = localStorage.getItem(STORAGE_PREFIX + name)
      if (!raw) return
      const data = JSON.parse(raw)
      const s = useStore.getState()
      if (data.aimbot) s.set('aimbot', data.aimbot)
      if (data.visuals) s.set('visuals', data.visuals)
      if (data.world) s.set('world', data.world)
      if (data.skins) s.set('skins', data.skins)
      if (data.misc) s.set('misc', data.misc)
      if (data.accentColor) s.setAccentColor(data.accentColor)
    } catch { /* invalid */ }
  }

  const deleteFromList = (name: string) => {
    localStorage.removeItem(STORAGE_PREFIX + name)
    setSavedConfigs(prev => prev.filter(n => n !== name))
  }

  const exportConfig = () => {
    const json = JSON.stringify(getSnapshot(), null, 2)
    const blob = new Blob([json], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'litware-config.json'
    a.click()
    URL.revokeObjectURL(url)
  }

  const importConfig = () => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'
    input.onchange = (e) => {
      const file = (e.target as HTMLInputElement).files?.[0]
      if (!file) return
      const reader = new FileReader()
      reader.onload = (ev) => {
        try {
          const data = JSON.parse(ev.target?.result as string)
          const s = useStore.getState()
          if (data.aimbot) s.set('aimbot', data.aimbot)
          if (data.visuals) s.set('visuals', data.visuals)
          if (data.world) s.set('world', data.world)
          if (data.skins) s.set('skins', data.skins)
          if (data.misc) s.set('misc', data.misc)
          if (data.accentColor) s.setAccentColor(data.accentColor)
        } catch { /* bad file */ }
      }
      reader.readAsText(file)
    }
    input.click()
  }

  return (
    <div>
      <Section title="Appearance">
        <ColorPicker
          label="Accent color"
          value={accentColor}
          onChange={(c) => { setAccentColor(c); upd({ accentColor: c }) }}
        />
        <Slider
          label="Menu opacity"
          value={settings.menuOpacity}
          min={0.4} max={1} step={0.01} decimals={2}
          onChange={v => upd({ menuOpacity: v })}
        />
      </Section>

      <Section title="Overlays">
        <Toggle label="Radar" value={world.radarEnabled} onChange={v => updWorld({ radarEnabled: v })} />
        <Toggle label="Bomb timer" value={world.bombTimerEnabled} onChange={v => updWorld({ bombTimerEnabled: v })} />
        <Toggle label="Spectators" value={world.spectatorListEnabled} onChange={v => updWorld({ spectatorListEnabled: v })} />
        <Toggle label="Watermark" value={world.watermarkEnabled} onChange={v => updWorld({ watermarkEnabled: v })} />
        <Toggle label="Keybinds" value={world.keybindsEnabled} onChange={v => updWorld({ keybindsEnabled: v })} />
      </Section>

      <Section title="Config list">
        {/* save input */}
        <div style={{ display: 'flex', gap: 5, marginBottom: 6 }}>
          <input
            value={saveName}
            onChange={e => setSaveName(e.target.value)}
            onKeyDown={e => { if (e.key === 'Enter') saveToList() }}
            placeholder="Config name…"
            style={{
              flex: 1,
              padding: '5px 9px',
              background: 'var(--surface3)',
              border: '1px solid var(--border)',
              borderRadius: 4,
              color: 'var(--text)',
              fontSize: 10,
              fontFamily: 'JetBrains Mono, monospace',
              letterSpacing: '0.04em',
              outline: 'none'
            }}
            onFocus={e => (e.currentTarget.style.borderColor = 'var(--accent-border)')}
            onBlur={e => (e.currentTarget.style.borderColor = 'var(--border)')}
          />
          <ActionBtn
            icon={saveFlash ? Check : Save}
            label={saveFlash ? 'Saved' : 'Save'}
            onClick={saveToList}
            variant={saveFlash ? 'accent' : 'default'}
            small
          />
        </div>

        {/* list */}
        {savedConfigs.length === 0 ? (
          <div style={{ padding: '6px 8px', fontSize: 10, color: 'var(--text-muted)', fontFamily: 'JetBrains Mono, monospace', letterSpacing: '0.04em' }}>
            No saved configs
          </div>
        ) : (
          <AnimatePresence>
            {savedConfigs.map(name => (
              <ConfigListItem
                key={name}
                name={name}
                onLoad={() => loadFromList(name)}
                onDelete={() => deleteFromList(name)}
              />
            ))}
          </AnimatePresence>
        )}
      </Section>

      <Section title="File">
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' as const, paddingTop: 3, paddingBottom: 3 }}>
          <ActionBtn icon={Save} label="Export" onClick={exportConfig} />
          <ActionBtn icon={FolderOpen} label="Import" onClick={importConfig} />
          <ActionBtn icon={RotateCcw} label="Reset" variant="danger" onClick={() => {
            if (confirm('Reset all settings to default?')) location.reload()
          }} />
        </div>
      </Section>

      <Section title="Debug">
        <Toggle label="Console" value={misc.debugConsole} onChange={v => updMisc({ debugConsole: v })} />
      </Section>

      <Section title="Connection">
        <ConnStatus />
      </Section>
    </div>
  )
}

function ConnStatus() {
  return (
    <div style={{ padding: '4px 0 2px', display: 'flex', alignItems: 'center', gap: 10 }}>
      <div style={{ position: 'relative', width: 8, height: 8, flexShrink: 0 }}>
        <div style={{
          width: 6,
          height: 6,
          borderRadius: '50%',
          background: '#2E2E3E',
          position: 'absolute',
          top: 1, left: 1
        }} />
      </div>

      <div>
        <div style={{ fontSize: 11, color: 'var(--text)', letterSpacing: '0.03em' }}>
          C++ bridge
          <span style={{
            display: 'inline-block',
            marginLeft: 7,
            fontSize: 9,
            padding: '1px 6px',
            borderRadius: 3,
            background: 'var(--surface3)',
            color: 'var(--text-dim)',
            letterSpacing: '0.08em',
            fontFamily: 'JetBrains Mono, monospace',
            verticalAlign: 'middle'
          }}>
            OFFLINE
          </span>
        </div>
        <div style={{ fontSize: 9, color: 'var(--text-muted)', marginTop: 3, letterSpacing: '0.06em' }}>
          ws://localhost:37373
        </div>
      </div>
    </div>
  )
}
