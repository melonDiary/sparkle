import { beforeEach, describe, expect, it, vi } from 'vitest'
import { createMihomoStream } from './mihomo-stream'

interface FakeSocket {
  readyState: number
  onmessage?: (event: { data: string }) => void
  onclose?: () => void
  onerror?: () => void
  close: ReturnType<typeof vi.fn>
  removeAllListeners: ReturnType<typeof vi.fn>
  on: ReturnType<typeof vi.fn>
}

function createFakeSocket(readyState = 1): FakeSocket {
  return {
    readyState,
    close: vi.fn(),
    removeAllListeners: vi.fn(),
    on: vi.fn()
  }
}

describe('createMihomoStream', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })

  it('opens only one active socket and forwards messages', async () => {
    const socket = createFakeSocket()
    const connect = vi.fn(async () => socket as never)
    const onMessage = vi.fn()
    const stream = createMihomoStream({ connect, onMessage })

    await stream.start()
    await stream.start()
    socket.onmessage?.({ data: 'payload' })

    expect(connect).toHaveBeenCalledTimes(1)
    expect(onMessage).toHaveBeenCalledWith('payload')
  })

  it('reconnects after close and stops pending retries', async () => {
    const first = createFakeSocket()
    const second = createFakeSocket()
    const connect = vi
      .fn<() => Promise<never>>()
      .mockResolvedValueOnce(first as never)
      .mockResolvedValueOnce(second as never)
    const stream = createMihomoStream({ connect, onMessage: vi.fn(), retries: 1 })

    await stream.start()
    first.onclose?.()
    await vi.advanceTimersByTimeAsync(1000)
    expect(connect).toHaveBeenCalledTimes(2)

    second.onclose?.()
    stream.stop()
    await vi.advanceTimersByTimeAsync(1000)
    expect(connect).toHaveBeenCalledTimes(2)
  })

  it('closes the active socket on stop', async () => {
    const socket = createFakeSocket()
    const stream = createMihomoStream({
      connect: async () => socket as never,
      onMessage: vi.fn()
    })

    await stream.start()
    stream.stop()

    expect(socket.removeAllListeners).toHaveBeenCalledOnce()
    expect(socket.close).toHaveBeenCalledOnce()
  })
})
