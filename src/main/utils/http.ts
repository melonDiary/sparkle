import axios, { type AxiosRequestConfig, type AxiosError } from 'axios'

export const HTTP_TIMEOUT = 30000
export const DOWNLOAD_TIMEOUT = 120000

export function localhostProxy(port: number): AxiosRequestConfig['proxy'] | undefined {
  if (!port) return undefined
  return { protocol: 'http', host: '127.0.0.1', port }
}

export function normalizeBaseUrl(url: string): string {
  return url.replace(/\/+$/, '')
}

/**
 * Extracts the filename from a `Content-Disposition` header, e.g.
 * `attachment; filename=xxx.yaml; filename*=UTF-8''%xx%xx%xx`.
 * Returns `undefined` when the header carries no usable filename.
 */
export function parseContentDispositionFilename(header: string): string | undefined {
  const encodedMatch = header.match(/filename\*=.*''([^;]*)/)
  if (encodedMatch?.[1]) {
    try {
      return decodeURIComponent(encodedMatch[1].trim()) || undefined
    } catch {
      // Malformed percent-encoding; fall back to the plain filename form.
    }
  }

  const filename = header.split('filename=')[1]
  if (!filename) return undefined

  return filename.split(';')[0].trim().replace(/"/g, '') || undefined
}

export function describeHttpError(error: unknown): string {
  if (!axios.isAxiosError(error))
    return error instanceof Error ? error.message : error == null ? '' : String(error)
  const axiosError = error as AxiosError
  const code = axiosError.code ? ` (${axiosError.code})` : ''
  const status = axiosError.response?.status ? ` HTTP ${axiosError.response.status}` : ''
  return `${axiosError.message}${status}${code}`
}
