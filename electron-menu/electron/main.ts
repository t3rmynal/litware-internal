import { app, BrowserWindow, ipcMain, screen, globalShortcut } from 'electron'
import { join } from 'path'

let win: BrowserWindow | null = null
let menuOpen = false

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
    show: true,
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  win.setAlwaysOnTop(true, 'screen-saver')
  // по умолчанию клики проходят насквозь в cs2
  win.setIgnoreMouseEvents(true, { forward: true })

  if (isDev) {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'))
  }

  win.on('closed', () => { win = null })
}

// переключаем меню — вызывается и из globalShortcut и из WS
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

  // insert перехватываем здесь — не зависит от ws
  globalShortcut.register('Insert', () => {
    toggleMenu(!menuOpen)
    // сообщаем c++ через ws (если подключён)
    win?.webContents.send('send-to-dll', 'menu_open', menuOpen ? 'true' : 'false')
  })

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  globalShortcut.unregisterAll()
  if (process.platform !== 'darwin') app.quit()
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

// уведомление — показываем overlay без фокуса
ipcMain.on('toast-notify', () => {
  if (!win) return
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
