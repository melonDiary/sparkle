import { getAppConfig, getControledMihomoConfig } from '../config'
import { Worker } from 'worker_threads'
import {
  mihomoWorkDir,
  subStoreBackendPath,
  subStoreDir,
  subStoreFrontendDir,
  subStoreTempDir
} from '../utils/dirs'
import subStoreIcon from '../../../resources/subStoreIcon.png?asset'
import { existsSync, mkdirSync } from 'fs'
import { writeFile, rm, cp } from 'fs/promises'
import http from 'http'
import net from 'net'
import path from 'path'
import { nativeImage } from 'electron'
import express from 'express'
import axios from 'axios'
import AdmZip from 'adm-zip'
import { appendAppLog, createLogWritable } from '../utils/log'
import { createHash } from 'crypto'
import { DOWNLOAD_TIMEOUT, HTTP_TIMEOUT } from '../utils/http'
import { waitForSubStoreReady } from '../core/subStoreClient'

export let pacPort: number
export let subStorePort: number
export let subStoreFrontendPort: number
let subStoreFrontendServer: http.Server
let subStoreBackendWorker: Worker

interface ReleaseAsset {
  name: string
  browser_download_url: string
  digest?: string
}

async function downloadReleaseAsset(repo: string, file: string, mixedPort: number) {
  const proxy =
    mixedPort != 0
      ? { proxy: { protocol: 'http' as const, host: '127.0.0.1', port: mixedPort } }
      : {}
  const { data: release } = await axios.get<{ assets: ReleaseAsset[] }>(
    `https://api.github.com/repos/${repo}/releases/latest`,
    {
      headers: { Accept: 'application/vnd.github.v3+json' },
      timeout: HTTP_TIMEOUT,
      ...proxy
    }
  )
  const asset = release.assets.find((asset) => asset.name === file)
  if (!asset?.browser_download_url || !asset.digest?.match(/^sha256:[a-f\d]{64}$/i)) {
    throw new Error(`无法从 GitHub Release 中找到 "${file}" 对应的 SHA-256 信息`)
  }
  const { data } = await axios.get(asset.browser_download_url, {
    responseType: 'arraybuffer',
    headers: { 'Content-Type': 'application/octet-stream' },
    timeout: DOWNLOAD_TIMEOUT,
    ...proxy
  })
  const buffer = Buffer.from(data)
  if (createHash('sha256').update(buffer).digest('hex') !== asset.digest.slice(7).toLowerCase()) {
    throw new Error(`SHA-256 校验失败："${file}" 哈希不匹配`)
  }
  return buffer
}

const defaultPacScript = `
function FindProxyForURL(url, host) {
  return "PROXY 127.0.0.1:%mixed-port%; SOCKS5 127.0.0.1:%mixed-port%; DIRECT;";
}
`

export function findAvailablePort(startPort: number): Promise<number> {
  return new Promise((resolve, reject) => {
    const server = net.createServer()
    // `once` keeps each probe's listener from outliving its own attempt: a failed
    // bind either moves on to the next port or rejects at the top of the range.
    server.once('error', (error) => {
      server.close()
      if (startPort < 65535) {
        resolve(findAvailablePort(startPort + 1))
      } else {
        reject(error)
      }
    })
    server.once('listening', () => {
      server.close(() => {
        resolve(startPort)
      })
    })
    server.listen(startPort, '127.0.0.1')
  })
}

/**
 * A binding failure must reach the caller instead of surfacing as an unhandled
 * 'error' event, which would take down the main process. The permanent logger is
 * attached first so errors after a successful bind stay handled as well.
 */
async function listenServer(
  server: http.Server,
  port: number,
  host: string,
  label: string
): Promise<void> {
  server.on('error', (error) => {
    void appendAppLog(`[Server]: ${label} error, ${error}\n`).catch(() => {})
  })

  await new Promise<void>((resolve, reject) => {
    const onError = (error: Error): void => {
      server.off('listening', onListening)
      reject(error)
    }
    const onListening = (): void => {
      server.off('error', onError)
      resolve()
    }

    server.once('error', onError)
    server.once('listening', onListening)
    server.listen(port, host)
  })
}

let pacServer: http.Server

export async function startPacServer(): Promise<void> {
  await stopPacServer()
  const { sysProxy } = await getAppConfig()
  const { mode = 'manual', host: cHost, pacScript } = sysProxy
  if (mode !== 'auto') {
    return
  }
  const host = cHost || '127.0.0.1'
  let script = pacScript || defaultPacScript
  const { 'mixed-port': port = 7890 } = await getControledMihomoConfig()
  script = script.replaceAll('%mixed-port%', port.toString())
  pacPort = await findAvailablePort(10000)
  pacServer = http.createServer((_req, res) => {
    res.writeHead(200, { 'Content-Type': 'application/x-ns-proxy-autoconfig' })
    res.end(script)
  })
  await listenServer(pacServer, pacPort, host, 'PAC')
}

export async function stopPacServer(): Promise<void> {
  if (pacServer) {
    await new Promise<void>((resolve) => {
      pacServer.close(() => resolve())
    })
    pacServer = undefined as unknown as http.Server
  }
}

