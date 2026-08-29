import { beforeEach, describe, expect, it, vi } from 'vitest'

const { handle, removeHandler } = vi.hoisted(() => ({
  handle: vi.fn(),
  removeHandler: vi.fn()
}))

vi.mock('electron', () => ({
  ipcMain: { handle, removeHandler }
}))

import { registerIpcHandler, unregisterIpcHandlers } from './ipc-registration'

describe('ipc registration', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    unregisterIpcHandlers()
    vi.clearAllMocks()
  })

  it('registers a handler once', () => {
    const handler = vi.fn()
    registerIpcHandler('example', handler)

    expect(handle).toHaveBeenCalledWith('example', handler)
    expect(removeHandler).not.toHaveBeenCalled()
  })

  it('removes an existing handler before replacing it', () => {
    const first = vi.fn()
    const second = vi.fn()
    registerIpcHandler('example', first)
    registerIpcHandler('example', second)

    expect(removeHandler).toHaveBeenCalledWith('example')
    expect(handle).toHaveBeenLastCalledWith('example', second)
  })

  it('unregisters all tracked handlers', () => {
    registerIpcHandler('one', vi.fn())
    registerIpcHandler('two', vi.fn())

    unregisterIpcHandlers()

    expect(removeHandler).toHaveBeenCalledWith('one')
    expect(removeHandler).toHaveBeenCalledWith('two')
  })
})
