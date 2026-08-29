import axios, { AxiosInstance } from 'axios'
import { getAppConfig, getControledMihomoConfig } from '../config'
import { mainWindow } from '..'
import WebSocket from 'ws'
import { customTrayWindow, tray } from '../resolve/tray'
import { calcTraffic } from '../utils/calc'
import { getRuntimeConfig } from './factory'
import { floatingWindow } from '../resolve/floatingWindow'
import { mihomoIpcPath, serviceIpcPath } from '../utils/dirs'
import { publishMihomoLog } from '../utils/log'
import { createSignedServiceAxios, getServiceAuthHeaders } from '../service/api'
import { createMihomoStream } from './mihomo-stream'
import { IPC_EVENTS } from '../../shared/ipc'

let axiosIns: AxiosInstance = null!

interface LatestSender<T> {
  send(value: T): void
  clear(): void
}

function createLatestSender<T>(intervalMs: number, sink: (value: T) => void): LatestSender<T> {
  let pending: T | undefined
  let timer: NodeJS.Timeout | null = null

  const flush = (): void => {
    timer = null
    if (pending === undefined) return
    const value = pending
    pending = undefined
    sink(value)
    timer = setTimeout(flush, intervalMs)
  }

  return {
    send(value: T): void {
      if (!timer) {
        sink(value)
        timer = setTimeout(flush, intervalMs)
        return
      }
      pending = value
    },
    clear(): void {
      pending = undefined
      if (timer) {
        clearTimeout(timer)
        timer = null
      }
    }
  }
}

const mihomoTrafficSender = createLatestSender(100, (json: ControllerTraffic) => {
  mainWindow?.webContents.send(IPC_EVENTS.MIHOMO_TRAFFIC, json)
  if (process.platform !== 'linux') {
    tray?.setToolTip(
      '↑' +
        `${calcTraffic(json.up)}/s`.padStart(9) +
        '\n↓' +
        `${calcTraffic(json.down)}/s`.padStart(9)
    )
  }
  floatingWindow?.webContents.send(IPC_EVENTS.MIHOMO_TRAFFIC, json)
  if (customTrayWindow && !customTrayWindow.isDestroyed() && customTrayWindow.isVisible()) {
    customTrayWindow.webContents.send(IPC_EVENTS.MIHOMO_TRAFFIC, json)
  }
})
const mihomoConnectionsSender = createLatestSender(200, (json: ControllerConnections) => {
  mainWindow?.webContents.send(IPC_EVENTS.MIHOMO_CONNECTIONS, json)
})

const mihomoTrafficStream = createMihomoStream({
  connect: () => mihomoWs('/traffic'),
  onMessage: handleMihomoTrafficMessage
})
const mihomoMemoryStream = createMihomoStream({
  connect: () => mihomoWs('/memory'),
  onMessage: handleMihomoMemoryMessage
})
const mihomoLogsStream = createMihomoStream({
  connect: () => mihomoLogsPath().then((path) => mihomoWs(path)),
  onMessage: handleMihomoLogsMessage
})
const mihomoConnectionsStream = createMihomoStream({
  connect: () => mihomoConnectionsPath().then((path) => mihomoWs(path)),
  onMessage: handleMihomoConnectionsMessage
})
let axiosMode: 'direct' | 'service' | null = null

/** Clear the cached controller client after the core stops or changes. */
export function resetMihomoApi(): void {
  axiosIns = null!
  axiosMode = null
  mihomoTrafficStream.stop()
  mihomoMemoryStream.stop()
  mihomoLogsStream.stop()
  mihomoConnectionsStream.stop()
  mihomoTrafficSender.clear()
  mihomoConnectionsSender.clear()
}

