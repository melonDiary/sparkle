import { describe, expect, it } from 'vitest'
import { isHttpUrl } from './url'

describe('isHttpUrl', () => {
  it('accepts HTTP and HTTPS URLs', () => {
    expect(isHttpUrl('http://example.com')).toBe(true)
    expect(isHttpUrl('https://example.com/path')).toBe(true)
  })

  it('rejects unsafe or invalid values', () => {
    expect(isHttpUrl('ftp://example.com')).toBe(false)
    expect(isHttpUrl('javascript:alert(1)')).toBe(false)
    expect(isHttpUrl('not-a-url')).toBe(false)
    expect(isHttpUrl(null)).toBe(false)
  })
})
