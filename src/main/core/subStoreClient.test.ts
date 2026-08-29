import { beforeEach, describe, expect, it, vi } from 'vitest'

const { getAppConfig, appendAppLog, axiosGet } = vi.hoisted(() => ({
  getAppConfig: vi.fn(),
  appendAppLog: vi.fn().mockResolvedValue(undefined),
  axiosGet: vi.fn()
}))

vi.mock('../config', () => ({ getAppConfig }))
vi.mock('../resolve/server', () => ({ subStorePort: 38324 }))
vi.mock('../utils/log', () => ({ appendAppLog }))
vi.mock('axios', () => ({
  default: {
    get: axiosGet,
    isAxiosError: (error: unknown) => Boolean((error as { isAxiosError?: boolean })?.isAxiosError)
  }
}))

import { requestSubStore, waitForSubStoreReady } from './subStoreClient'

describe('subStoreClient', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    getAppConfig.mockResolvedValue({})
  })

  it('uses a custom Sub-Store URL and returns the API payload', async () => {
    getAppConfig.mockResolvedValue({ useCustomSubStore: true, customSubStoreUrl: 'http://store///' })
    axiosGet.mockResolvedValue({ data: { data: ['sub'] } })

    await expect(requestSubStore<{ id: string }[]>('api/subs')).resolves.toEqual(['sub'])
    expect(axiosGet).toHaveBeenCalledWith('http://store/api/subs', expect.objectContaining({ timeout: 30000 }))
  })

  it('rejects an empty custom URL', async () => {
    getAppConfig.mockResolvedValue({ useCustomSubStore: true, customSubStoreUrl: '  ' })

    await expect(requestSubStore('api/subs')).rejects.toThrow('自定义 Sub-Store 地址为空')
    expect(axiosGet).not.toHaveBeenCalled()
  })

  it('reports request failures and rethrows them', async () => {
    const error = new Error('network failure')
    axiosGet.mockRejectedValue(error)

    await expect(requestSubStore('api/subs')).rejects.toBe(error)
    expect(appendAppLog).toHaveBeenCalledWith(expect.stringContaining('request api/subs failed'))
  })

  it('waits until the backend becomes ready', async () => {
    axiosGet.mockRejectedValueOnce(new Error('not ready')).mockResolvedValueOnce({ data: {} })

    await expect(waitForSubStoreReady(38324, 1500)).resolves.toBeUndefined()
    expect(axiosGet).toHaveBeenCalledTimes(2)
  })
})
