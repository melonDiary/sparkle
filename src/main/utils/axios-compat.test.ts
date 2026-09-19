import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import axios, { type AxiosError, type AxiosInstance } from 'axios'
import { rmSync } from 'fs'
import { createServer, type Server } from 'http'
import { tmpdir } from 'os'
import { join } from 'path'
import { describeHttpError } from './http'

/**
 * Guards the axios runtime contracts the main process relies on. Type checking
 * cannot catch a change in interceptor unwrapping or cancellation semantics, so
 * the service API transport is exercised over a real local socket here.
 *
 * The socket form is platform-specific: POSIX listens on a filesystem path,
 * while Windows rejects a `.sock` path with EACCES and requires a named pipe
 * under `\\.\pipe\`. Both are valid `socketPath` values for their platform.
 */
const isWindows = process.platform === 'win32'
// `String.raw` keeps the `\\.\pipe\` prefix literal instead of half-escaping it.
const socketPath = isWindows
  ? String.raw`\\.\pipe\sparkle-axios-${process.pid}`
  : join(tmpdir(), `sparkle-axios-${process.pid}.sock`)

/** Named pipes are not filesystem entries, so only POSIX paths are unlinked. */
function removeSocketPath(): void {
  if (!isWindows) rmSync(socketPath, { force: true })
}

let server: Server

beforeAll(async () => {
  removeSocketPath()
  server = createServer((req, res) => {
    if (req.url === '/ok') {
      res.writeHead(200, { 'Content-Type': 'application/json' })
      res.end(JSON.stringify({ value: 42 }))
      return
    }
    res.writeHead(503, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ error: 'unavailable' }))
  })
  await new Promise<void>((resolve) => {
    server.listen(socketPath, resolve)
  })
})

afterAll(async () => {
  await new Promise<void>((resolve) => {
    server.close(() => resolve())
  })
  removeSocketPath()
})

function createServiceLikeAxios(): AxiosInstance {
  const instance = axios.create({ baseURL: 'http://localhost', socketPath, timeout: 15000 })
  instance.interceptors.response.use(
    (response) => response.data,
    (error) => Promise.reject(error)
  )
  return instance
}

describe('axios main process contracts', () => {
  it('unwraps response data through the interceptor over a unix socket', async () => {
    const instance = createServiceLikeAxios()
    await expect(instance.get('/ok')).resolves.toEqual({ value: 42 })
  })

  it('honours validateStatus for non-2xx responses', async () => {
    const instance = createServiceLikeAxios()
    await expect(instance.get('/fail', { validateStatus: () => true })).resolves.toEqual({
      error: 'unavailable'
    })
  })

  it('rejects with an axios error carrying the response status', async () => {
    const instance = createServiceLikeAxios()
    const error = await instance.get('/fail').catch((e: unknown) => e)
    expect(axios.isAxiosError(error)).toBe(true)
    expect((error as AxiosError).response?.status).toBe(503)
    expect(describeHttpError(error)).toContain('HTTP 503')
  })

  it('keeps the CancelToken contract used by the updater download', async () => {
    const source = axios.CancelToken.source()
    source.cancel('用户取消下载')
    const error = await axios
      .get('http://localhost/never', { cancelToken: source.token })
      .catch((e: unknown) => e)
    expect(axios.isCancel(error)).toBe(true)
  })

  it('maps AbortController aborts to a cancel error', async () => {
    const controller = new AbortController()
    controller.abort()
    const error = await axios
      .get('http://localhost/never', { signal: controller.signal })
      .catch((e: unknown) => e)
    expect(axios.isCancel(error)).toBe(true)
  })
})
