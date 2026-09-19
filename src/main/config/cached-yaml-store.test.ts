import { beforeEach, describe, expect, it, vi } from 'vitest'

const { readFile, writeFile, copyFile, rename, rm, existsSync } = vi.hoisted(() => ({
  readFile: vi.fn(),
  writeFile: vi.fn(),
  copyFile: vi.fn(),
  rename: vi.fn(),
  rm: vi.fn(),
  existsSync: vi.fn()
}))

vi.mock('fs/promises', () => ({ readFile, writeFile, copyFile, rename, rm }))
vi.mock('fs', () => ({ existsSync }))

import { CachedYamlStore } from './cached-yaml-store'

interface Config {
  items: string[]
}

function enoent(): NodeJS.ErrnoException {
  return Object.assign(new Error('ENOENT: no such file'), { code: 'ENOENT' })
}

describe('CachedYamlStore', () => {
  const path = '/tmp/config.yaml'
  const tmpPath = `${path}.tmp`
  const backupPath = `${path}.backup`
  const createDefault = (): Config => ({ items: [] })

  beforeEach(() => {
    vi.clearAllMocks()
    existsSync.mockReturnValue(false)
    writeFile.mockResolvedValue(undefined)
    copyFile.mockResolvedValue(undefined)
    rename.mockResolvedValue(undefined)
    rm.mockResolvedValue(undefined)
  })

  it('loads lazily and caches the parsed value', async () => {
    readFile.mockResolvedValue('items:\n  - a\n')
    const store = new CachedYamlStore<Config>({ path, createDefault })

    const first = await store.get()
    const second = await store.get()

    expect(first).toEqual({ items: ['a'] })
    expect(second).toBe(first)
    expect(readFile).toHaveBeenCalledTimes(1)
  })

  it('re-reads the file when forced', async () => {
    readFile.mockResolvedValue('items: []\n')
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await store.get()
    await store.get(true)

    expect(readFile).toHaveBeenCalledTimes(2)
  })

  it('normalizes parsed values', async () => {
    readFile.mockResolvedValue('items:\n  - a\n')
    const normalize = vi.fn(() => ({ items: ['normalized'] }))
    const store = new CachedYamlStore<Config>({ path, createDefault, normalize })

    await expect(store.get()).resolves.toEqual({ items: ['normalized'] })
    expect(normalize).toHaveBeenCalled()
  })

  it('creates and persists a default atomically when the file is missing', async () => {
    readFile.mockRejectedValue(enoent())
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await expect(store.get()).resolves.toEqual({ items: [] })
    expect(writeFile).toHaveBeenCalledTimes(1)
    expect(writeFile).toHaveBeenCalledWith(tmpPath, expect.any(String), 'utf-8')
    expect(rename).toHaveBeenCalledWith(tmpPath, path)
  })

  it('preserves ENOENT when initializeOnMissing is false', async () => {
    readFile.mockRejectedValue(enoent())
    const store = new CachedYamlStore<Config>({
      path,
      createDefault,
      initializeOnMissing: false
    })

    await expect(store.get()).rejects.toMatchObject({ code: 'ENOENT' })
    expect(writeFile).not.toHaveBeenCalled()
  })

  it('propagates non-ENOENT read errors when no backup is readable', async () => {
    readFile.mockRejectedValue(new Error('EACCES'))
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await expect(store.get()).rejects.toThrow('EACCES')
  })

  it('recovers from the backup when the config file itself is unreadable', async () => {
    readFile.mockImplementation((file: string) =>
      file === backupPath
        ? Promise.resolve('items:\n  - from-backup\n')
        : Promise.reject(Object.assign(new Error('EACCES'), { code: 'EACCES' }))
    )
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await expect(store.get()).resolves.toEqual({ items: ['from-backup'] })
  })

  it('writes and caches on set', async () => {
    writeFile.mockResolvedValue(undefined)
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await store.set({ items: ['b'] })

    expect(writeFile).toHaveBeenCalledWith(tmpPath, expect.any(String), 'utf-8')
    expect(rename).toHaveBeenCalledWith(tmpPath, path)
    await expect(store.get()).resolves.toEqual({ items: ['b'] })
    expect(readFile).not.toHaveBeenCalled()
  })

  it('backs up the previous config before replacing it', async () => {
    existsSync.mockReturnValue(true)
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await store.set({ items: ['b'] })

    expect(copyFile).toHaveBeenCalledWith(path, backupPath)
    expect(rename).toHaveBeenCalledWith(tmpPath, path)
  })

  it('serializes concurrent writes to the same store', async () => {
    const pendingRenames: (() => void)[] = []
    rename.mockImplementation(
      () =>
        new Promise<void>((resolve) => {
          pendingRenames.push(resolve)
        })
    )

    const store = new CachedYamlStore<Config>({ path, createDefault })
    const first = store.set({ items: ['a'] })
    const second = store.set({ items: ['b'] })

    await vi.waitFor(() => expect(rename).toHaveBeenCalledTimes(1))
    expect(writeFile).toHaveBeenCalledTimes(1)

    pendingRenames[0]()
    await vi.waitFor(() => expect(rename).toHaveBeenCalledTimes(2))

    pendingRenames[1]()
    await expect(Promise.all([first, second])).resolves.toHaveLength(2)
  })

  it('clear invalidates the cache', async () => {
    readFile.mockResolvedValue('items: []\n')
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await store.get()
    store.clear()
    await store.get()

    expect(readFile).toHaveBeenCalledTimes(2)
  })
})
