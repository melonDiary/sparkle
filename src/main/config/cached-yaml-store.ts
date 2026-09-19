import { copyFile, readFile, rename, rm, writeFile } from 'fs/promises'
import { existsSync } from 'fs'
import { parseYaml, stringifyYaml } from '../utils/yaml'

export interface CachedYamlStoreOptions<T> {
  path: string
  createDefault: () => T
  normalize?: (value: unknown) => T
  initializeOnMissing?: boolean
}

/**
 * Shared persistence primitive for YAML-backed configuration.
 * Domain modules retain ownership of validation and side effects while this
 * class centralizes lazy loading, cache invalidation, serialization, and IO.
 *
 * Writes go through a temporary file plus a rename so a crash mid-write cannot
 * truncate the live config, and they are chained so concurrent `set` calls of the
 * same store cannot interleave on disk.
 */
export class CachedYamlStore<T> {
  private value: T | undefined
  private writePromise: Promise<void> = Promise.resolve()

  constructor(private readonly options: CachedYamlStoreOptions<T>) {}

  async get(force = false): Promise<T> {
    if (force || this.value === undefined) {
      this.value = await this.load()
    }
    return this.value
  }

  async set(value: T): Promise<void> {
    this.value = value
    const content = stringifyYaml(value)
    const nextWrite = this.writePromise.then(() => this.writeContent(content))
    // Keep the chain alive even when a write fails; the caller still sees the error.
    this.writePromise = nextWrite.catch(() => {})
    await nextWrite
  }

  clear(): void {
    this.value = undefined
  }

  private get backupPath(): string {
    return `${this.options.path}.backup`
  }

  private async writeContent(content: string): Promise<void> {
    const { path } = this.options
    const tmpPath = `${path}.tmp`

    try {
      await writeFile(tmpPath, content, 'utf-8')
      if (existsSync(path)) {
        await copyFile(path, this.backupPath)
      }
      await rename(tmpPath, path)
    } catch (error) {
      await rm(tmpPath, { force: true }).catch(() => {})
      throw error
    }
  }

  private async load(): Promise<T> {
    try {
      const data = await readFile(this.options.path, 'utf-8')
      return this.normalize(parseYaml<T>(data))
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'ENOENT') {
        return await this.loadBackup(error)
      }

      if (this.options.initializeOnMissing === false) {
        throw error
      }

      const value = this.options.createDefault()
      await this.writeContent(stringifyYaml(value))
      return value
    }
  }

  /**
   * A config file that exists but cannot be read or parsed is recovered from the
   * backup written by the previous successful save, mirroring `config/app.ts`.
   */
  private async loadBackup(originalError: unknown): Promise<T> {
    try {
      const data = await readFile(this.backupPath, 'utf-8')
      return this.normalize(parseYaml<T>(data))
    } catch {
      throw originalError
    }
  }

  private normalize(value: unknown): T {
    return this.options.normalize ? this.options.normalize(value) : (value as T)
  }
}
