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

export function describeHttpError(error: unknown): string {
  if (!axios.isAxiosError(error)) return error instanceof Error ? error.message : error == null ? '' : String(error)
  const axiosError = error as AxiosError
  const code = axiosError.code ? ` (${axiosError.code})` : ''
  const status = axiosError.response?.status ? ` HTTP ${axiosError.response.status}` : ''
  return `${axiosError.message}${status}${code}`
}
