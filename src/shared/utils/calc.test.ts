import { describe, expect, it } from 'vitest'
import { calcTraffic } from './calc'

describe('calcTraffic', () => {
  it('formats byte values', () => {
    expect(calcTraffic(0)).toBe('0.00 B')
    expect(calcTraffic(1024)).toContain('KB')
    expect(calcTraffic(1024 * 1024)).toContain('MB')
  })

  it('keeps the value readable as it grows', () => {
    expect(calcTraffic(999)).toBe('999.0 B')
    expect(calcTraffic(1536)).toBe('1.50 KB')
    expect(calcTraffic(1024 * 1024 * 1024)).toBe('1.00 GB')
  })
})
