import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import path from 'path'

const { appGetPath, existsSync, getAppConfigSync, checkCorePermissionPathSync } = vi.hoisted(
  () => ({
    appGetPath: vi.fn(),
    existsSync: vi.fn(),
    getAppConfigSync: vi.fn(),
    checkCorePermissionPathSync: vi.fn()
  })
)

vi.mock('electron', () => ({
  app: {
    getPath: appGetPath,
    getAppPath: () => '/opt/sparkle/app'
  }
}))

vi.mock('@electron-toolkit/utils', () => ({ is: { dev: false } }))

vi.mock('fs', () => ({
  existsSync,
  mkdirSync: vi.fn(),
  readdirSync: vi.fn(() => [])
}))

vi.mock('../config/app', () => ({ getAppConfigSync }))

vi.mock('../core/permission-check', () => ({ checkCorePermissionPathSync }))

import { dataDir, invalidateDirCache, isPortable, mihomoIpcPath } from './dirs'

const originalPlatform = process.platform

function setPlatform(platform: NodeJS.Platform): void {
  Object.defineProperty(process, 'platform', { value: platform, configurable: true })
}

const exePath = '/opt/sparkle/sparkle'
const userDataDir = '/home/test/.config/sparkle'

describe('dirs caching', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    invalidateDirCache()
    setPlatform('linux')

    appGetPath.mockImplementation((name: string) => {
      if (name === 'home') return '/home/test'
      if (name === 'exe') return exePath
      if (name === 'userData') return userDataDir
      return `/resolved/${name}`
    })
    getAppConfigSync.mockReturnValue({ core: 'mihomo' })
    checkCorePermissionPathSync.mockReturnValue(true)
    existsSync.mockReturnValue(false)
  })

  afterEach(() => {
    setPlatform(originalPlatform)
  })

  it('probes the portable marker only once across dataDir calls', () => {
    expect(dataDir()).toBe(userDataDir)
    dataDir()
    dataDir()
    expect(existsSync).toHaveBeenCalledTimes(1)

    invalidateDirCache()
    dataDir()
    expect(existsSync).toHaveBeenCalledTimes(2)
  })

  it('resolves the portable data directory when the marker exists', () => {
    existsSync.mockReturnValue(true)

    expect(isPortable()).toBe(true)
    expect(dataDir()).toBe(path.join(path.dirname(exePath), 'data'))
  })

  it('memoizes the setuid probe behind the controller socket path', () => {
    expect(mihomoIpcPath()).toBe('/tmp/sparkle-mihomo-api.sock')
    expect(mihomoIpcPath()).toBe('/tmp/sparkle-mihomo-api.sock')
    expect(checkCorePermissionPathSync).toHaveBeenCalledTimes(1)

    invalidateDirCache()
    checkCorePermissionPathSync.mockReturnValue(false)

    expect(mihomoIpcPath()).toBe('/tmp/sparkle-mihomo-api-noperm.sock')
    expect(checkCorePermissionPathSync).toHaveBeenCalledTimes(2)
  })

  it('uses the external socket for the system core without a permission probe', () => {
    getAppConfigSync.mockReturnValue({ core: 'system' })

    expect(mihomoIpcPath()).toBe('/tmp/sparkle-mihomo-external.sock')
    expect(checkCorePermissionPathSync).not.toHaveBeenCalled()
  })

  it('returns the windows named pipe without touching the filesystem', () => {
    setPlatform('win32')

    expect(mihomoIpcPath()).toBe('\\\\.\\pipe\\Sparkle\\mihomo')
    expect(checkCorePermissionPathSync).not.toHaveBeenCalled()
    expect(existsSync).not.toHaveBeenCalled()
  })
})
