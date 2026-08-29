import { describe, it, expect } from 'vitest'
import { HTTP_TIMEOUT, DOWNLOAD_TIMEOUT, normalizeBaseUrl, localhostProxy } from '../../src/main/utils/http'
import { describeHttpError } from '../../src/main/utils/http'

describe('http helpers', () => {
  it('exposes timeout constants', () => {
    expect(HTTP_TIMEOUT).toBe(30000)
    expect(DOWNLOAD_TIMEOUT).toBe(120000)
  })

  it('normalizes trailing slashes', () => {
    expect(normalizeBaseUrl('http://127.0.0.1:1234')).toBe('http://127.0.0.1:1234')
    expect(normalizeBaseUrl('http://127.0.0.1:1234/')).toBe('http://127.0.0.1:1234')
    expect(normalizeBaseUrl('http://127.0.0.1:1234//')).toBe('http://127.0.0.1:1234')
  })

  it('returns proxy config only for valid ports', () => {
    expect(localhostProxy(undefined as unknown as number)).toBeUndefined()
    expect(localhostProxy(0)).toBeUndefined()
    expect(localhostProxy(7890)).toEqual({ protocol: 'http', host: '127.0.0.1', port: 7890 })
  })
})

describe('describeHttpError', () => {
  it('describes non-errors', () => {
    expect(describeHttpError(null)).toBe('')
    expect(describeHttpError('boom')).toBe('boom')
  })

  it('describes error instances', () => {
    expect(describeHttpError(new Error('连接被拒绝'))).toBe('连接被拒绝')
  })

  it('describes axios errors with status codes', () => {
    const error = new Error('Request failed with status code 502') as Error & {
      isAxiosError: boolean
      response: { status: number }
      code?: string
    }
    error.name = 'AxiosError'
    error.isAxiosError = true
    error.response = { status: 502 }
    expect(describeHttpError(error)).toContain('HTTP 502')
  })
})
