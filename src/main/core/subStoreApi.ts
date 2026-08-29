import axios from 'axios'
import { subStorePort } from '../resolve/server'
import { getAppConfig } from '../config'
import { appendAppLog } from '../utils/log'

export async function subStoreSubs(): Promise<SubStoreSub[]> {
  const { useCustomSubStore = false, customSubStoreUrl = '' } = await getAppConfig()
  const baseUrl = useCustomSubStore ? customSubStoreUrl : `http://127.0.0.1:${subStorePort}`
  try {
    const res = await axios.get(`${baseUrl.replace(/\/$/, '')}/api/subs`, {
      responseType: 'json',
      timeout: 30000,
      validateStatus: (status) => status >= 200 && status < 300
    })
    return res.data.data as SubStoreSub[]
  } catch (error) {
    await appendAppLog(`[SubStore]: load subscriptions failed, ${error}\n`).catch(() => {})
    throw error
  }
}

export async function subStoreCollections(): Promise<SubStoreSub[]> {
  const { useCustomSubStore = false, customSubStoreUrl = '' } = await getAppConfig()
  const baseUrl = useCustomSubStore ? customSubStoreUrl : `http://127.0.0.1:${subStorePort}`
  try {
    const res = await axios.get(`${baseUrl.replace(/\/$/, '')}/api/collections`, {
      responseType: 'json',
      timeout: 30000,
      validateStatus: (status) => status >= 200 && status < 300
    })
    return res.data.data as SubStoreSub[]
  } catch (error) {
    await appendAppLog(`[SubStore]: load collections failed, ${error}\n`).catch(() => {})
    throw error
  }
}
