import { readFile, writeFile } from 'fs/promises'
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
 */
export class CachedYamlStore<T> {
  private value: T | undefined

  constructor(private readonly options: CachedYamlStoreOptions<T>) {}

  async get(force = false): Promise<T> {
    if (force || this.value === undefined) {
      this.value = await this.load()
    }
    return this.value
  }

  async set(value: T): Promise<void> {
    this.value = value
    await writeFile(this.options.path, stringifyYaml(value), 'utf-8')
  }

  clear(): void {
    this.value = undefined
  }

  private async load(): Promise<T> {
    try {
      const data = await readFile(this.options.path, 'utf-8')
      return this.normalize(parseYaml<T>(data))
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'ENOENT') {
        throw error
      }

      if (this.options.initializeOnMissing === false) {
        throw error
      }

      const value = this.options.createDefault()
      await writeFile(this.options.path, stringifyYaml(value), 'utf-8')
      return value
    }
  }

  private normalize(value: unknown): T {
    return this.options.normalize ? this.options.normalize(value) : (value as T)
  }
}
