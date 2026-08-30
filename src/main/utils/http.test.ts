import { describe, expect, it } from 'vitest'
import axios from 'axios'
import { describeHttpError, DOWNLOAD_TIMEOUT, HTTP_TIMEOUT, localhostProxy, normalizeBaseUrl } from './http'

describe('http helpers', () => {
  it('exposes stable timeout constants', () => {
    expect(HTTP_TIMEOUT).toBe(30000)
    expect(DOWNLOAD_TIMEOUT).toBe(120000)
  })

  it('normalizes base URLs', () => {
    expect(normalizeBaseUrl('http://localhost///')).toBe('http://localhost')
  })

  it('creates localhost proxy config only for non-zero ports', () => {
    expect(localhostProxy(0)).toBeUndefined()
    expect(localhostProxy(7890)).toEqual({ protocol: 'http', host: '127.0.0.1', port: 7890 })
  })

  it('formats axios errors with status and code', () => {
    const error = new axios.AxiosError('failed', 'ECONNABORTED', undefined, undefined, {
      status: 504,
      statusText: 'Gateway Timeout',
      headers: {},
      data: undefined,
      config: { headers: {} as never } as never
    })
    expect(describeHttpError(error)).toContain('HTTP 504')
    expect(describeHttpError(error)).toContain('ECONNABORTED')
  })
})
