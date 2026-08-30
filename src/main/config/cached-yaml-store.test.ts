import { beforeEach, describe, expect, it, vi } from 'vitest'

const { readFile, writeFile } = vi.hoisted(() => ({
  readFile: vi.fn(),
  writeFile: vi.fn()
}))

vi.mock('fs/promises', () => ({ readFile, writeFile }))

import { CachedYamlStore } from './cached-yaml-store'

interface Config {
  items: string[]
}

function enoent(): NodeJS.ErrnoException {
  return Object.assign(new Error('ENOENT: no such file'), { code: 'ENOENT' })
}

describe('CachedYamlStore', () => {
  const path = '/tmp/config.yaml'
  const createDefault = (): Config => ({ items: [] })

  beforeEach(() => {
    vi.clearAllMocks()
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

  it('creates and persists a default when the file is missing', async () => {
    readFile.mockRejectedValue(enoent())
    writeFile.mockResolvedValue(undefined)
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await expect(store.get()).resolves.toEqual({ items: [] })
    expect(writeFile).toHaveBeenCalledTimes(1)
    expect(writeFile).toHaveBeenCalledWith(path, expect.any(String), 'utf-8')
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

  it('propagates non-ENOENT read errors', async () => {
    readFile.mockRejectedValue(new Error('EACCES'))
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await expect(store.get()).rejects.toThrow('EACCES')
  })

  it('writes and caches on set', async () => {
    readFile.mockResolvedValue('items: []\n')
    writeFile.mockResolvedValue(undefined)
    const store = new CachedYamlStore<Config>({ path, createDefault })

    await store.set({ items: ['b'] })

    expect(writeFile).toHaveBeenCalledWith(path, expect.any(String), 'utf-8')
    await expect(store.get()).resolves.toEqual({ items: ['b'] })
    expect(readFile).not.toHaveBeenCalled()
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