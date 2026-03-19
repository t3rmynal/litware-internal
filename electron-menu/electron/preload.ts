import { contextBridge, ipcRenderer } from 'electron'

// мост между renderer и main
contextBridge.exposeInMainWorld('electron', {
  moveWindow: (dx: number, dy: number) => ipcRenderer.send('window-move', { dx, dy }),
  closeWindow: () => ipcRenderer.send('window-close'),
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  showWindow: () => ipcRenderer.send('menu-visibility', true),
  hideWindow: () => ipcRenderer.send('menu-visibility', false),
  toastNotify: () => ipcRenderer.send('toast-notify'),
})

// websocket к dll
const WS_URL = 'ws://localhost:37373'
const RECONNECT_DELAY = 3000

let socket: WebSocket | null = null
let messageListeners: Array<(key: string, value: unknown) => void> = []
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

// буфер последних значений — react может подписаться позже чем придёт сообщение
const lastValues: Record<string, unknown> = {}

function connect(): void {
  try {
    socket = new WebSocket(WS_URL)
    socket.onopen = () => { console.log('[litware] подключились к dll') }
    socket.onmessage = (e: MessageEvent) => {
      try {
        const data = JSON.parse(e.data as string) as { key: string; value: unknown }
        lastValues[data.key] = data.value
        messageListeners.forEach(cb => cb(data.key, data.value))
        // overlay_visible от c++ → сообщаем main process чтобы показал/скрыл окно
        if (data.key === 'overlay_visible') {
          const visible = data.value === true || data.value === 'true'
          ipcRenderer.send('overlay-visible', visible)
        }
      } catch { console.warn('[litware] не удалось распарсить:', e.data) }
    }
    socket.onerror = () => { console.log('[litware] dll недоступна, переподключение...') }
    socket.onclose = () => {
      socket = null
      if (reconnectTimer) clearTimeout(reconnectTimer)
      reconnectTimer = setTimeout(connect, RECONNECT_DELAY)
    }
  } catch (err) { console.log('[litware] websocket:', err) }
}

function send(key: string, value: unknown): void {
  const valueStr = typeof value === 'string' ? value : JSON.stringify(value)
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ key, value: valueStr }))
  }
}

function onMessage(cb: (key: string, value: unknown) => void): () => void {
  messageListeners.push(cb)
  // сразу отдаём буферизованные значения — react может подписаться позже
  Object.entries(lastValues).forEach(([key, value]) => cb(key, value))
  return () => { messageListeners = messageListeners.filter(l => l !== cb) }
}

// main toggles menu via globalShortcut → сообщаем react + шлём в dll
ipcRenderer.on('toggle-menu', (_, open: boolean) => {
  lastValues['menu_open'] = open
  messageListeners.forEach(cb => cb('menu_open', open))
})

// main хочет отправить что-то в dll напрямую
ipcRenderer.on('send-to-dll', (_, key: string, value: string) => {
  send(key, value)
})

contextBridge.exposeInMainWorld('litware', {
  send,
  on: onMessage,
  connect
})

connect()
