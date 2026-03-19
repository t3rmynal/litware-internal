import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'
import Slider from '../../controls/Slider'
import ColorPicker from '../../controls/ColorPicker'
import Dropdown from '../../controls/Dropdown'
import Reveal from '../../controls/Reveal'

const PARTICLE_TYPES = ['Snow', 'Sakura']

export default function WorldTab() {
  const { world, set } = useStore()
  const upd = (v: Partial<typeof world>) => set('world', v)

  return (
    <div>
      <Section title="World">
        <Toggle label="Sky color" value={world.skyColorEnabled} onChange={v => upd({ skyColorEnabled: v })} />
        <Reveal show={world.skyColorEnabled}>
          <ColorPicker label="Color" value={world.skyColor} onChange={v => upd({ skyColor: v })} />
        </Reveal>

        <Toggle label="Hands color" value={world.handsColorEnabled} onChange={v => upd({ handsColorEnabled: v })} />
        <Reveal show={world.handsColorEnabled}>
          <ColorPicker label="Color" value={world.handsColor} onChange={v => upd({ handsColor: v })} />
        </Reveal>

        <Toggle label="FOV changer" value={world.fovEnabled} onChange={v => upd({ fovEnabled: v })} />
        <Reveal show={world.fovEnabled}>
          <Slider label="FOV" value={world.fovValue} min={60} max={130} step={1} decimals={0} onChange={v => upd({ fovValue: v })} />
        </Reveal>
      </Section>

      <Section title="Particles">
        <Toggle label="Enable" value={world.particlesEnabled} onChange={v => upd({ particlesEnabled: v })} />
        <Reveal show={world.particlesEnabled}>
          <Dropdown label="Type" value={world.particleType} options={PARTICLE_TYPES} onChange={v => upd({ particleType: v })} />
          <Slider label="Density" value={world.particleDensity} min={10} max={200} step={5} decimals={0} onChange={v => upd({ particleDensity: v })} />
          <Slider label="Speed" value={world.particleSpeed} min={0.1} max={5.0} step={0.1} decimals={1} onChange={v => upd({ particleSpeed: v })} />
        </Reveal>
      </Section>
    </div>
  )
}
