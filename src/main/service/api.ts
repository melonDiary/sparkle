import axios, { AxiosInstance, AxiosRequestConfig, InternalAxiosRequestConfig } from 'axios'
import crypto from 'crypto'
import WebSocket from 'ws'
import { KeyManager } from './key'
import { serviceIpcPath } from '../utils/dirs'
import { appendAppLog } from '../utils/log'
import { shouldSkipServiceUnavailableFallback } from './fallback'
import { createServiceEventStream } from './event-stream'

let serviceAxios: AxiosInstance | null = null
let keyManager: KeyManager | null = null
let serviceUnavailableFallbackHandler: ((reason: unknown) => Promise<void>) | null = null
let serviceUnavailableFallbackTimer: NodeJS.Timeout | null = null
let serviceUnavailableFallbackPromise: Promise<void> | null = null
const serviceUnavailableFallbackDelay = 2000
const serviceUnavailableStatuses = [401, 403, 409, 503]

export class ServiceAPIError extends Error {
  status?: number
  responseData?: unknown

  constructor(message: string, options?: { status?: number; responseData?: unknown }) {
    super(message)
    this.name = 'ServiceAPIError'
    this.status = options?.status
    this.responseData = options?.responseData
  }
}

function getHeaderValue(config: AxiosRequestConfig, name: string): string {
  const headers = config.headers as
    | Record<string, unknown>
    | { get?: (headerName: string) => string | undefined | null }
    | undefined

  if (!headers) {
    return ''
  }

  if ('get' in headers && typeof headers.get === 'function') {
    return String(headers.get(name) || headers.get(name.toLowerCase()) || '')
  }

  return String(headers[name] || headers[name.toLowerCase()] || headers[name.toUpperCase()] || '')
}

function shouldUseJsonEncoding(config: AxiosRequestConfig): boolean {
  const contentType = getHeaderValue(config, 'Content-Type').toLowerCase()
  return contentType === '' || contentType.includes('application/json')
}

function getRequestBodyBytes(config: AxiosRequestConfig): Buffer {
  const data = config.data

  if (data == null) {
    return Buffer.alloc(0)
  }

  if (Buffer.isBuffer(data)) {
    return data
  }

  if (data instanceof Uint8Array) {
    return Buffer.from(data)
  }

  if (data instanceof ArrayBuffer) {
    return Buffer.from(data)
  }

  if (typeof data === 'string') {
    return Buffer.from(shouldUseJsonEncoding(config) ? JSON.stringify(data) : data)
  }

  if (data instanceof URLSearchParams) {
    return Buffer.from(data.toString())
  }

  if (typeof data === 'object') {
    return Buffer.from(JSON.stringify(data))
  }

  return Buffer.from(String(data))
}

function canonicalizeQuery(urlObj: URL): string {
  const source = new URLSearchParams(urlObj.search)
  const keys = Array.from(new Set(source.keys())).sort()
  const target = new URLSearchParams()

  for (const key of keys) {
    const values = source.getAll(key).sort()
    for (const value of values) {
      target.append(key, value)
    }
  }

  return target.toString()
}

function resolveRequestUrl(instance: AxiosInstance, config: AxiosRequestConfig): URL {
  return new URL(instance.getUri(config))
}

interface CanonicalRequestParts {
  timestamp: string
  nonce: string
  keyId: string
  method: string
  path: string
  query: string
  bodyHash: string
}

function buildCanonicalString(parts: CanonicalRequestParts): string {
  return [
    'SPARKLE-AUTH-V2',
    parts.timestamp,
    parts.nonce,
    parts.keyId,
    parts.method.toUpperCase(),
    parts.path || '/',
    parts.query,
    parts.bodyHash
  ].join('\n')
}

function buildServiceAuthHeaders(
  keyId: string,
  timestamp: string,
  nonce: string,
  bodyHash: string,
  signature: string
): Record<string, string> {
  return {
    'X-Auth-Version': '2',
    'X-Key-Id': keyId,
    'X-Nonce': nonce,
    'X-Content-SHA256': bodyHash,
    'X-Timestamp': timestamp,
    'X-Signature': signature
  }
}

