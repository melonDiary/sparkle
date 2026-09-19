import { invalidateDirCache, mihomoCorePath } from '../utils/dirs'
import { execFileAsync } from '../utils/exec'
import { checkCorePermissionPathSync } from './permission-check'
import { createElevateTask } from '../sys/misc'
import { UserCancelledError, isUserCancelledError } from '../../shared/utils/user-cancelled'

type CoreName = 'mihomo' | 'mihomo-alpha'

export async function manualGrantCorePermition(cores?: CoreName[]): Promise<void> {
  if (process.platform === 'win32') {
    try {
      await createElevateTask()
    } catch (error) {
      if (isUserCancelledError(error)) {
        throw new UserCancelledError()
      }
      throw error
    }
    return
  }

  const grantPermission = async (coreName: CoreName): Promise<void> => {
    const corePath = mihomoCorePath(coreName)
    try {
      if (process.platform === 'darwin') {
        const escapedPath = corePath.replace(/"/g, '\\"')
        const shell = `chown root:admin \\"${escapedPath}\\" && chmod +sx \\"${escapedPath}\\"`
        const command = `do shell script "${shell}" with administrator privileges`
        await execFileAsync('osascript', ['-e', command])
      }
      if (process.platform === 'linux') {
        await execFileAsync('pkexec', [
          'bash',
          '-c',
          `chown root:root "${corePath}" && chmod +sx "${corePath}"`
        ])
      }
    } catch (error) {
      if (isUserCancelledError(error)) {
        throw new UserCancelledError()
      }
      throw error
    }
  }

  const targetCores = cores || ['mihomo', 'mihomo-alpha']
  await Promise.all(targetCores.map((core) => grantPermission(core)))
  // The setuid bit changes which controller socket the app targets.
  invalidateDirCache()
}

export async function checkCorePermission(): Promise<{ mihomo: boolean; 'mihomo-alpha': boolean }> {
  // Resolve the setuid bit with the same stat-based probe the controller socket
  // path already uses, instead of shelling out to `ls` and parsing its permission
  // string (which also reported a setgid-only file as permitted).
  const checkPermission = (coreName: CoreName): boolean => {
    try {
      return checkCorePermissionPathSync(mihomoCorePath(coreName))
    } catch {
      // An unresolvable path (for example `system` without a configured path)
      // counts as "not permitted" rather than an error.
      return false
    }
  }

  return {
    mihomo: checkPermission('mihomo'),
    'mihomo-alpha': checkPermission('mihomo-alpha')
  }
}

export async function revokeCorePermission(cores?: CoreName[]): Promise<void> {
  const revokePermission = async (coreName: CoreName): Promise<void> => {
    const corePath = mihomoCorePath(coreName)
    try {
      if (process.platform === 'darwin') {
        const escapedPath = corePath.replace(/"/g, '\\"')
        const shell = `chmod a-s \\"${escapedPath}\\"`
        const command = `do shell script "${shell}" with administrator privileges`
        await execFileAsync('osascript', ['-e', command])
      }
      if (process.platform === 'linux') {
        await execFileAsync('pkexec', ['bash', '-c', `chmod a-s "${corePath}"`])
      }
    } catch (error) {
      if (isUserCancelledError(error)) {
        throw new UserCancelledError()
      }
      throw error
    }
  }

  const targetCores = cores || ['mihomo', 'mihomo-alpha']
  await Promise.all(targetCores.map((core) => revokePermission(core)))
  // The setuid bit changes which controller socket the app targets.
  invalidateDirCache()
}
