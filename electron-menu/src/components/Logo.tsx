// Geometry logo — L + W чистые угловые линии

export default function Logo({ size = 3 }: { size?: number }) {
  const sw = size * 0.7   // strokeWidth
  return (
    <svg
      width={size * 11}
      height={size * 5}
      viewBox="0 0 44 20"
      fill="none"
      style={{ display: 'block', flexShrink: 0 }}
    >
      {/* L */}
      <line x1={2} y1={2} x2={2} y2={17} stroke="#E4E4EC" strokeWidth={sw * 1.4} strokeLinecap="square" />
      <line x1={2} y1={17} x2={11} y2={17} stroke="#E4E4EC" strokeWidth={sw * 1.4} strokeLinecap="square" />

      {/* W — rotated 180° */}
      <polyline
        points="17,3 21,17 25,7 29,17 33,3"
        stroke="var(--accent)"
        strokeWidth={sw * 1.4}
        fill="none"
        strokeLinecap="round"
        strokeLinejoin="miter"
      />
    </svg>
  )
}