function buildCanonicalRequest(
  instance: AxiosInstance,
  config: AxiosRequestConfig,
  timestamp: string,
  nonce: string,
  keyId: string,
  bodyHash: string
): string {
  const resolvedUrl = resolveRequestUrl(instance, config)

  return buildCanonicalString({
    timestamp,
    nonce,
    keyId,
    method: config.method || 'GET',
    path: resolvedUrl.pathname,
    query: canonicalizeQuery(resolvedUrl),
    bodyHash
  })
}

function signServiceRequest(
  instance: AxiosInstance,
  config: InternalAxiosRequestConfig
): InternalAxiosRequestConfig {
  if (keyManager?.isInitialized()) {
    const bodyBytes = getRequestBodyBytes(config)
    const bodyHash = crypto.createHash('sha256').update(bodyBytes).digest('hex')
    const timestamp = Date.now().toString()
    const nonce = crypto.randomBytes(16).toString('base64url')
    const keyId = keyManager.getKeyID()
    const canonical = buildCanonicalRequest(instance, config, timestamp, nonce, keyId, bodyHash)
    const signature = keyManager.signData(canonical)

    Object.assign(
      config.headers,
      buildServiceAuthHeaders(keyId, timestamp, nonce, bodyHash, signature)
    )
  }

  return config
}

function attachServiceAuth(instance: AxiosInstance): void {
  instance.interceptors.request.use((config) => signServiceRequest(instance, config))
}

export function setServiceUnavailableFallbackHandler(
  handler: (reason: unknown) => Promise<void>
): void {
  serviceUnavailableFallbackHandler = handler
}

export function isServiceConnectionError(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error)
  return [
    'ECONNREFUSED',
    'ECONNRESET',
    'ENOENT',
    'EPIPE',
    'ETIMEDOUT',
    'socket hang up',
    'connect ',
    'no such file'
  ].some((fragment) => message.toLowerCase().includes(fragment.toLowerCase()))
}

function getServiceErrorStatus(error: unknown): number | undefined {
  if (error instanceof ServiceAPIError) {
    return error.status
  }

  const status = (error as { response?: { status?: unknown } })?.response?.status
  return typeof status === 'number' ? status : undefined
}

export function isServiceUnavailableError(error: unknown): boolean {
  const status = getServiceErrorStatus(error)
  return (
    isServiceConnectionError(error) ||
    (status !== undefined && serviceUnavailableStatuses.includes(status))
  )
}

function scheduleServiceUnavailableFallback(reason: unknown): void {
  if (shouldSkipServiceUnavailableFallback()) return
  if (serviceUnavailableFallbackTimer || serviceUnavailableFallbackPromise) return

  serviceUnavailableFallbackTimer = setTimeout(() => {
    serviceUnavailableFallbackTimer = null
    serviceUnavailableFallbackPromise = runServiceUnavailableFallback(reason).finally(() => {
      serviceUnavailableFallbackPromise = null
    })
  }, serviceUnavailableFallbackDelay)
}

async function runServiceUnavailableFallback(reason: unknown): Promise<void> {
  if (shouldSkipServiceUnavailableFallback()) return
  if (await isServiceUsable()) return

  if (!serviceUnavailableFallbackHandler) {
    await appendAppLog(`[Service]: service unavailable fallback handler is not registered\n`)
    return
  }

  await serviceUnavailableFallbackHandler(reason).catch((error) =>
    appendAppLog(`[Service]: service unavailable fallback failed, ${error}\n`)
  )
}

async function isServiceUsable(): Promise<boolean> {
  try {
    await axios.get('/test', {
      baseURL: 'http://localhost',
      socketPath: serviceIpcPath(),
      headers: keyManager?.isInitialized() ? getServiceAuthHeaders('GET', '/test') : undefined,
      timeout: 1000,
      validateStatus: (status) => status >= 200 && status < 300
    })
    return true
  } catch {
    return false
  }
}

function getResponseErrorMessage(responseData: unknown, fallback: string): string {
  if (responseData && typeof responseData === 'object') {
    const data = responseData as Record<string, unknown>
    return String(data.message || data.error || fallback)
  }

  return fallback
}

function createServiceAPIError(error: unknown): unknown {
  const serviceError = error as {
    response?: { data?: unknown; status?: number }
    message?: string
  }

  if (serviceError.response?.data) {
    const message = getResponseErrorMessage(
      serviceError.response.data,
      serviceError.message || '请求失败'
    )

    return new ServiceAPIError(message, {
      status: serviceError.response.status,
      responseData: serviceError.response.data
    })
  }

  if (error instanceof Error) {
    return new ServiceAPIError(error.message)
  }

  return error
}

