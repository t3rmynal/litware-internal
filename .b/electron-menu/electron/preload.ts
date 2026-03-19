import { contextBridge, ipcRenderer } from 'electron'

// экспортируем API в рендерер через безопасный мост
contextBridge.exposeInMainWorld('electron', {
  moveWindow: (x: number, y: number) => ipcRenderer.send('window-move', { x, y }),
  closeWindow: () => ipcRenderer.send('window-close'),
  minimizeWindow: () => ipcRenderer.send('window-minimize')
})

// мост к C++ через WebSocket (пока заглушка)
contextBridge.exposeInMainWorld('litware', {
  send: (key: string, value: unknown) => {
    // TODO: пробрасываем в bridge.ts когда подключимся к C++
    console.log('[litware bridge] send:', key, value)
  },
  on: (cb: (key: string, value: unknown) => void) => {
    // TODO: подписка на сообщения от C++
    console.log('[litware bridge] on registered, cb:', typeof cb)
  }
})
