import { useSyncExternalStore } from 'react'

const relativeTimeTickMs = 60000

let tick = 0
const listeners = new Set<() => void>()
let timer: number | null = null

function subscribe(listener: () => void): () => void {
  listeners.add(listener)

  if (timer === null) {
    timer = window.setInterval(() => {
      tick += 1
      listeners.forEach((notify) => notify())
    }, relativeTimeTickMs)
  }

  return () => {
    listeners.delete(listener)
    if (listeners.size === 0 && timer !== null) {
      window.clearInterval(timer)
      timer = null
    }
  }
}

function getSnapshot(): number {
  return tick
}

/**
 * One shared 60s ticker for every "x minutes ago" label. Each row used to own a
 * `setInterval`, so a long connection list ran one timer per mounted card.
 */
export function useRelativeTimeTick(): number {
  return useSyncExternalStore(subscribe, getSnapshot, getSnapshot)
}
