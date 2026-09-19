import { WebSocket } from 'ws'
import { appendAppLog } from '../utils/log'

export type ServiceEventStreamState = 'connected' | 'disconnected'

export interface ServiceEventStreamOptions<T> {
  /** Label used in log lines, e.g. `core events`. */
  label: string
  connect: () => WebSocket
  scheduleFallback: (reason: unknown) => void
  /** Whether open/close should notify stream-state subscribers. */
  emitStreamState: boolean
  /** Whether a non-manual close should notify the fallback handler. */
  fallbackOnClose: boolean
  /** Parse a raw websocket frame into a typed event. */
  parse: (raw: string) => T
}

export interface ServiceEventStream<T> {
  subscribeEvent: (handler: (event: T) => void | Promise<void>) => () => void
  subscribeStreamState?: (
    handler: (state: ServiceEventStreamState) => void | Promise<void>
  ) => () => void
  start: () => Promise<void>
  stop: () => void
}

const RECONNECT_DELAY_MS = 1000
const CONNECT_WAIT_MS = 1500

/**
 * One auto-reconnecting WebSocket event stream over the service Unix socket /
 * named pipe. This is the shared engine behind the core-events and
 * sysproxy-events streams, which previously duplicated ~150 lines of
 * start/stop/reconnect/dispatch logic with only labels differing.
 */
export function createServiceEventStream<T>(
  options: ServiceEventStreamOptions<T>
): ServiceEventStream<T> {
  const eventHandlers = new Set<(event: T) => void | Promise<void>>()
  const streamStateHandlers = new Set<(state: ServiceEventStreamState) => void | Promise<void>>()

  let ws: WebSocket | null = null
  let manualClose = true
  let reconnectTimer: NodeJS.Timeout | null = null

  function clearReconnectTimer(): void {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  async function dispatchStreamState(state: ServiceEventStreamState): Promise<void> {
    for (const handler of streamStateHandlers) {
      await Promise.resolve(handler(state)).catch((error) => {
        appendAppLog(
          `[Service]: ${options.label} event stream state handler failed, ${error}\n`
        ).catch(() => {})
      })
    }
  }

  async function dispatchEvent(data: WebSocket.RawData): Promise<void> {
    const raw = Buffer.isBuffer(data) ? data.toString('utf8') : data.toString()
    const event = options.parse(raw)
    for (const handler of eventHandlers) {
      await Promise.resolve(handler(event)).catch((error) => {
        appendAppLog(`[Service]: ${options.label} event handler failed, ${error}\n`).catch(() => {})
      })
    }
  }

  function scheduleReconnect(): void {
    if (manualClose || reconnectTimer) return
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null
      start().catch((error) => {
        appendAppLog(`[Service]: reconnect ${options.label} ws failed, ${error}\n`).catch(() => {})
      })
    }, RECONNECT_DELAY_MS)
  }

  async function start(): Promise<void> {
    manualClose = false
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return
    }

    clearReconnectTimer()

    let socket: WebSocket
    try {
      socket = options.connect()
    } catch (error) {
      await appendAppLog(`[Service]: create ${options.label} ws failed, ${error}\n`)
      options.scheduleFallback(error)
      scheduleReconnect()
      return
    }

    ws = socket
    socket.on('open', () => {
      if (options.emitStreamState) {
        dispatchStreamState('connected').catch((error) => {
          appendAppLog(
            `[Service]: handle ${options.label} event stream state failed, ${error}\n`
          ).catch(() => {})
        })
      }
    })
    socket.on('message', (data) => {
      dispatchEvent(data).catch((error) => {
        appendAppLog(`[Service]: handle ${options.label} event failed, ${error}\n`).catch(() => {})
      })
    })
    socket.on('close', () => {
      if (ws === socket) {
        ws = null
      }
      if (options.emitStreamState) {
        dispatchStreamState('disconnected').catch((error) => {
          appendAppLog(
            `[Service]: handle ${options.label} event stream state failed, ${error}\n`
          ).catch(() => {})
        })
      }
      if (!manualClose) {
        if (options.fallbackOnClose) {
          options.scheduleFallback(new Error(`${options.label} websocket disconnected`))
        }
        scheduleReconnect()
      }
    })
    socket.on('error', (error) => {
      appendAppLog(`[Service]: ${options.label} ws error, ${error}\n`).catch(() => {})
      if (!manualClose) {
        options.scheduleFallback(error)
      }
    })

    await waitForSocket(socket)
  }

  function stop(): void {
    manualClose = true
    clearReconnectTimer()
    if (ws) {
      closeServiceWebSocket(ws)
      ws = null
    }
  }

  return {
    subscribeEvent: (handler) => {
      eventHandlers.add(handler)
      return () => {
        eventHandlers.delete(handler)
      }
    },
    ...(options.emitStreamState
      ? {
          subscribeStreamState: (handler) => {
            streamStateHandlers.add(handler)
            return () => {
              streamStateHandlers.delete(handler)
            }
          }
        }
      : {}),
    start,
    stop
  }
}

export function closeServiceWebSocket(ws: WebSocket): void {
  ws.removeAllListeners()
  ws.on('error', () => {})
  if (ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
    return
  }
  ws.close()
}

async function waitForSocket(ws: WebSocket): Promise<void> {
  await new Promise<void>((resolve) => {
    let settled = false
    const complete = (): void => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      ws.off('open', complete)
      ws.off('error', complete)
      resolve()
    }
    const timer = setTimeout(complete, CONNECT_WAIT_MS)
    ws.once('open', complete)
    ws.once('error', complete)
  })
}