function handleServiceAxiosError(error: unknown): Promise<never> {
  const serviceError = createServiceAPIError(error)

  if (isServiceUnavailableError(error) || isServiceUnavailableError(serviceError)) {
    scheduleServiceUnavailableFallback(serviceError)
  }

  return Promise.reject(serviceError)
}

export const initServiceAPI = (km: KeyManager): void => {
  keyManager = km

  serviceAxios = axios.create({
    baseURL: 'http://localhost',
    socketPath: serviceIpcPath(),
    timeout: 15000,
    headers: {
      'Content-Type': 'application/json'
    }
  })

  attachServiceAuth(serviceAxios)

  serviceAxios.interceptors.response.use((response) => response.data, handleServiceAxiosError)
}

export const createSignedServiceAxios = (baseURL = 'http://localhost'): AxiosInstance => {
  const instance = axios.create({
    baseURL,
    socketPath: serviceIpcPath(),
    timeout: 15000,
    headers: {
      'Content-Type': 'application/json'
    }
  })

  attachServiceAuth(instance)

  instance.interceptors.response.use((response) => response.data, handleServiceAxiosError)

  return instance
}

export const getServiceAuthHeaders = (
  method: string,
  pathWithQuery: string,
  body: Buffer = Buffer.alloc(0)
): Record<string, string> => {
  if (!keyManager?.isInitialized()) {
    throw new Error('服务 API 未初始化')
  }

  const bodyHash = crypto.createHash('sha256').update(body).digest('hex')
  const timestamp = Date.now().toString()
  const nonce = crypto.randomBytes(16).toString('base64url')
  const keyId = keyManager.getKeyID()
  const urlObj = new URL(pathWithQuery, 'http://localhost')
  const canonical = buildCanonicalString({
    timestamp,
    nonce,
    keyId,
    method,
    path: urlObj.pathname,
    query: canonicalizeQuery(urlObj),
    bodyHash
  })
  const signature = keyManager.signData(canonical)

  return buildServiceAuthHeaders(keyId, timestamp, nonce, bodyHash, signature)
}

export const getServiceAxios = (): AxiosInstance => {
  if (!serviceAxios) {
    throw new Error('服务 API 未初始化')
  }
  return serviceAxios
}

export const getKeyManager = (): KeyManager => {
  if (!keyManager) {
    throw new Error('密钥管理器未初始化')
  }
  return keyManager
}

export const ping = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.get('/ping')
}

export const test = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.get('/test')
}

export const getCoreStatus = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.get('/core')
}

export interface ServiceCoreLaunchProfile {
  core_path?: string
  args?: string[]
  mode?: 'auto' | 'sandbox' | 'direct'
  safe_paths?: string[]
  env?: Record<string, string | undefined>
  mihomo_cpu_priority?: Priority
  log_path?: string
  save_logs?: boolean
  max_log_file_size_mb?: number
}

export type ServiceCoreEventType =
  | 'starting'
  | 'started'
  | 'stopping'
  | 'stopped'
  | 'exited'
  | 'restarting'
  | 'restart_failed'
  | 'takeover'
  | 'ready'
  | 'failed'
  | 'log'

export interface ServiceCoreEvent {
  seq?: number
  type: ServiceCoreEventType
  time: string
  running: boolean
  pid?: number
  old_pid?: number
  message?: string
  error?: string
  data?: Record<string, string>
}

export type ServiceSysproxyEventType =
  | 'guard_started'
  | 'guard_stopped'
  | 'guard_changed'
  | 'guard_restored'
  | 'guard_restore_failed'
  | 'guard_check_failed'
  | 'guard_watch_failed'

export interface ServiceSysproxyEvent {
  seq?: number
  type: ServiceSysproxyEventType
  time: string
  guard: boolean
  mode?: string
  message?: string
  error?: string
}

export const createServiceWebSocket = (pathWithQuery: string): WebSocket => {
  return new WebSocket(`ws+unix:${serviceIpcPath()}:${pathWithQuery}`, {
    headers: getServiceAuthHeaders('GET', pathWithQuery)
  })
}