export const getAxios = async (force: boolean = false): Promise<AxiosInstance> => {
  const { corePermissionMode = 'elevated' } = await getAppConfig()
  const nextMode = corePermissionMode === 'service' ? 'service' : 'direct'
  const currentBaseURL =
    nextMode === 'service' ? 'http://localhost/core/controller' : 'http://localhost'

  if (axiosIns && (axiosIns.defaults.baseURL !== currentBaseURL || axiosMode !== nextMode)) {
    force = true
  }

  if (axiosIns && !force) return axiosIns

  axiosMode = nextMode
  if (nextMode === 'service') {
    axiosIns = createSignedServiceAxios(currentBaseURL)
  } else {
    axiosIns = axios.create({
      baseURL: currentBaseURL,
      socketPath: mihomoIpcPath(),
      timeout: 15000
    })

    axiosIns.interceptors.response.use(
      (response) => {
        return response.data
      },
      (error) => {
        if (error.response && error.response.data) {
          return Promise.reject(error.response.data)
        }
        return Promise.reject(error)
      }
    )
  }
  return axiosIns
}

function handleMihomoTrafficMessage(data: string): void {
  mihomoTrafficSender.send(JSON.parse(data) as ControllerTraffic)
}

function handleMihomoMemoryMessage(data: string): void {
  mainWindow?.webContents.send(IPC_EVENTS.MIHOMO_MEMORY, JSON.parse(data) as ControllerMemory)
}

function handleMihomoLogsMessage(data: string): void {
  publishMihomoLog(JSON.parse(data) as ControllerLog)
}

function handleMihomoConnectionsMessage(data: string): void {
  mihomoConnectionsSender.send(JSON.parse(data) as ControllerConnections)
}

async function mihomoLogsPath(): Promise<string> {
  const { realtimeLogLevel } = await getAppConfig()
  const { 'log-level': logLevel = 'info' } = await getControledMihomoConfig()
  return `/logs?level=${realtimeLogLevel ?? logLevel}`
}

async function mihomoConnectionsPath(): Promise<string> {
  const { connectionInterval = 500 } = await getAppConfig()
  return `/connections?interval=${connectionInterval}`
}

function formatMihomoApiError(error: unknown): string {
  if (error instanceof Error) return error.message
  if (typeof error === 'string') return error
  try {
    return JSON.stringify(error)
  } catch {
    return String(error)
  }
}

const mihomoWs = async (path: string): Promise<WebSocket> => {
  const { corePermissionMode = 'elevated' } = await getAppConfig()
  if (corePermissionMode !== 'service') {
    return new WebSocket(`ws+unix:${mihomoIpcPath()}:${path}`)
  }

  const servicePath = `/core/controller${path}`
  return new WebSocket(`ws+unix:${serviceIpcPath()}:${servicePath}`, {
    headers: getServiceAuthHeaders('GET', servicePath)
  })
}

export async function mihomoVersion(): Promise<ControllerVersion> {
  const instance = await getAxios()
  const { corePermissionMode = 'elevated' } = await getAppConfig()
  const controller =
    corePermissionMode === 'service' ? 'service controller' : `命名管道 ${mihomoIpcPath()}`
  try {
    return await instance.get('/version')
  } catch (error) {
    throw new Error(`内核控制器连接失败（${controller}）：${formatMihomoApiError(error)}`)
  }
}

export const mihomoConfig = async (): Promise<ControllerConfigs> => {
  const instance = await getAxios()
  return await instance.get('/configs')
}

export const patchMihomoConfig = async (patch: Partial<ControllerConfigs>): Promise<void> => {
  const instance = await getAxios()
  return await instance.patch('/configs', patch)
}

export const mihomoCloseConnection = async (id: string): Promise<void> => {
  const instance = await getAxios()
  return await instance.delete(`/connections/${encodeURIComponent(id)}`)
}

export const mihomoGetConnections = async (): Promise<ControllerConnections> => {
  const instance = await getAxios()
  return await instance.get('/connections')
}

export const mihomoCloseConnections = async (name?: string): Promise<void> => {
  const instance = await getAxios()
  if (name) {
    const connectionsInfo = await mihomoGetConnections()
    const targetConnections =
      connectionsInfo?.connections?.filter((conn) => conn.chains && conn.chains.includes(name)) ||
      []
    for (const conn of targetConnections) {
      try {
        await mihomoCloseConnection(conn.id)
      } catch (error) {
        // ignore
      }
    }
  } else {
    return await instance.delete('/connections')
  }
}

