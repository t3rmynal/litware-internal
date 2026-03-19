import { useStore } from '../../store/useStore'
import Section from '../../controls/Section'
import Toggle from '../../controls/Toggle'

export default function MiscTab() {
  const { misc, set } = useStore()
  const upd = (v: Partial<typeof misc>) => set('misc', v)

  return (
    <div>
      <Section title="Movement">
        <Toggle label="Bunny Hop" value={misc.bhopEnabled} onChange={v => upd({ bhopEnabled: v })} />
        <Toggle label="Strafe helper" value={misc.strafeEnabled} onChange={v => upd({ strafeEnabled: v })} />
        <Toggle label="Auto stop" value={misc.autoStop} onChange={v => upd({ autoStop: v })} />
      </Section>

    </div>
  )
}
