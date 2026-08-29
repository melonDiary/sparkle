import axios, { type AxiosRequestConfig, type AxiosResponse } from 'axios'
import http from 'http'
import https from 'https'
import tls from 'tls'
import { getCertFingerprint } from '../config/profile'
import { DOWNLOAD_TIMEOUT, describeHttpError, localhostProxy } from './http'

export type RemoteRequestOptions = {
  url: string
  fingerprint?: string
  proxyPort?: number
  userAgent?: string
  timeout?: number
}

function createFingerprintVerifier(fingerprint: string): (socket: tls.TLSSocket) => void {
  const expected = fingerprint.replace(/:/g, '').toUpperCase()
  return (socket) => {
    if (getCertFingerprint(socket.getPeerCertificate()) !== expected) {
      socket.destroy(new Error('证书指纹不匹配'))
    }
  }
}

function createHttpsAgent(fingerprint: string | undefined, proxyPort: number | undefined): https.Agent {
  const agent = new https.Agent({ rejectUnauthorized: !fingerprint })
  if (!fingerprint) return agent

  const verify = createFingerprintVerifier(fingerprint)
  if (!proxyPort) {
    const createConnection = agent.createConnection.bind(agent)
    agent.createConnection = (options, callback) => {
      const socket = createConnection(options, callback)
      socket?.once('secureConnect', function (this: tls.TLSSocket) {
        verify(this)
      })
      return socket
    }
    return agent
  }

  agent.createConnection = (options, callback) => {
    const target = options as { hostname?: string; host?: string; port?: string | number }
    const hostname = target.hostname || target.host || ''
    const port = target.port || 443
    const request = http.request({
      host: '127.0.0.1',
      port: proxyPort,
      method: 'CONNECT',
      path: `${hostname}:${port}`
    })
    request.once('connect', (response, socket, head) => {
      if (response.statusCode !== 200) {
        callback?.(new Error(`代理连接失败，状态码：${response.statusCode}`), null!)
        return
      }
      if (head.length > 0) socket.unshift(head)
      const secureSocket = tls.connect(
        { socket, servername: hostname, rejectUnauthorized: false },
        () => verify(secureSocket)
      )
      callback?.(null, secureSocket)
    })
    request.once('error', (error) => callback?.(error, null!))
    request.end()
    return null!
  }
  return agent
}

export async function requestRemoteText(
  options: RemoteRequestOptions
): Promise<AxiosResponse<string>> {
  const { url, fingerprint, proxyPort, userAgent, timeout = DOWNLOAD_TIMEOUT } = options
  const config: AxiosRequestConfig = {
    responseType: 'text',
    timeout,
    headers: { 'User-Agent': userAgent },
    httpsAgent: createHttpsAgent(fingerprint, fingerprint ? proxyPort : undefined),
    ...(proxyPort && !fingerprint ? { proxy: localhostProxy(proxyPort) } : {})
  }
  try {
    return await axios.get<string>(url, config)
  } catch (error) {
    throw new Error(`请求远程文件失败：${describeHttpError(error)}：${url}`)
  }
}
