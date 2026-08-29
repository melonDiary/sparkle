import WebSocket from 'ws'

const wsReconnectDelay = 1000

function isWebSocketActive(ws: WebSocket | null): boolean {
  return ws?.readyState === WebSocket.OPEN || ws?.readyState === WebSocket.CONNECTING
}

function closeQuietly(ws: WebSocket): void {
  ws.removeAllListeners()
  ws.on('error', () => {})
  if (isWebSocketActive(ws)) {
    ws.close()
  }
}

export interface MihomoStream {
  /** Open the stream unless an active socket or pending reconnect already exists. */
  start: () => Promise<void>
  /** Stop the stream and reset the retry budget. */
  stop: () => void
  /** Stop then immediately start again. */
  restart: () => Promise<void>
}

export interface MihomoStreamOptions {
  /** Build the socket for one connection attempt (path/headers resolved fresh each attempt). */
  connect: () => Promise<WebSocket>
  /** Handle one decoded message. */
  onMessage: (data: string) => void
  /** Initial retry budget; each successful message restores it. */
  retries?: number
}

/**
 * One long-lived controller stream with bounded reconnects.
 *
 * Behavior shared by all mihomo streams: only one active socket or pending
 * reconnect at a time; a successful message restores the retry budget; on
 * close the socket retries up to the budget with a fixed 1s delay; errors
 * close the socket so the close handler drives the retry.
 */
export function createMihomoStream(options: MihomoStreamOptions): MihomoStream {
  const retryBudget = options.retries ?? 10
  let ws: WebSocket | null = null
  let retry = retryBudget
  let reconnectTimer: NodeJS.Timeout | null = null

  const clearReconnectTimer = (): void => {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  const connect = async (): Promise<void> => {
    const socket = await options.connect()
    ws = socket

    socket.onmessage = (e): void => {
      retry = retryBudget
      try {
        options.onMessage(e.data as string)
      } catch {
        // ignore
      }
    }

    socket.onclose = (): void => {
      if (ws === socket) {
        ws = null
      }
      if (ws !== null || !retry || reconnectTimer) return

      retry--
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null
        connect().catch(() => {})
      }, wsReconnectDelay)
    }

    socket.onerror = (): void => {
      socket.close()
    }
  }

  return {
    start: async (): Promise<void> => {
      if (isWebSocketActive(ws)) return
      clearReconnectTimer()
      await connect()
    },
    stop: (): void => {
      retry = retryBudget
      clearReconnectTimer()
      if (ws) {
        closeQuietly(ws)
        ws = null
      }
    },
    restart: async (): Promise<void> => {
      stop()
      await connect()
    }
  }
}
