import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const { appendAppLog, showNotification } = vi.hoisted(() => ({
  appendAppLog: vi.fn(() => Promise.resolve()),
  showNotification: vi.fn()
}))

vi.mock('../utils/log', () => ({ appendAppLog }))
vi.mock('../utils/notification', () => ({ showNotification }))

import { registerCoreCrash, registerCoreStartSuccess } from './crash-loop-guard'

describe('crash-loop-guard', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    registerCoreStartSuccess()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('allows restarts below the crash limit', () => {
    expect(registerCoreCrash()).toBe(true)
    expect(registerCoreCrash()).toBe(true)
    expect(showNotification).not.toHaveBeenCalled()
  })

  it('trips the breaker after repeated crashes and stops restarting', () => {
    const results = Array.from({ length: 6 }, () => registerCoreCrash())

    expect(results.slice(0, 4)).toEqual([true, true, true, true])
    expect(results[4]).toBe(false)
    expect(results[5]).toBe(false)
    expect(showNotification).toHaveBeenCalledTimes(1)
    expect(appendAppLog).toHaveBeenCalledWith(expect.stringContaining('auto-restart disabled'))
  })

  it('keeps restarts allowed when crashes are spread out in time', () => {
    vi.useFakeTimers()

    const t0 = Date.now()
    for (let i = 0; i < 10; i++) {
      vi.setSystemTime(t0 + i * 30 * 1000)
      expect(registerCoreCrash()).toBe(true)
    }
  })

  it('re-arms after a successful startup', () => {
    for (let i = 0; i < 4; i++) registerCoreCrash()

    registerCoreStartSuccess()

    expect(registerCoreCrash()).toBe(true)
  })

  it('shows the notification only once per breaker trip', () => {
    for (let i = 0; i < 8; i++) registerCoreCrash()

    expect(showNotification).toHaveBeenCalledTimes(1)
  })
})