export async function startSubStoreFrontendServer(): Promise<void> {
  const { useSubStore = true, subStoreHost = '127.0.0.1' } = await getAppConfig()
  if (!useSubStore) return
  await stopSubStoreFrontendServer()
  subStoreFrontendPort = await findAvailablePort(14122)
  const app = express()
  const frontendDir = subStoreFrontendDir()
  app.use(express.static(frontendDir))
  app.use((_req, res) => {
    res.sendFile(path.join(frontendDir, 'index.html'))
  })
  subStoreFrontendServer = http.createServer(app)
  await listenServer(subStoreFrontendServer, subStoreFrontendPort, subStoreHost, 'Sub-Store 前端')
}

export async function stopSubStoreFrontendServer(): Promise<void> {
  if (subStoreFrontendServer) {
    await new Promise<void>((resolve) => {
      subStoreFrontendServer.close(() => resolve())
    })
    subStoreFrontendServer = undefined as unknown as http.Server
  }
}

export async function startSubStoreBackendServer(): Promise<void> {
  const {
    useSubStore = true,
    useCustomSubStore = false,
    useProxyInSubStore = false,
    subStoreHost = '127.0.0.1',
    subStoreBackendSyncCron = '',
    subStoreBackendDownloadCron = '',
    subStoreBackendUploadCron = ''
  } = await getAppConfig()
  const { 'mixed-port': port = 7890 } = await getControledMihomoConfig()
  if (!useSubStore) return
  if (useCustomSubStore) {
    // The remote/custom backend is not owned by Sparkle. Only terminate a
    // worker reference that Sparkle itself created before switching modes.
    await stopSubStoreBackendServer()
    subStorePort = undefined as unknown as number
    return
  }
  {
    await stopSubStoreBackendServer()
    subStorePort = await findAvailablePort(38324)
    const icon = nativeImage.createFromPath(subStoreIcon)
    icon.toDataURL()
    const stdout = createLogWritable('substore')
    const stderr = createLogWritable('substore')
    const env = {
      SUB_STORE_BACKEND_API_PORT: subStorePort.toString(),
      SUB_STORE_BACKEND_API_HOST: subStoreHost,
      SUB_STORE_DATA_BASE_PATH: subStoreDir(),
      SUB_STORE_BACKEND_CUSTOM_ICON: icon.toDataURL(),
      SUB_STORE_BACKEND_CUSTOM_NAME: 'Sparkle',
      SUB_STORE_BACKEND_SYNC_CRON: subStoreBackendSyncCron,
      SUB_STORE_BACKEND_DOWNLOAD_CRON: subStoreBackendDownloadCron,
      SUB_STORE_BACKEND_UPLOAD_CRON: subStoreBackendUploadCron,
      SUB_STORE_MMDB_COUNTRY_PATH: path.join(mihomoWorkDir(), 'country.mmdb'),
      SUB_STORE_MMDB_ASN_PATH: path.join(mihomoWorkDir(), 'ASN.mmdb')
    }
    subStoreBackendWorker = new Worker(subStoreBackendPath(), {
      env: useProxyInSubStore
        ? {
            ...env,
            HTTP_PROXY: `http://127.0.0.1:${port}`,
            HTTPS_PROXY: `http://127.0.0.1:${port}`,
            ALL_PROXY: `http://127.0.0.1:${port}`
          }
        : env
    })
    const worker = subStoreBackendWorker
    worker.on('error', (error) => {
      void appendAppLog(`[SubStore]: backend worker error, ${error}\n`).catch(() => {})
    })
    worker.on('exit', (code) => {
      if (subStoreBackendWorker === worker && code !== 0) {
        void appendAppLog(`[SubStore]: backend worker exited unexpectedly, code: ${code}\n`).catch(
          () => {}
        )
      }
    })
    worker.stdout.pipe(stdout)
    worker.stderr.pipe(stderr)
    try {
      await waitForSubStoreReady(subStorePort)
    } catch (error) {
      await appendAppLog(`[SubStore]: backend worker was not ready, ${error}\n`).catch(() => {})
      await stopSubStoreBackendServer()
      throw error
    }
  }
}

export async function stopSubStoreBackendServer(): Promise<void> {
  const worker = subStoreBackendWorker
  subStoreBackendWorker = undefined as unknown as Worker
  if (worker) {
    await worker.terminate()
  }
}

export async function downloadSubStore(): Promise<void> {
  const { 'mixed-port': mixedPort = 7890 } = await getControledMihomoConfig()
  const frontendDir = subStoreFrontendDir()
  const backendPath = subStoreBackendPath()
  const tempDir = subStoreTempDir()

  try {
    const [backend, frontend] = await Promise.all([
      downloadReleaseAsset('sub-store-org/Sub-Store', 'sub-store.bundle.js', mixedPort),
      downloadReleaseAsset('sub-store-org/Sub-Store-Front-End', 'dist.zip', mixedPort)
    ])

    // 创建临时目录
    if (existsSync(tempDir)) {
      await rm(tempDir, { recursive: true })
    }
    mkdirSync(tempDir, { recursive: true })

    // 先解压到临时目录
    const zip = new AdmZip(frontend)
    zip.extractAllTo(tempDir, true)

    await writeFile(backendPath, backend)

    // 确保目标目录存在并清空
    if (existsSync(frontendDir)) {
      await rm(frontendDir, { recursive: true })
    }
    mkdirSync(frontendDir, { recursive: true })

    // 将 dist 目录中的内容移动到目标目录
    await cp(path.join(tempDir, 'dist'), frontendDir, { recursive: true })

    // 清理临时目录
    await rm(tempDir, { recursive: true })
  } catch (error) {
    await appendAppLog(`[SubStore]: 下载 Sub-Store 文件失败，${error}\n`).catch(() => {})
    throw error
  }
}
