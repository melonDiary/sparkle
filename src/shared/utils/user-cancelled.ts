/**
 * Thrown when the user cancels an elevation prompt (UAC / osascript / pkexec).
 *
 * Instances cross the IPC boundary as serialized errors, so the renderer cannot
 * use `instanceof`. Both sides therefore identify cancellation through
 * `error.name === 'UserCancelledError'` — keep the name stable.
 */
export class UserCancelledError extends Error {
  constructor(message = '用户取消操作') {
    super(message)
    this.name = 'UserCancelledError'
  }
}

/**
 * Detects a user cancellation from either a live error instance (main process)
 * or an IPC-serialized message (renderer), where only `name`/`message` survive.
 */
export function isUserCancelledError(error: unknown): boolean {
  if (error instanceof UserCancelledError) {
    return true
  }
  if (error && typeof error === 'object' && 'name' in error) {
    if ((error as { name?: unknown }).name === 'UserCancelledError') {
      return true
    }
  }
  const message = error instanceof Error ? error.message : String(error)
  return (
    message.includes('用户已取消') ||
    message.includes('User canceled') ||
    message.includes('(-128)') ||
    message.includes('user cancelled') ||
    message.includes('dismissed') ||
    message.includes('UserCancelledError')
  )
}