type ServiceCoreEventHandler = (event: ServiceCoreEvent) => void | Promise<void>
type ServiceCoreEventStreamState = 'connected' | 'disconnected'
type ServiceCoreEventStreamHandler = (state: ServiceCoreEventStreamState) => void | Promise<void>
type ServiceSysproxyEventHandler = (event: ServiceSysproxyEvent) => void | Promise<void>

// Both event streams previously duplicated ~150 lines of start/stop/reconnect/
// dispatch logic. They now share the engine in ./event-stream, with only the
// socket path, log label, and stream-state emission differing. The public
// function surface is unchanged so callers need no edits.
const coreEventStream = createServiceEventStream<ServiceCoreEvent>({
  label: 'core events',
  connect: () => createServiceWebSocket('/core/events'),
  scheduleFallback: scheduleServiceUnavailableFallback,
  emitStreamState: true,
  fallbackOnClose: true,
  parse: (raw) => JSON.parse(raw) as ServiceCoreEvent
})

const sysproxyEventStream = createServiceEventStream<ServiceSysproxyEvent>({
  label: 'sysproxy events',
  connect: () => createServiceWebSocket('/sysproxy/events'),
  scheduleFallback: scheduleServiceUnavailableFallback,
  emitStreamState: false,
  fallbackOnClose: true,
  parse: (raw) => JSON.parse(raw) as ServiceSysproxyEvent
})

export function subscribeServiceCoreEvents(handler: ServiceCoreEventHandler): () => void {
  return coreEventStream.subscribeEvent(handler)
}

export function subscribeServiceCoreEventStream(
  handler: ServiceCoreEventStreamHandler
): () => void {
  const wrapped = coreEventStream.subscribeStreamState?.(handler)
  return wrapped ?? (() => {})
}

export function subscribeServiceSysproxyEvents(handler: ServiceSysproxyEventHandler): () => void {
  return sysproxyEventStream.subscribeEvent(handler)
}

export function startServiceCoreEventStream(): Promise<void> {
  return coreEventStream.start()
}

export function stopServiceCoreEventStream(): void {
  coreEventStream.stop()
}

export function startServiceSysproxyEventStream(): Promise<void> {
  return sysproxyEventStream.start()
}

export function stopServiceSysproxyEventStream(): void {
  sysproxyEventStream.stop()
}

export const startCore = async (
  profile?: ServiceCoreLaunchProfile
): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.post('/core/start', profile)
}

export const stopCore = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.post('/core/stop')
}

export const restartCore = async (
  profile?: ServiceCoreLaunchProfile
): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.post('/core/restart', profile)
}

export const patchCoreProfile = async (
  profile: Partial<ServiceCoreLaunchProfile>
): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.patch('/core/profile', profile)
}

export const getProxyStatus = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.get('/sysproxy/status')
}

export const stopServiceApi = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.post('/service/stop')
}

export const restartServiceApi = async (): Promise<Record<string, unknown>> => {
  const instance = getServiceAxios()
  return await instance.post('/service/restart')
}

export const setPac = async (
  url: string,
  device?: string,
  onlyActiveDevice?: boolean,
  useRegistry?: boolean,
  guard?: boolean
): Promise<void> => {
  const instance = getServiceAxios()
  return await instance.post('/sysproxy/pac', {
    url,
    device,
    only_active_device: onlyActiveDevice,
    use_registry: useRegistry,
    guard
  })
}

export const setProxy = async (
  server: string,
  bypass?: string,
  device?: string,
  onlyActiveDevice?: boolean,
  useRegistry?: boolean,
  guard?: boolean
): Promise<void> => {
  const instance = getServiceAxios()
  return await instance.post('/sysproxy/proxy', {
    server,
    bypass,
    device,
    only_active_device: onlyActiveDevice,
    use_registry: useRegistry,
    guard
  })
}

export const disableProxy = async (
  device?: string,
  onlyActiveDevice?: boolean,
  useRegistry?: boolean
): Promise<void> => {
  const instance = getServiceAxios()
  return await instance.post('/sysproxy/disable', {
    device,
    only_active_device: onlyActiveDevice,
    use_registry: useRegistry
  })
}

export const setSysDns = async (device?: string, servers?: string[]): Promise<void> => {
  const instance = getServiceAxios()
  return await instance.post('/sys/dns/set', { servers, device })
}
