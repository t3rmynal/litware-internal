import { app, BrowserWindow, ipcMain, screen } from 'electron'
import { join } from 'path'

let win: BrowserWindow | null = null
let menuOpen = false
let quitTimer: ReturnType<typeof setTimeout> | null = null
let hasConnectedOnce = false
let bridgeHasConnectedOnce = false

const isDev = !app.isPackaged

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
    show: false,
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  win.setAlwaysOnTop(true, 'screen-saver')
  // по умолчанию клики проходят насквозь в cs2
  win.setIgnoreMouseEvents(true, { forward: true })

  // показываем только когда cs2 стал активным (overlay_visible: true от dll)
  // до первого подключения — скрыты

  if (isDev) {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'))
  }

  win.on('closed', () => { win = null })
}

// переключаем меню — C++ шлёт menu_open через WebSocket, preload → menu-visibility
function toggleMenu(open: boolean) {
  if (!win) return
  menuOpen = open
  // сообщаем react про новое состояние
  win.webContents.send('toggle-menu', open)
  if (open) {
    win.setIgnoreMouseEvents(false)
    win.setAlwaysOnTop(true, 'screen-saver')
    win.moveTop()
    win.focus()
  } else {
    win.setIgnoreMouseEvents(true, { forward: true })
  }
}

app.whenReady().then(() => {
  createWindow()

  // INSERT обрабатывает только C++ (render_hook), шлёт menu_open через WebSocket
  // Electron получает через preload -> menu-visibility IPC
  // globalShortcut не используем — двойной toggle ломал закрытие

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

// cs2 закрыт — WebSocket разорван; если не переподключились за 5 сек — выходим
const BRIDGE_QUIT_DELAY_MS = 5000
ipcMain.on('bridge-disconnected', () => {
  if (quitTimer) clearTimeout(quitTimer)
  quitTimer = setTimeout(() => {
    quitTimer = null
    app.quit()
  }, BRIDGE_QUIT_DELAY_MS)
})
ipcMain.on('bridge-connected', () => {
  if (quitTimer) {
    clearTimeout(quitTimer)
    quitTimer = null
  }
})

// перемещение окна через драг
ipcMain.on('window-move', (_, { dx, dy }: { dx: number; dy: number }) => {
  if (!win) return
  const [cx, cy] = win.getPosition()
  win.setPosition(cx + dx, cy + dy)
})

ipcMain.on('window-close', () => {
  if (isDev) {
    toggleMenu(false)
  } else {
    app.quit()
  }
})

ipcMain.on('window-minimize', () => {
  win?.minimize()
})

// dll шлёт menu_open через websocket -> preload -> ipc
// (запасной путь если globalShortcut не сработал)
ipcMain.on('menu-visibility', (_, open: boolean) => {
  toggleMenu(open)
})

// уведомление — показываем overlay если был скрыт
ipcMain.on('toast-notify', () => {
  if (!win) return
  win.show()
  win.setAlwaysOnTop(true, 'screen-saver')
  win.moveTop()
})

// c++ шлёт overlay_visible когда cs2 теряет/получает фокус
ipcMain.on('overlay-visible', (_, visible: boolean) => {
  if (!win) return
  if (!visible) {
    // cs2 alt-tabbed — скрываем overlay
    if (menuOpen) toggleMenu(false)
    win.hide()
  } else {
    // cs2 снова в фокусе — показываем
    win.show()
    win.setAlwaysOnTop(true, 'screen-saver')
    win.moveTop()
  }
})
