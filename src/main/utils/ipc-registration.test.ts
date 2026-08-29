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
    registerIpcHandler('getAppConfig', handler)

    expect(handle).toHaveBeenCalledWith('getAppConfig', handler)
    expect(removeHandler).not.toHaveBeenCalled()
  })

  it('removes an existing handler before replacing it', () => {
    const first = vi.fn()
    const second = vi.fn()
    registerIpcHandler('getAppConfig', first)
    registerIpcHandler('getAppConfig', second)

    expect(removeHandler).toHaveBeenCalledWith('getAppConfig')
    expect(handle).toHaveBeenLastCalledWith('getAppConfig', second)
  })

  it('unregisters all tracked handlers', () => {
    registerIpcHandler('getAppConfig', vi.fn())
    registerIpcHandler('getInterfaces', vi.fn())

    unregisterIpcHandlers()

    expect(removeHandler).toHaveBeenCalledWith('getAppConfig')
    expect(removeHandler).toHaveBeenCalledWith('getInterfaces')
  })
})
