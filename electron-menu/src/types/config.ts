// типы конфига — зеркало C++ структур в render_hook.cpp

export interface AimbotConfig {
  enabled: boolean
  key: string
  fov: number
  smooth: number
  bone: number        // 0=head 1=neck 2=chest 3=stomach
  teamCheck: boolean
  visCheck: boolean

  triggerEnabled: boolean
  triggerKey: string
  triggerDelay: number
  triggerTeamCheck: boolean

  rcsEnabled: boolean
  rcsX: number
  rcsY: number
  rcsSmooth: number
}

export interface VisualsConfig {
  espEnabled: boolean
  boxStyle: number    // 0=box 1=corners 2=3d
  name: boolean
  health: boolean
  distance: boolean
  weapon: boolean
  money: boolean
  ammo: boolean
  skeleton: boolean
  headDot: boolean
  offscreenArrows: boolean

  // Glow ESP
  glowEnabled: boolean
  glowEnemyColor: string
  glowTeamColor: string
  glowRadius: number
  glowPulse: boolean

  noFlash: boolean
  noSmoke: boolean
  soundEsp: boolean
  soundEspColor: string

  enemyColor: string
  teamColor: string
}

export interface WorldConfig {
  skyColorEnabled: boolean
  skyColor: string
  handsColorEnabled: boolean
  handsColor: string
  fovEnabled: boolean
  fovValue: number

  // Частицы
  particlesEnabled: boolean
  particleType: number    // 0=snow 1=sakura
  particleDensity: number
  particleSpeed: number

  // Overlays (хранятся здесь, отображаются в ConfigTab)
  radarEnabled: boolean
  bombTimerEnabled: boolean
  spectatorListEnabled: boolean
  watermarkEnabled: boolean
  keybindsEnabled: boolean
}

export interface SkinsConfig {
  knife: number
  gloves: number
  knifeFloat: number
  knifePattern: number
  applySkins: boolean
}

export interface MiscConfig {
  bhopEnabled: boolean
  strafeEnabled: boolean
  autoStop: boolean
  debugConsole: boolean
}

export interface ConfigSettings {
  accentColor: string
  menuOpacity: number
}

export interface AppConfig {
  aimbot: AimbotConfig
  visuals: VisualsConfig
  world: WorldConfig
  skins: SkinsConfig
  misc: MiscConfig
  settings: ConfigSettings
  accentColor: string
}
