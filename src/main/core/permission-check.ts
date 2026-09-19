import { statSync } from 'fs'

const S_ISUID = 0o4000

/**
 * Windows has no setuid bit; the elevated-task model decides whether the core is
 * permitted, so the core is always reported as permitted here.
 */
export function checkCorePermissionPathSync(corePath: string): boolean {
  if (process.platform === 'win32') return true
  try {
    return (statSync(corePath).mode & S_ISUID) !== 0
  } catch {
    return false
  }
}
