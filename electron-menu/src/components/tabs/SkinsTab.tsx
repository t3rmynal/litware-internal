import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'
import Dropdown from '../../controls/Dropdown'
import Slider from '../../controls/Slider'
import Reveal from '../../controls/Reveal'

const KNIVES = ['Default', 'Bayonet', 'Flip', 'Gut', 'Karambit', 'M9 Bayonet', 'Huntsman', 'Bowie', 'Butterfly', 'Shadow Daggers', 'Falchion', 'Navaja', 'Stiletto', 'Ursus', 'Talon', 'Skeleton', 'Nomad', 'Survival', 'Paracord', 'Classic']
const GLOVES = ['Default', 'Sport', 'Slick', 'Leather', 'Wraps', 'Moto', 'Specialist', 'Bloodhound', 'Hydra', 'Broken Fang']

export default function InventoryChangerTab() {
  const { skins, set } = useStore()
  const upd = (v: Partial<typeof skins>) => set('skins', v)

  return (
    <div>
      <Section title="Inventory">
        <Toggle label="Apply skins" value={skins.applySkins} onChange={v => upd({ applySkins: v })} />
      </Section>

      <Section title="Knife">
        <Dropdown label="Model" value={skins.knife} options={KNIVES} onChange={v => upd({ knife: v })} />
        <Reveal show={skins.knife > 0}>
          <Slider
            label="Float"
            value={skins.knifeFloat}
            min={0.0001} max={0.9999} step={0.0001} decimals={4}
            onChange={v => upd({ knifeFloat: v })}
          />
          <Slider
            label="Pattern seed"
            value={skins.knifePattern}
            min={0} max={1000} step={1} decimals={0}
            onChange={v => upd({ knifePattern: v })}
          />
        </Reveal>
      </Section>

      <Section title="Gloves">
        <Dropdown label="Model" value={skins.gloves} options={GLOVES} onChange={v => upd({ gloves: v })} />
      </Section>

      <div style={{ padding: '14px 12px', textAlign: 'center' as const, color: 'var(--text-muted)', fontSize: 10, letterSpacing: '0.07em', fontFamily: 'JetBrains Mono, monospace' }}>
        WEAPON SKINS — COMING SOON
      </div>
    </div>
  )
}
