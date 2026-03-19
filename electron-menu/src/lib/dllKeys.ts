/**
 * Маппинг ключей Electron store -> ключи конфига DLL (render_hook LoadConfigKey*).
 * При вызове set(section, value) пушим в DLL через litware.send(dllKey, valueStr).
 */

declare global {
  interface Window {
    litware?: {
      send: (key: string, value: unknown) => void
      on: (cb: (key: string, value: unknown) => void) => () => void
    }
  }
}

function toStr (v: unknown): string {
  if (typeof v === 'boolean') return v ? 'true' : 'false'
  if (typeof v === 'number') return String(v)
  if (typeof v === 'string') return v
  return JSON.stringify(v)
}

/** Маппинг section.camelKey -> dll_key */
const SECTION_KEY_MAP: Record<string, Record<string, string>> = {
  aimbot: {
    enabled: 'aimbot_enabled',
    key: 'aimbot_key',
    fov: 'aimbot_fov',
    smooth: 'aimbot_smooth',
    bone: 'aimbot_bone',
    teamCheck: 'aimbot_team',
    visCheck: 'esp_vis_check',
    triggerEnabled: 'tb_enabled',
    triggerKey: 'tb_key',
    triggerDelay: 'tb_delay',
    triggerTeamCheck: 'tb_team',
    rcsEnabled: 'rcs_enabled',
    rcsX: 'rcs_x',
    rcsY: 'rcs_y',
    rcsSmooth: 'rcs_smooth'
  },
  visuals: {
    espEnabled: 'esp_enabled',
    boxStyle: 'esp_box_style',
    name: 'esp_name',
    health: 'esp_health',
    distance: 'esp_dist',
    weapon: 'esp_weapon',
    money: 'esp_money',
    ammo: 'esp_ammo',
    skeleton: 'esp_skeleton',
    headDot: 'esp_head_dot',
    offscreenArrows: 'esp_oof',
    glowEnabled: 'glow_enabled',
    glowEnemyColor: 'glow_enemy_col',
    glowTeamColor: 'glow_team_col',
    noFlash: 'no_flash',
    noSmoke: 'no_smoke',
    soundEsp: 'sound_indicators',
    enemyColor: 'esp_enemy_col',
    teamColor: 'esp_team_col'
  },
  world: {
    skyColorEnabled: 'sky_color_enabled',
    skyColor: 'sky_color',
    fovEnabled: 'fov_enabled',
    fovValue: 'fov_value',
    handsColorEnabled: 'hands_color_enabled',
    handsColor: 'hands_color',
    particlesEnabled: 'particles_world',
    radarEnabled: 'radar_ingame',
    bombTimerEnabled: 'bomb_timer',
    spectatorListEnabled: 'spectator_list',
    watermarkEnabled: 'watermark',
    keybindsEnabled: 'keybinds_enabled'
  },
  skins: {
    applySkins: 'skin_enabled'
  },
  misc: {
    bhopEnabled: 'bhop',
    strafeEnabled: 'strafe_enabled',
    autoStop: 'autostop'  // DLL: g_autostopEnabled
  },
  settings: {
    accentColor: 'accent',
    menuOpacity: 'menu_opacity'
  }
}

/** Цвет из hex #RRGGBB в "r g b a" для DLL ParseColor4 */
function hexToConfigColor (hex: string): string {
  const r = parseInt(hex.slice(1, 3), 16) / 255
  const g = parseInt(hex.slice(3, 5), 16) / 255
  const b = parseInt(hex.slice(5, 7), 16) / 255
  return `${r.toFixed(4)} ${g.toFixed(4)} ${b.toFixed(4)} 1.0`
}

export function pushSectionToDll (section: string, value: Record<string, unknown>): void {
  const map = SECTION_KEY_MAP[section]
  if (!map || !window.litware?.send) return

  for (const [camelKey, val] of Object.entries(value)) {
    const dllKey = map[camelKey]
    if (!dllKey) continue
    let v = toStr(val)
    if ((camelKey === 'glowEnemyColor' || camelKey === 'glowTeamColor' || camelKey === 'enemyColor' || camelKey === 'teamColor' || camelKey === 'skyColor' || camelKey === 'accentColor' || camelKey === 'handsColor') && typeof val === 'string') {
      v = hexToConfigColor(val)
    }
    window.litware.send(dllKey, v)
  }
}
