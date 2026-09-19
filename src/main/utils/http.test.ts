import { describe, expect, it } from 'vitest'
import axios from 'axios'
import {
  describeHttpError,
  DOWNLOAD_TIMEOUT,
  HTTP_TIMEOUT,
  localhostProxy,
  normalizeBaseUrl,
  parseContentDispositionFilename
} from './http'

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

  it('parses plain and RFC 5987 content-disposition filenames', () => {
    expect(parseContentDispositionFilename('attachment;filename=xxx.yaml')).toBe('xxx.yaml')
    expect(parseContentDispositionFilename('attachment; filename="xxx.yaml"; size=1')).toBe(
      'xxx.yaml'
    )
    expect(
      parseContentDispositionFilename("attachment; filename*=UTF-8''%E4%B8%AD%E6%96%87.yaml")
    ).toBe('中文.yaml')
  })

  it('returns undefined when no filename is present', () => {
    expect(parseContentDispositionFilename('attachment')).toBeUndefined()
    expect(parseContentDispositionFilename('attachment; filename=')).toBeUndefined()
    // Malformed percent-encoding has no usable fallback either.
    expect(parseContentDispositionFilename("attachment; filename*=UTF-8''%E4%B8")).toBeUndefined()
  })
})
