import { app, BrowserWindow, ipcMain, screen } from 'electron'
import { join } from 'path'
import * as net from 'net'

let win: BrowserWindow | null = null
let menuOpen = false
let quitTimer: ReturnType<typeof setTimeout> | null = null
let tcpSocket: net.Socket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

const isDev = !app.isPackaged
const TCP_PORT = 37373
const RECONNECT_DELAY = 3000
const QUIT_DELAY = 8000

function createWindow() {
  const { bounds } = screen.getPrimaryDisplay()

  win = new BrowserWindow({
    x: bounds.x,
    y: bounds.y,
    width: bounds.width,
    height: bounds.height,
    frame: false,
    transparent: true,
    skipTaskbar: true,
    alwaysOnTop: true,
    resizable: false,
    focusable: true,
    show: true,
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  win.setAlwaysOnTop(true, 'screen-saver')
  win.setIgnoreMouseEvents(true, { forward: true })

  if (isDev) {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'))
  }

  win.on('closed', () => { win = null })
}

function toggleMenu(open: boolean) {
  if (!win) return
  menuOpen = open
  win.webContents.send('toggle-menu', open)
  if (open) {
    win.setIgnoreMouseEvents(false)
    win.setAlwaysOnTop(true, 'screen-saver')
    win.focus()
  } else {
    win.setIgnoreMouseEvents(true, { forward: true })
  }
}

function tcpSend(key: string, value: unknown) {
  if (tcpSocket?.writable) {
    tcpSocket.write(JSON.stringify({ key, value }) + '\n')
  }
}

function handleDllMessage(key: string, value: unknown) {
  if (!win) return
  if (key === 'menu_open') {
    toggleMenu(value === true || value === 'true')
  } else if (key === 'overlay_visible') {
    const visible = value === true || value === 'true'
    if (!visible) {
      if (menuOpen) toggleMenu(false)
      win.webContents.send('cs2-focused', false)
      win.hide()
    } else {
      win.show()
      win.webContents.send('cs2-focused', true)
    }
  } else if (key === 'notification') {
    // пробрасываем в renderer
    win.webContents.send('dll-notification', value)
    win.show()
  } else {
    // остальные ключи (настройки) → renderer
    win.webContents.send('dll-key', key, value)
  }
}

function tcpConnect() {
  if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null }

  const sock = new net.Socket()
  tcpSocket = sock

  let buf = ''

  sock.connect(TCP_PORT, '127.0.0.1', () => {
    console.log('[bridge] подключились к dll')
    if (quitTimer) { clearTimeout(quitTimer); quitTimer = null }
    // overlay_visible от dll покажет окно — здесь только снимаем таймер выхода
  })

  sock.on('data', (data) => {
    buf += data.toString()
    const lines = buf.split('\n')
    buf = lines.pop() ?? ''
    for (const line of lines) {
      const trimmed = line.trim()
      if (!trimmed) continue
      try {
        const { key, value } = JSON.parse(trimmed)
        handleDllMessage(key, value)
      } catch {
        console.warn('[bridge] parse error:', trimmed)
      }
    }
  })

  sock.on('close', () => {
    tcpSocket = null
    console.log('[bridge] соединение закрыто')
    if (!quitTimer) {
      quitTimer = setTimeout(() => { quitTimer = null; app.quit() }, QUIT_DELAY)
    }
    reconnectTimer = setTimeout(tcpConnect, RECONNECT_DELAY)
  })

  sock.on('error', (err) => {
    console.log('[bridge] ошибка:', err.message)
  })
}

app.whenReady().then(() => {
  createWindow()

  // подключаемся к dll tcp серверу
  tcpConnect()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

// renderer хочет отправить что-то в dll
ipcMain.on('send-to-dll', (_, key: string, value: unknown) => {
  tcpSend(key, value)
})

// перемещение окна
ipcMain.on('window-move', (_, { dx, dy }: { dx: number; dy: number }) => {
  if (!win) return
  const [cx, cy] = win.getPosition()
  win.setPosition(cx + dx, cy + dy)
})

ipcMain.on('window-close', () => {
  if (isDev) toggleMenu(false)
  else app.quit()
})

ipcMain.on('window-minimize', () => {
  win?.minimize()
})

// renderer просит показать/скрыть меню (fallback)
ipcMain.on('menu-visibility', (_, open: boolean) => {
  toggleMenu(open)
})
