import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'
import Slider from '../../controls/Slider'
import Dropdown from '../../controls/Dropdown'
import Keybind from '../../controls/Keybind'

const BONES = ['Head', 'Neck', 'Chest', 'Stomach']

export default function AimbotTab() {
  const { aimbot, set } = useStore()
  const upd = (v: Partial<typeof aimbot>) => set('aimbot', v)

  return (
    <div>
      <Section title="Legitbot">
        <Toggle label="Enable" value={aimbot.enabled} onChange={v => upd({ enabled: v })} />
        {aimbot.enabled && (<>
          <Keybind label="Key" value={aimbot.key} onChange={v => upd({ key: v })} />
          <Slider label="FOV" value={aimbot.fov} min={0.5} max={30} step={0.5} onChange={v => upd({ fov: v })} />
          <Slider label="Smooth" value={aimbot.smooth} min={1} max={20} step={0.5} onChange={v => upd({ smooth: v })} />
          <Dropdown label="Bone" value={aimbot.bone} options={BONES} onChange={v => upd({ bone: v })} />
          <Toggle label="Team check" value={aimbot.teamCheck} onChange={v => upd({ teamCheck: v })} />
          <Toggle label="Visible only" value={aimbot.visCheck} onChange={v => upd({ visCheck: v })} />
        </>)}
      </Section>

      <Section title="Triggerbot">
        <Toggle label="Enable" value={aimbot.triggerEnabled} onChange={v => upd({ triggerEnabled: v })} />
        {aimbot.triggerEnabled && (<>
          <Keybind label="Key" value={aimbot.triggerKey} onChange={v => upd({ triggerKey: v })} />
          <Slider label="Delay (ms)" value={aimbot.triggerDelay} min={0} max={500} step={5} decimals={0} onChange={v => upd({ triggerDelay: v })} />
          <Toggle label="Team check" value={aimbot.triggerTeamCheck} onChange={v => upd({ triggerTeamCheck: v })} />
        </>)}
      </Section>

      <Section title="Recoil Control (RCS)">
        <Toggle label="Enable" value={aimbot.rcsEnabled} onChange={v => upd({ rcsEnabled: v })} />
        {aimbot.rcsEnabled && (<>
          <Slider label="X axis" value={aimbot.rcsX} min={0} max={2} step={0.05} onChange={v => upd({ rcsX: v })} />
          <Slider label="Y axis" value={aimbot.rcsY} min={0} max={2} step={0.05} onChange={v => upd({ rcsY: v })} />
          <Slider label="Smooth" value={aimbot.rcsSmooth} min={1} max={10} step={0.5} onChange={v => upd({ rcsSmooth: v })} />
        </>)}
      </Section>
    </div>
  )
}