export const mihomoRules = async (): Promise<ControllerRules> => {
  const instance = await getAxios()
  return await instance.get('/rules')
}

export const mihomoProxies = async (): Promise<ControllerProxies> => {
  const instance = await getAxios()
  return await instance.get('/proxies')
}

function isControllerGroupDetail(
  proxy: ControllerProxiesDetail | ControllerGroupDetail | undefined
): proxy is ControllerGroupDetail {
  return Boolean(proxy && 'all' in proxy)
}

const PROVIDER_DETAIL_FETCH_THRESHOLD = 8

async function resolveProviderProxies(
  names: Set<string>,
  providerNames: Set<string>,
  fallbackToAllProviders: boolean
): Promise<Record<string, ControllerProxiesDetail>> {
  if (names.size === 0) return {}

  const providers =
    fallbackToAllProviders || providerNames.size > PROVIDER_DETAIL_FETCH_THRESHOLD
      ? Object.values((await mihomoProxyProviders()).providers)
      : await Promise.all([...providerNames].map((name) => mihomoProxyProvider(name)))

  const providerProxies: Record<string, ControllerProxiesDetail> = {}
  providers.forEach((provider) => {
    provider.proxies?.forEach((proxy) => {
      if (names.has(proxy.name)) {
        providerProxies[proxy.name] = proxy
      }
    })
  })
  return providerProxies
}

export const mihomoGroups = async (): Promise<ControllerMixedGroup[]> => {
  const { mode = 'rule' } = await getControledMihomoConfig()
  if (mode === 'direct') return []
  const [proxies, runtime] = await Promise.all([mihomoProxies(), getRuntimeConfig()])
  const rawGroups: { group: ControllerGroupDetail & { testUrl?: string }; providers: string[] }[] =
    []

  runtime?.['proxy-groups']?.forEach((group: { name: string; url?: string; use?: string[] }) => {
    const proxy = proxies.proxies[group.name]
    if (isControllerGroupDetail(proxy) && !proxy.hidden) {
      rawGroups.push({ group: { ...proxy, testUrl: group.url }, providers: group.use || [] })
    }
  })

  if (!rawGroups.find(({ group }) => group.name === 'GLOBAL')) {
    const global = proxies.proxies['GLOBAL']
    if (isControllerGroupDetail(global) && !global.hidden) {
      rawGroups.push({ group: global, providers: [] })
    }
  }

  const missingProxyNames = new Set<string>()
  const providerNames = new Set<string>()
  let fallbackToAllProviders = false
  rawGroups.forEach(({ group, providers }) => {
    group.all.forEach((name) => {
      if (!proxies.proxies[name]) {
        missingProxyNames.add(name)
        if (providers.length > 0) {
          providers.forEach((provider) => providerNames.add(provider))
        } else {
          fallbackToAllProviders = true
        }
      }
    })
  })

  const providerProxies = await resolveProviderProxies(
    missingProxyNames,
    providerNames,
    fallbackToAllProviders
  )
  const groups: ControllerMixedGroup[] = []
  rawGroups.forEach(({ group }) => {
    const newAll = group.all
      .map((name) => proxies.proxies[name] || providerProxies[name])
      .filter((proxy): proxy is ControllerProxiesDetail | ControllerGroupDetail => Boolean(proxy))
    groups.push({ ...group, all: newAll })
  })

  if (mode === 'global') {
    const global = groups.findIndex((group) => group.name === 'GLOBAL')
    if (global > 0) groups.unshift(groups.splice(global, 1)[0])
  }
  return groups
}

export const mihomoProxyProviders = async (): Promise<ControllerProxyProviders> => {
  const instance = await getAxios()
  return await instance.get('/providers/proxies')
}

const mihomoProxyProvider = async (name: string): Promise<ControllerProxyProviderDetail> => {
  const instance = await getAxios()
  return await instance.get(`/providers/proxies/${encodeURIComponent(name)}`)
}

