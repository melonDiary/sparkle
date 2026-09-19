import { ipcMain, type IpcMainEvent } from 'electron'

const DEFAULT_CONFIRM_TIMEOUT_MS = 5 * 60 * 1000

// One cancel function per channel for the currently pending confirmation.
const pendingConfirmations = new Map<string, () => void>()

/**
 * Waits for a one-shot boolean confirmation from the renderer over `channel`.
 *
 * The renderer replies with `ipcRenderer.send(channel, confirmed)` and no
 * request id, so only one confirmation per channel may be awaited at a time;
 * a new call supersedes the previous one (which resolves false). If the user
 * never answers — window closed, renderer crashed — the timeout resolves false
 * and removes the listener instead of leaking a forever-pending promise.
 */
export function waitForIpcConfirmation(
  channel: string,
  timeoutMs = DEFAULT_CONFIRM_TIMEOUT_MS
): Promise<boolean> {
  pendingConfirmations.get(channel)?.()

  return new Promise((resolve) => {
    let settled = false
    let timeout: NodeJS.Timeout | null = null

    function finish(confirmed: boolean): void {
      if (settled) return
      settled = true
      if (timeout) clearTimeout(timeout)
      ipcMain.off(channel, handleConfirm)
      if (pendingConfirmations.get(channel) === cancel) {
        pendingConfirmations.delete(channel)
      }
      resolve(confirmed)
    }

    function handleConfirm(_event: IpcMainEvent, confirmed: boolean): void {
      finish(Boolean(confirmed))
    }

    function cancel(): void {
      finish(false)
    }

    ipcMain.on(channel, handleConfirm)
    pendingConfirmations.set(channel, cancel)
    timeout = setTimeout(() => finish(false), timeoutMs)
  })
}
