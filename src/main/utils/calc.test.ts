import { describe, expect, it } from 'vitest'
import { calcTraffic } from './calc'

describe('calcTraffic', () => {
  it('formats byte values', () => {
    expect(calcTraffic(0)).toBe('0 B')
    expect(calcTraffic(1024)).toContain('KB')
    expect(calcTraffic(1024 * 1024)).toContain('MB')
  })
})
