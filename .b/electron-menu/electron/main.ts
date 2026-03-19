import { app, BrowserWindow, ipcMain } from 'electron'
import { join } from 'path'
import { exec } from 'child_process'

let win: BrowserWindow | null = null
let cs2WatcherInterval: NodeJS.Timeout | null = null

// в dev режиме всегда показываем окно без проверки cs2
const isDev = !app.isPackaged

function createWindow() {
  win = new BrowserWindow({
    width: 880,
    height: 580,
    frame: false,
    transparent: true,
    skipTaskbar: true,
    alwaysOnTop: true,
    resizable: false,
    show: isDev,
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  win.center()

  if (isDev) {
    win.loadURL('http://localhost:5173')
    // win.webContents.openDevTools({ mode: 'detach' })
  } else {
    win.loadFile(join(__dirname, '../renderer/index.html'))
  }

  win.on('closed', () => {
    win = null
  })
}

// проверяем запущен ли cs2.exe
function checkCs2Running(cb: (running: boolean) => void) {
  exec('tasklist /FI "IMAGENAME eq cs2.exe" /NH', (_, stdout) => {
    cb(stdout.toLowerCase().includes('cs2.exe'))
  })
}

// запускаем вотчер — показываем окно только когда cs2 запущен
function startCs2Watcher() {
  if (isDev) return

  cs2WatcherInterval = setInterval(() => {
    if (!win) return
    checkCs2Running((running) => {
      if (!win) return
      if (running && !win.isVisible()) {
        win.show()
        win.focus()
      } else if (!running && win.isVisible()) {
        win.hide()
      }
    })
  }, 2000)
}

app.whenReady().then(() => {
  createWindow()
  startCs2Watcher()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (cs2WatcherInterval) clearInterval(cs2WatcherInterval)
  if (process.platform !== 'darwin') app.quit()
})

// IPC: перемещение окна (drag без рамки)
ipcMain.on('window-move', (_, { x, y }: { x: number; y: number }) => {
  win?.setPosition(x, y)
})

// IPC: закрыть / свернуть
ipcMain.on('window-close', () => {
  if (isDev) win?.hide()
  else app.quit()
})

ipcMain.on('window-minimize', () => {
  win?.minimize()
})
