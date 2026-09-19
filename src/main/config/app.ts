import { readFile, writeFile, rename, copyFile, unlink } from 'fs/promises'
import { appConfigPath } from '../utils/dirs'
import { parseYaml, stringifyYaml } from '../utils/yaml'
import { deepMerge } from '../utils/merge'
import { defaultConfig } from '../utils/template'
import { readFileSync, existsSync } from 'fs'

let appConfig: AppConfig
// Mirrors the resolved config so `getAppConfigSync` does not re-read and re-parse
// config.yaml on every call. Kept in step with the async cache below.
let appConfigSyncCache: AppConfig | undefined
// Bumped whenever the cached config object changes, so hot paths can memoize
// values derived from the config without polling or deep comparisons.
let appConfigRevision = 0
let writePromise: Promise<void> = Promise.resolve()

function isValidConfig(config: unknown): config is AppConfig {
  if (!config || typeof config !== 'object') return false
  const cfg = config as Partial<AppConfig>
  return 'sysProxy' in cfg && typeof cfg.sysProxy === 'object' && cfg.sysProxy !== null
}

async function safeWriteConfig(content: string): Promise<void> {
  const configPath = appConfigPath()
  const tmpPath = `${configPath}.tmp`
  const backupPath = `${configPath}.backup`

  try {
    await writeFile(tmpPath, content, 'utf-8')
    if (existsSync(configPath)) {
      await copyFile(configPath, backupPath)
      if (process.platform === 'win32') {
        await unlink(configPath)
      }
    }
    if (existsSync(tmpPath)) {
      await rename(tmpPath, configPath)
    }
  } catch (e) {
    if (existsSync(tmpPath)) {
      try {
        await unlink(tmpPath)
      } catch {
        // ignore
      }
    }
    throw e
  }
}

export async function getAppConfig(force = false): Promise<AppConfig> {
  if (force || !appConfig) {
    try {
      const data = await readFile(appConfigPath(), 'utf-8')
      const parsed = parseYaml<AppConfig>(data)
      if (!parsed || !isValidConfig(parsed)) {
        const backup = await readFile(`${appConfigPath()}.backup`, 'utf-8')
        appConfig = parseYaml<AppConfig>(backup)
      } else {
        appConfig = parsed
      }
    } catch (e) {
      appConfig = defaultConfig
    }
    appConfigRevision++
  }
  if (typeof appConfig !== 'object') {
    appConfig = defaultConfig
    appConfigRevision++
  }
  appConfigSyncCache = appConfig
  return appConfig
}

/**
 * Monotonic revision of the cached app config. Consumers that memoize values
 * derived from the config can compare it instead of re-reading the file.
 */
export function getAppConfigRevision(): number {
  return appConfigRevision
}

export async function patchAppConfig(patch: Partial<AppConfig>): Promise<AppConfig> {
  const previousPromise = writePromise
  const currentPromise = (async () => {
    await previousPromise
    appConfig = deepMerge(appConfig, patch)
    appConfigRevision++
    await safeWriteConfig(stringifyYaml(appConfig))
    appConfigSyncCache = appConfig
  })()
  writePromise = currentPromise.catch(() => {})
  await currentPromise
  return appConfig
}

export function getAppConfigSync(): AppConfig {
  if (appConfigSyncCache) return appConfigSyncCache

  try {
    const raw = readFileSync(appConfigPath(), 'utf-8')
    const data = parseYaml<AppConfig>(raw)
    appConfigSyncCache = typeof data === 'object' && data !== null ? data : defaultConfig
  } catch (e) {
    appConfigSyncCache = defaultConfig
  }
  return appConfigSyncCache
}
