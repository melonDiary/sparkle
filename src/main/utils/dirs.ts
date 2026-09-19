import { is } from '@electron-toolkit/utils'
import { existsSync, mkdirSync, readdirSync } from 'fs'
import { app } from 'electron'
import path from 'path'
import { getAppConfigSync } from '../config/app'
import { execFileAsync } from './exec'
import { checkCorePermissionPathSync } from '../core/permission-check'

export const homeDir = app.getPath('home')

// The portable layout and the data directory are fixed for the lifetime of the
// process. Path helpers are called on hot paths (every log write resolves its
// directory), so the probe result is cached instead of hitting the filesystem.
let portableCache: boolean | undefined
let dataDirCache: string | undefined
let corePermissionCache: { corePath: string; permitted: boolean } | undefined

export function isPortable(): boolean {
  if (portableCache === undefined) {
    portableCache = existsSync(path.join(exeDir(), 'PORTABLE'))
  }
  return portableCache
}

export function dataDir(): string {
  if (dataDirCache === undefined) {
    dataDirCache = isPortable() ? path.join(exeDir(), 'data') : app.getPath('userData')
  }
  return dataDirCache
}

/**
 * Invalidate cached path/permission decisions. Required after the core binary's
 * setuid bit changes; safe to call at any time.
 */
export function invalidateDirCache(): void {
  portableCache = undefined
  dataDirCache = undefined
  corePermissionCache = undefined
}

export function taskDir(): string {
  const dir = path.join(app.getPath('userData'), 'tasks')
  if (!existsSync(dir)) {
    mkdirSync(dir, { recursive: true })
  }
  return dir
}

export function subStoreDir(): string {
  return path.join(dataDir(), 'substore')
}

export function subStoreFrontendDir(): string {
  return path.join(subStoreDir(), 'sub-store-frontend')
}

export function subStoreBackendPath(): string {
  return path.join(subStoreDir(), 'sub-store.bundle.js')
}

export function subStoreTempDir(): string {
  return path.join(subStoreDir(), 'temp')
}

export function exeDir(): string {
  return path.dirname(exePath())
}

export function exePath(): string {
  return app.getPath('exe')
}

export function resourcesDir(): string {
  if (is.dev) {
    return path.join(__dirname, '../../extra')
  } else {
    if (app.getAppPath().endsWith('asar')) {
      return process.resourcesPath
    } else {
      return path.join(app.getAppPath(), 'resources')
    }
  }
}

export function resourcesFilesDir(): string {
  return path.join(resourcesDir(), 'files')
}

export function themesDir(): string {
  return path.join(dataDir(), 'themes')
}

function isCorePermissionGranted(corePath: string): boolean {
  if (corePermissionCache?.corePath !== corePath) {
    corePermissionCache = { corePath, permitted: checkCorePermissionPathSync(corePath) }
  }
  return corePermissionCache.permitted
}

export function mihomoIpcPath(): string {
  if (process.platform === 'win32') {
    return '\\\\.\\pipe\\Sparkle\\mihomo'
  }
  const { core = 'mihomo' } = getAppConfigSync()
  if (core === 'system') {
    return '/tmp/sparkle-mihomo-external.sock'
  }
  if (!isCorePermissionGranted(mihomoCorePath(core))) {
    return '/tmp/sparkle-mihomo-api-noperm.sock'
  }
  return '/tmp/sparkle-mihomo-api.sock'
}

export function serviceIpcPath(): string {
  if (process.platform === 'win32') {
    return '\\\\.\\pipe\\sparkle\\service'
  }
  return '/tmp/sparkle-service.sock'
}

export function mihomoCoreDir(): string {
  return path.join(resourcesDir(), 'sidecar')
}

export function mihomoCorePath(core: string): string {
  if (core === 'mihomo' || core === 'mihomo-alpha') {
    const isWin = process.platform === 'win32'
    return path.join(mihomoCoreDir(), `${core}${isWin ? '.exe' : ''}`)
  }
  if (core === 'system') {
    const sysPath = systemCorePath()
    if (!sysPath || !existsSync(sysPath)) {
      const errorMsg = sysPath ? `系统内核路径无效或不存在: ${sysPath}` : '系统内核路径未设置'
      throw new Error(errorMsg)
    }
    return sysPath
  }
  throw new Error('内核路径错误')
}

function systemCorePath(): string {
  const { systemCorePath = '' } = getAppConfigSync()
  return systemCorePath
}

export function servicePath(): string {
  const isWin = process.platform === 'win32'
  return path.join(resourcesFilesDir(), `sparkle-service${isWin ? '.exe' : ''}`)
}

export function serviceAuthStorePath(): string {
  return path.join(dataDir(), 'service-auth.json')
}

export function appConfigPath(): string {
  return path.join(dataDir(), 'config.yaml')
}

export function controledMihomoConfigPath(): string {
  return path.join(dataDir(), 'mihomo.yaml')
}

export function profileConfigPath(): string {
  return path.join(dataDir(), 'profile.yaml')
}

export function profilesDir(): string {
  return path.join(dataDir(), 'profiles')
}

export function profilePath(id: string): string {
  return path.join(profilesDir(), `${id}.yaml`)
}

export function overrideDir(): string {
  return path.join(dataDir(), 'override')
}

export function overrideConfigPath(): string {
  return path.join(dataDir(), 'override.yaml')
}

