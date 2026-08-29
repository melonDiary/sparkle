export interface LatestSender<T> {
  send(value: T): void
  clear(): void
}

/**
 * Emits the first value immediately, then at most one latest value per window.
 * Clearing cancels both the pending value and the scheduled timer.
 */
export function createLatestSender<T>(
  intervalMs: number,
  sink: (value: T) => void
): LatestSender<T> {
  let pending: T | undefined
  let timer: NodeJS.Timeout | null = null

  const flush = (): void => {
    timer = null
    if (pending === undefined) return
    const value = pending
    pending = undefined
    sink(value)
    timer = setTimeout(flush, intervalMs)
  }

  return {
    send(value: T): void {
      if (!timer) {
        sink(value)
        timer = setTimeout(flush, intervalMs)
        return
      }
      pending = value
    },
    clear(): void {
      pending = undefined
      if (timer) {
        clearTimeout(timer)
        timer = null
      }
    }
  }
}