export const mihomoUpdateProxyProviders = async (name: string): Promise<void> => {
  const instance = await getAxios()
  return await instance.put(`/providers/proxies/${encodeURIComponent(name)}`)
}

export const mihomoRuleProviders = async (): Promise<ControllerRuleProviders> => {
  const instance = await getAxios()
  return await instance.get('/providers/rules')
}

export const mihomoUpdateRuleProviders = async (name: string): Promise<void> => {
  const instance = await getAxios()
  return await instance.put(`/providers/rules/${encodeURIComponent(name)}`)
}

export const mihomoChangeProxy = async (
  group: string,
  proxy: string
): Promise<ControllerProxiesDetail> => {
  const instance = await getAxios()
  return await instance.put(`/proxies/${encodeURIComponent(group)}`, { name: proxy })
}

export const mihomoUnfixedProxy = async (group: string): Promise<ControllerProxiesDetail> => {
  const instance = await getAxios()
  return await instance.delete(`/proxies/${encodeURIComponent(group)}`)
}

export const mihomoProxyDelay = async (
  proxy: string,
  url?: string,
  provider?: string
): Promise<ControllerProxiesDelay> => {
  const appConfig = await getAppConfig()
  const { delayTestUrl, delayTestTimeout } = appConfig
  const instance = await getAxios()
  const path = provider
    ? `/providers/proxies/${encodeURIComponent(provider)}/${encodeURIComponent(proxy)}/healthcheck`
    : `/proxies/${encodeURIComponent(proxy)}/delay`
  return await instance.get(path, {
    params: {
      url: url || delayTestUrl || 'https://www.gstatic.com/generate_204',
      timeout: delayTestTimeout || 5000
    }
  })
}

export const mihomoGroupDelay = async (
  group: string,
  url?: string
): Promise<ControllerGroupDelay> => {
  const appConfig = await getAppConfig()
  const { delayTestUrl, delayTestTimeout } = appConfig
  const instance = await getAxios()
  return await instance.get(`/group/${encodeURIComponent(group)}/delay`, {
    params: {
      url: url || delayTestUrl || 'https://www.gstatic.com/generate_204',
      timeout: delayTestTimeout || 5000
    }
  })
}

export const mihomoRulesDisable = async (rules: Record<string, boolean>): Promise<void> => {
  const instance = await getAxios()
  return await instance.patch(`/rules/disable`, rules)
}

export const mihomoUpgrade = async (channel: string): Promise<void> => {
  if (process.platform === 'win32') await patchMihomoConfig({ 'log-level': 'info' })
  const instance = await getAxios()
  return await instance.post(`/upgrade?channel=${encodeURIComponent(channel)}`, undefined, {
    timeout: 90000
  })
}

export const mihomoUpgradeGeo = async (): Promise<void> => {
  const instance = await getAxios()
  return await instance.post('/upgrade/geo', undefined, { timeout: 90000 })
}

export const mihomoUpgradeUI = async (): Promise<void> => {
  const instance = await getAxios()
  return await instance.post('/upgrade/ui', undefined, { timeout: 90000 })
}

export const startMihomoTraffic = (): Promise<void> => mihomoTrafficStream.start()

export const stopMihomoTraffic = (): void => mihomoTrafficStream.stop()

export const startMihomoMemory = (): Promise<void> => mihomoMemoryStream.start()

export const stopMihomoMemory = (): void => mihomoMemoryStream.stop()

export const startMihomoLogs = (): Promise<void> => mihomoLogsStream.start()

export const stopMihomoLogs = (): void => mihomoLogsStream.stop()

export const restartMihomoLogs = (): Promise<void> => mihomoLogsStream.restart()
export const startMihomoConnections = (): Promise<void> => mihomoConnectionsStream.start()

export const stopMihomoConnections = (): void => mihomoConnectionsStream.stop()

export const restartMihomoConnections = (): Promise<void> => mihomoConnectionsStream.restart()
