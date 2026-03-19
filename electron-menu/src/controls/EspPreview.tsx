import { useStore } from '../store/useStore'

interface BoxProps {
  x: number
  y: number
  w: number
  h: number
  color: string
  name: string
  hp: number
  dist: number
  weapon: string
  showName: boolean
  showHealth: boolean
  showDistance: boolean
  showWeapon: boolean
  showSkeleton: boolean
  showHeadDot: boolean
  boxStyle: number
}

function EspBox({ x, y, w, h, color, name, hp, dist, weapon, showName, showHealth, showDistance, showWeapon, showSkeleton, showHeadDot, boxStyle }: BoxProps) {
  const hpColor = hp > 60 ? '#44CC66' : hp > 30 ? '#CCAA22' : '#CC3322'
  const cornerSize = Math.min(w, h) * 0.25

  return (
    <g>
      {/* box or corners */}
      {boxStyle === 0 && (
        <rect x={x} y={y} width={w} height={h} fill="none" stroke={color} strokeWidth={1} opacity={0.85} />
      )}
      {boxStyle === 1 && (<>
        {/* top-left */}
        <line x1={x} y1={y + cornerSize} x2={x} y2={y} stroke={color} strokeWidth={1.5} />
        <line x1={x} y1={y} x2={x + cornerSize} y2={y} stroke={color} strokeWidth={1.5} />
        {/* top-right */}
        <line x1={x + w - cornerSize} y1={y} x2={x + w} y2={y} stroke={color} strokeWidth={1.5} />
        <line x1={x + w} y1={y} x2={x + w} y2={y + cornerSize} stroke={color} strokeWidth={1.5} />
        {/* bottom-left */}
        <line x1={x} y1={y + h - cornerSize} x2={x} y2={y + h} stroke={color} strokeWidth={1.5} />
        <line x1={x} y1={y + h} x2={x + cornerSize} y2={y + h} stroke={color} strokeWidth={1.5} />
        {/* bottom-right */}
        <line x1={x + w - cornerSize} y1={y + h} x2={x + w} y2={y + h} stroke={color} strokeWidth={1.5} />
        <line x1={x + w} y1={y + h - cornerSize} x2={x + w} y2={y + h} stroke={color} strokeWidth={1.5} />
      </>)}
      {boxStyle === 2 && (<>
        {/* 3D perspective box */}
        <rect x={x} y={y} width={w} height={h} fill="none" stroke={color} strokeWidth={1} opacity={0.85} />
        <rect x={x + 5} y={y - 4} width={w} height={h} fill="none" stroke={color} strokeWidth={0.5} opacity={0.35} />
        <line x1={x} y1={y} x2={x + 5} y2={y - 4} stroke={color} strokeWidth={0.5} opacity={0.35} />
        <line x1={x + w} y1={y} x2={x + w + 5} y2={y - 4} stroke={color} strokeWidth={0.5} opacity={0.35} />
        <line x1={x + w} y1={y + h} x2={x + w + 5} y2={y + h - 4} stroke={color} strokeWidth={0.5} opacity={0.35} />
      </>)}

      {/* health bar */}
      {showHealth && (<>
        <rect x={x - 4} y={y} width={2} height={h} fill="rgba(0,0,0,0.55)" />
        <rect x={x - 4} y={y + h * (1 - hp / 100)} width={2} height={h * (hp / 100)} fill={hpColor} />
      </>)}

      {/* head dot */}
      {showHeadDot && (
        <circle cx={x + w / 2} cy={y - 4} r={2.5} fill="none" stroke={color} strokeWidth={1} />
      )}

      {/* skeleton lines (simplified) */}
      {showSkeleton && (<>
        <line x1={x + w * 0.5} y1={y + h * 0.15} x2={x + w * 0.5} y2={y + h * 0.55} stroke={color} strokeWidth={0.8} opacity={0.5} />
        <line x1={x + w * 0.2} y1={y + h * 0.25} x2={x + w * 0.8} y2={y + h * 0.25} stroke={color} strokeWidth={0.8} opacity={0.5} />
        <line x1={x + w * 0.5} y1={y + h * 0.55} x2={x + w * 0.3} y2={y + h * 0.9} stroke={color} strokeWidth={0.8} opacity={0.5} />
        <line x1={x + w * 0.5} y1={y + h * 0.55} x2={x + w * 0.7} y2={y + h * 0.9} stroke={color} strokeWidth={0.8} opacity={0.5} />
      </>)}

      {/* labels above box */}
      {showName && (
        <text x={x + w / 2} y={y - (showHeadDot ? 14 : 8)} textAnchor="middle" fill={color} fontSize={7} fontFamily="JetBrains Mono, monospace" opacity={0.9}>
          {name}
        </text>
      )}
      {showDistance && (
        <text x={x + w / 2} y={y - (showName ? (showHeadDot ? 24 : 18) : (showHeadDot ? 14 : 8))} textAnchor="middle" fill="#AAAACC" fontSize={6} fontFamily="JetBrains Mono, monospace" opacity={0.7}>
          {dist}m
        </text>
      )}

      {/* labels below box */}
      {showWeapon && (
        <text x={x + w / 2} y={y + h + 9} textAnchor="middle" fill="#CCCCDD" fontSize={6} fontFamily="JetBrains Mono, monospace" opacity={0.65}>
          {weapon}
        </text>
      )}
    </g>
  )
}

export default function EspPreview() {
  const { visuals } = useStore()

  return (
    <div style={{
      background: 'linear-gradient(160deg, #0C1018 0%, #0E0E14 100%)',
      border: '1px solid var(--border)',
      borderRadius: 5,
      height: 100,
      margin: '4px 0 8px',
      position: 'relative',
      overflow: 'hidden'
    }}>
      {/* fake map background lines */}
      <svg width="100%" height="100%" style={{ position: 'absolute', inset: 0 }}>
        <line x1="0" y1="60" x2="100%" y2="55" stroke="rgba(255,255,255,0.025)" strokeWidth={1} />
        <line x1="0" y1="75" x2="100%" y2="78" stroke="rgba(255,255,255,0.018)" strokeWidth={1} />
        <rect x="20" y="30" width="40" height="45" fill="rgba(255,255,255,0.012)" />
        <rect x="220" y="25" width="55" height="50" fill="rgba(255,255,255,0.012)" />

        {/* enemy */}
        <EspBox
          x={60} y={12} w={28} h={68}
          color={visuals.enemyColor}
          name="xXenemyXx" hp={72} dist={23} weapon="AK-47"
          showName={visuals.name}
          showHealth={visuals.health}
          showDistance={visuals.distance}
          showWeapon={visuals.weapon}
          showSkeleton={visuals.skeleton}
          showHeadDot={visuals.headDot}
          boxStyle={visuals.boxStyle}
        />

        {/* teammate */}
        <EspBox
          x={220} y={18} w={22} h={56}
          color={visuals.teamColor}
          name="teammate" hp={100} dist={41} weapon="M4A4"
          showName={visuals.name}
          showHealth={visuals.health}
          showDistance={visuals.distance}
          showWeapon={visuals.weapon}
          showSkeleton={visuals.skeleton}
          showHeadDot={visuals.headDot}
          boxStyle={visuals.boxStyle}
        />
      </svg>

      {/* preview label */}
      <div style={{
        position: 'absolute',
        bottom: 5,
        right: 8,
        fontSize: 8,
        color: 'var(--text-muted)',
        fontFamily: 'JetBrains Mono, monospace',
        letterSpacing: '0.08em'
      }}>
        PREVIEW
      </div>
    </div>
  )
}
