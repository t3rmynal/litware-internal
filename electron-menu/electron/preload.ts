import { contextBridge, ipcRenderer } from 'electron'

// буфер последних значений — react может подписаться позже чем придёт сообщение
const lastValues: Record<string, unknown> = {}
let messageListeners: Array<(key: string, value: unknown) => void> = []

function notify(key: string, value: unknown) {
  lastValues[key] = value
  messageListeners.forEach(cb => cb(key, value))
}

// dll → main (tcp) → preload → react
ipcRenderer.on('toggle-menu', (_, open: boolean) => {
  notify('menu_open', open)
})

ipcRenderer.on('cs2-focused', (_, focused: boolean) => {
  notify('cs2_focused', focused)
})

ipcRenderer.on('dll-notification', (_, text: unknown) => {
  notify('notification', text)
})

ipcRenderer.on('dll-key', (_, key: string, value: unknown) => {
  notify(key, value)
})

function send(key: string, value: unknown): void {
  ipcRenderer.send('send-to-dll', key, value)
}

function onMessage(cb: (key: string, value: unknown) => void): () => void {
  messageListeners.push(cb)
  // сразу отдаём буферизованные значения — react мог подписаться позже
  Object.entries(lastValues).forEach(([key, value]) => cb(key, value))
  return () => { messageListeners = messageListeners.filter(l => l !== cb) }
}

contextBridge.exposeInMainWorld('electron', {
  moveWindow: (dx: number, dy: number) => ipcRenderer.send('window-move', { dx, dy }),
  closeWindow: () => ipcRenderer.send('window-close'),
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  showWindow: () => ipcRenderer.send('menu-visibility', true),
  hideWindow: () => ipcRenderer.send('menu-visibility', false),
})

contextBridge.exposeInMainWorld('litware', {
  send,
  on: onMessage,
})