export function overridePath(id: string, ext: 'js' | 'yaml' | 'log'): string {
  return path.join(overrideDir(), `${id}.${ext}`)
}

export function mihomoWorkDir(): string {
  return path.join(dataDir(), 'work')
}

export function mihomoProfileWorkDir(id: string | undefined): string {
  return path.join(mihomoWorkDir(), id || 'default')
}

export function mihomoTestDir(): string {
  return path.join(dataDir(), 'test')
}

export function mihomoWorkConfigPath(id: string | undefined): string {
  if (id === 'work') {
    return path.join(mihomoWorkDir(), 'config.yaml')
  } else {
    return path.join(mihomoProfileWorkDir(id), 'config.yaml')
  }
}

export function logDir(): string {
  return path.join(dataDir(), 'logs')
}

function datedLogPath(prefix?: string): string {
  const date = new Date()
  const name = `${date.getFullYear()}-${date.getMonth() + 1}-${date.getDate()}`
  return path.join(logDir(), `${prefix ? `${prefix}-` : ''}${name}.log`)
}

export function logPath(): string {
  return datedLogPath()
}

export function appLogPath(): string {
  return datedLogPath('app')
}

export function coreLogPath(): string {
  return datedLogPath('core')
}

export function substoreLogPath(): string {
  return datedLogPath('sub-store')
}

async function execCommand(command: string, args: string[]): Promise<string | undefined> {
  try {
    const { stdout } = await execFileAsync(command, args, { encoding: 'utf8', windowsHide: true })
    const result = stdout.trim()
    return result || undefined
  } catch (error) {
    return undefined
  }
}

async function hasCommand(command: string): Promise<boolean> {
  const whichCmd = process.platform === 'win32' ? 'where' : 'which'
  return (await execCommand(whichCmd, [command])) !== undefined
}

/**
 * Discovery shells out to `where`/`which`/`brew` and package managers. Every probe
 * is awaited instead of running `execSync`, which used to block the main process
 * for the whole scan.
 */
export async function findSystemMihomo(): Promise<string[]> {
  const isWin = process.platform === 'win32'
  const isLinux = process.platform === 'linux'
  const isMac = process.platform === 'darwin'
  const foundPaths = new Set<string>()
  const searchNames = ['mihomo', 'clash']
  const addResultPaths = (output: string | undefined): void => {
    if (!output) return
    for (const line of output.split(/\r?\n/)) {
      const candidate = line.trim()
      if (candidate && existsSync(candidate)) {
        foundPaths.add(candidate)
      }
    }
  }

  for (const name of searchNames) {
    addResultPaths(await execCommand(isWin ? 'where' : 'which', [name]))
  }

  if (!isWin) {
    const commonDirs = [
      '/bin',
      '/usr/bin',
      '/usr/local/bin',
      '/opt/homebrew/bin',
      path.join(homeDir, '.local/bin'),
      path.join(homeDir, 'bin')
    ]

    for (const dir of commonDirs) {
      if (!existsSync(dir)) continue
      try {
        for (const file of readdirSync(dir)) {
          if (!file.startsWith('mihomo') && !file.startsWith('clash')) continue
          const binPath = path.join(dir, file)
          if (existsSync(binPath)) {
            foundPaths.add(binPath)
          }
        }
      } catch (error) {
        // ignore
      }
    }
  }

  if (isMac || isLinux) {
    // Homebrew
    if (await hasCommand('brew')) {
      for (const name of searchNames) {
        const prefix = await execCommand('brew', ['--prefix', name])
        if (!prefix) continue
        const binPath = path.join(prefix, 'bin', name)
        if (existsSync(binPath)) {
          foundPaths.add(binPath)
        }
      }
    }
  }

  if (isLinux) {
    // apt/dpkg (Debian/Ubuntu)
    if (await hasCommand('dpkg')) {
      for (const name of searchNames) {
        const result = await execCommand('dpkg', ['-L', name])
        if (!result) continue
        for (const line of result.split('\n')) {
          const candidate = line.trim()
          if (candidate.endsWith(`bin/${name}`) && existsSync(candidate)) {
            foundPaths.add(candidate)
          }
        }
      }
    }

    // rpm/yum (RedHat/CentOS/Fedora)
    if (await hasCommand('rpm')) {
      for (const name of searchNames) {
        const result = await execCommand('rpm', ['-ql', name])
        if (!result) continue
        for (const line of result.split('\n')) {
          const candidate = line.trim()
          if (candidate.endsWith(`bin/${name}`) && existsSync(candidate)) {
            foundPaths.add(candidate)
          }
        }
      }
    }

    // pacman (Arch Linux)
    if (await hasCommand('pacman')) {
      for (const name of searchNames) {
        const result = await execCommand('pacman', ['-Ql', name])
        if (!result) continue
        for (const line of result.split('\n')) {
          const candidate = line.split(' ')[1]
          if (candidate?.endsWith(`bin/${name}`) && existsSync(candidate)) {
            foundPaths.add(candidate)
          }
        }
      }
    }
  }

  if (isWin) {
    // Scoop
    if (await hasCommand('scoop')) {
      for (const name of searchNames) {
        const result = await execCommand('scoop', ['which', name])
        if (result && existsSync(result)) {
          foundPaths.add(result)
        }
      }
    }
  }

  return Array.from(foundPaths).sort()
}
