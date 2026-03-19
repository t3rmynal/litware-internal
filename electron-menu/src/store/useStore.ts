import { create } from 'zustand'
import type { AppConfig } from '../types/config'
import { pushSectionToDll } from '../lib/dllKeys'

type SectionKey = 'aimbot' | 'visuals' | 'world' | 'skins' | 'misc' | 'settings'

interface Store extends AppConfig {
  activeTab: number
  setTab: (index: number) => void
  set: <K extends SectionKey>(section: K, value: Partial<AppConfig[K]>) => void
  setAccentColor: (color: string) => void
}

const defaultConfig: AppConfig = {
  aimbot: {
    enabled: false,
    key: 'MOUSE1',
    fov: 5.0,
    smooth: 8.0,
    bone: 0,
    teamCheck: true,
    visCheck: true,
    triggerEnabled: false,
    triggerKey: 'MOUSE2',
    triggerDelay: 50,
    triggerTeamCheck: true,
    rcsEnabled: false,
    rcsX: 1.0,
    rcsY: 1.0,
    rcsSmooth: 3.0
  },
  visuals: {
    espEnabled: false,
    boxStyle: 0,
    name: true,
    health: true,
    distance: true,
    weapon: true,
    money: false,
    ammo: true,
    skeleton: false,
    headDot: false,
    offscreenArrows: true,
    glowEnabled: false,
    glowEnemyColor: '#CC1111',
    glowTeamColor: '#1166CC',
    glowRadius: 12,
    glowPulse: false,
    noFlash: false,
    noSmoke: false,
    soundEsp: false,
    soundEspColor: '#FFDD44',
    enemyColor: '#CC1111',
    teamColor: '#1166CC'
  },
  world: {
    skyColorEnabled: false,
    skyColor: '#3930B0',
    handsColorEnabled: false,
    handsColor: '#E5E5F2',
    fovEnabled: false,
    fovValue: 90,
    particlesEnabled: false,
    particleType: 0,
    particleDensity: 60,
    particleSpeed: 1.0,
    radarEnabled: false,
    bombTimerEnabled: true,
    spectatorListEnabled: true,
    watermarkEnabled: true,
    keybindsEnabled: false
  },
  skins: {
    knife: 0,
    gloves: 0,
    knifeFloat: 0.01,
    knifePattern: 0,
    applySkins: false
  },
  misc: {
    bhopEnabled: false,
    strafeEnabled: false,
    autoStop: false,
    debugConsole: false
  },
  settings: {
    accentColor: '#CC1111',
    menuOpacity: 0.97
  },
  accentColor: '#CC1111'
}

export const useStore = create<Store>((setStore) => ({
  ...defaultConfig,
  activeTab: 0,
  setTab: (index) => setStore({ activeTab: index }),
  set: (section, value) =>
    setStore((state) => {
      const next = { [section]: { ...(state[section] as object), ...value } }
      pushSectionToDll(section, value as Record<string, unknown>)
      return next
    }),
  setAccentColor: (color) => {
    setStore({ accentColor: color })
    pushSectionToDll('settings', { accentColor: color })
  }
}))
