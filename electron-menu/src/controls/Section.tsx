import { motion } from 'framer-motion'
import type { ReactNode } from 'react'

interface Props {
  title: string
  children: ReactNode
}

export default function Section({ title, children }: Props) {
  return (
    <motion.div
      initial={{ opacity: 0, y: 5 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.16 }}
      style={{
        background: 'var(--surface)',
        border: '1px solid var(--border)',
        borderRadius: 7,
        overflow: 'hidden',
        marginBottom: 7
      }}
    >
      {/* заголовок секции */}
      <div style={{
        padding: '7px 12px',
        borderBottom: '1px solid var(--border)',
        display: 'flex',
        alignItems: 'center',
        gap: 7,
        background: 'rgba(255,255,255,0.012)'
      }}>
        {/* акцентный маркер — градиент сверху вниз */}
        <div style={{
          width: 2,
          height: 9,
          borderRadius: 1,
          background: 'linear-gradient(180deg, var(--accent) 0%, rgba(204,17,17,0.25) 100%)',
          flexShrink: 0
        }} />
        <span style={{
          fontSize: 9,
          color: 'var(--text-dim)',
          letterSpacing: '0.12em',
          textTransform: 'uppercase' as const,
          fontFamily: 'JetBrains Mono, monospace',
          fontWeight: 500
        }}>
          {title}
        </span>
      </div>

      <div style={{ padding: '5px 12px 9px' }}>
        {children}
      </div>
    </motion.div>
  )
}
