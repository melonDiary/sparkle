import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createLatestSender } from './latest-sender'

describe('createLatestSender', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('emits the first value immediately and the latest value at the end of a window', () => {
    const sink = vi.fn()
    const sender = createLatestSender(100, sink)

    sender.send(1)
    sender.send(2)
    sender.send(3)

    expect(sink).toHaveBeenCalledTimes(1)
    expect(sink).toHaveBeenLastCalledWith(1)

    vi.advanceTimersByTime(100)
    expect(sink).toHaveBeenCalledTimes(2)
    expect(sink).toHaveBeenLastCalledWith(3)
  })

  it('does not schedule work after clear', () => {
    const sink = vi.fn()
    const sender = createLatestSender(100, sink)

    sender.send(1)
    sender.send(2)
    sender.clear()
    vi.advanceTimersByTime(1000)

    expect(sink).toHaveBeenCalledTimes(1)
    expect(sink).toHaveBeenLastCalledWith(1)
  })
})
