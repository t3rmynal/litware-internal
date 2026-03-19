import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'
import Slider from '../../controls/Slider'
import Dropdown from '../../controls/Dropdown'
import ColorPicker from '../../controls/ColorPicker'
import Reveal from '../../controls/Reveal'
const BOX_STYLES = ['Box', 'Corners', '3D']

export default function VisualsTab() {
  const { visuals, set } = useStore()
  const upd = (v: Partial<typeof visuals>) => set('visuals', v)

  return (
    <div>
      <Section title="ESP">
        <Toggle label="Enable" value={visuals.espEnabled} onChange={v => upd({ espEnabled: v })} />
        <Reveal show={visuals.espEnabled}>
          <Dropdown label="Box style" value={visuals.boxStyle} options={BOX_STYLES} onChange={v => upd({ boxStyle: v })} />
          <ColorPicker label="Enemy color" value={visuals.enemyColor} onChange={v => upd({ enemyColor: v })} />
          <ColorPicker label="Team color" value={visuals.teamColor} onChange={v => upd({ teamColor: v })} />
          <div style={{ height: 1, background: 'var(--border)', margin: '4px 0' }} />
          <Toggle label="Name" value={visuals.name} onChange={v => upd({ name: v })} />
          <Toggle label="Health" value={visuals.health} onChange={v => upd({ health: v })} />
          <Toggle label="Distance" value={visuals.distance} onChange={v => upd({ distance: v })} />
          <Toggle label="Weapon" value={visuals.weapon} onChange={v => upd({ weapon: v })} />
          <Toggle label="Money" value={visuals.money} onChange={v => upd({ money: v })} />
          <Toggle label="Ammo bar" value={visuals.ammo} onChange={v => upd({ ammo: v })} />
          <Toggle label="Skeleton" value={visuals.skeleton} onChange={v => upd({ skeleton: v })} />
          <Toggle label="Head dot" value={visuals.headDot} onChange={v => upd({ headDot: v })} />
          <Toggle label="Offscreen arrows" value={visuals.offscreenArrows} onChange={v => upd({ offscreenArrows: v })} />
        </Reveal>
      </Section>

      <Section title="Glow ESP">
        <Toggle label="Enable" value={visuals.glowEnabled} onChange={v => upd({ glowEnabled: v })} />
        <Reveal show={visuals.glowEnabled}>
          <ColorPicker label="Enemy color" value={visuals.glowEnemyColor} onChange={v => upd({ glowEnemyColor: v })} />
          <ColorPicker label="Team color" value={visuals.glowTeamColor} onChange={v => upd({ glowTeamColor: v })} />
          <Slider label="Radius" value={visuals.glowRadius} min={2} max={30} step={1} decimals={0} onChange={v => upd({ glowRadius: v })} />
          <Toggle label="Pulse" value={visuals.glowPulse} onChange={v => upd({ glowPulse: v })} />
        </Reveal>
      </Section>

      <Section title="Effects">
        <Toggle label="No flash" value={visuals.noFlash} onChange={v => upd({ noFlash: v })} />
        <Toggle label="No smoke" value={visuals.noSmoke} onChange={v => upd({ noSmoke: v })} />
        <Toggle label="Sound ESP" value={visuals.soundEsp} onChange={v => upd({ soundEsp: v })} />
        <Reveal show={visuals.soundEsp}>
          <ColorPicker label="Indicator color" value={visuals.soundEspColor} onChange={v => upd({ soundEspColor: v })} />
        </Reveal>
      </Section>
    </div>
  )
}
