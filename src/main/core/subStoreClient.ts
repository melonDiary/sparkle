import axios from 'axios'
import { getAppConfig } from '../config'
import { subStorePort } from '../resolve/server'
import { appendAppLog } from '../utils/log'
import { HTTP_TIMEOUT, describeHttpError, normalizeBaseUrl } from '../utils/http'

async function getBaseUrl(): Promise<string> {
  const { useCustomSubStore = false, customSubStoreUrl = '' } = await getAppConfig()
  if (useCustomSubStore) {
    if (!customSubStoreUrl.trim()) throw new Error('自定义 Sub-Store 地址为空')
    return normalizeBaseUrl(customSubStoreUrl)
  }
  if (!subStorePort) throw new Error('Sub-Store 服务尚未启动，请稍后重试')
  return `http://127.0.0.1:${subStorePort}`
}

export async function waitForSubStoreReady(port: number, timeoutMs = 10000): Promise<void> {
  const startedAt = Date.now()
  let lastError: unknown
  while (Date.now() - startedAt < timeoutMs) {
    try {
      await axios.get(`http://127.0.0.1:${port}/api/subs`, { timeout: 1000 })
      return
    } catch (error) {
      lastError = error
      await new Promise<void>((resolve) => {
        setTimeout(resolve, 200)
      })
    }
  }
  throw new Error(
    `Sub-Store 服务启动超时：${describeHttpError(lastError)}`
  )
}

export async function requestSubStore<T>(endpoint: string): Promise<T> {
  const baseUrl = await getBaseUrl()
  try {
    const response = await axios.get<{ data: T }>(`${baseUrl}/${endpoint}`, {
      responseType: 'json',
      timeout: HTTP_TIMEOUT,
      validateStatus: (status) => status >= 200 && status < 300
    })
    return response.data.data
  } catch (error) {
    await appendAppLog(`[SubStore]: request ${endpoint} failed, ${describeHttpError(error)}\n`).catch(() => {})
    throw error
  }
}
