// ???????????
// ???????????
// ???????????

const WS_URL = 'ws://localhost:37373'
const RECONNECT_DELAY = 3000

type MessageCallback = (key: string, value: unknown) => void

let socket: WebSocket | null = null
let listeners: MessageCallback[] = []
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

export function bridgeConnect() {
  try {
    socket = new WebSocket(WS_URL)

    socket.onopen = () => {
      console.log('[bridge] подключились к C++ серверу')
    }

    socket.onmessage = (e) => {
      try {
        const { key, value } = JSON.parse(e.data)
        listeners.forEach(cb => cb(key, value))
      } catch {
        console.warn('[bridge] не удалось распарсить сообщение:', e.data)
      }
    }

    socket.onerror = () => {
      console.log('[bridge] C++ сервер не доступен, переподключение через', RECONNECT_DELAY, 'ms')
    }

    socket.onclose = () => {
      socket = null
      if (reconnectTimer) clearTimeout(reconnectTimer)
      reconnectTimer = setTimeout(bridgeConnect, RECONNECT_DELAY)
    }
  } catch {
    console.log('[bridge] нет WebSocket в этом контексте (main process)')
  }
}

export function bridgeSend(key: string, value: unknown) {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ key, value }))
  } else {
    // ???????????
    console.log('[bridge stub] send:', key, '=', value)
  }
}

export function bridgeOnMessage(cb: MessageCallback) {
  listeners.push(cb)
  return () => {
    listeners = listeners.filter(l => l !== cb)
  }
}
