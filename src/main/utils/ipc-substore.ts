import {
  downloadSubStore,
  startSubStoreBackendServer,
  startSubStoreFrontendServer,
  stopSubStoreBackendServer,
  stopSubStoreFrontendServer,
  subStoreFrontendPort,
  subStorePort
} from '../resolve/server'
import { subStoreCollections, subStoreSubs } from '../core/subStoreApi'
import { registerIpcHandler } from './ipc-registration'

type ErrorResult = { invokeError: unknown }
type Wrapped<T> = Promise<T | ErrorResult>
type Wrapper = <T>(fn: (...args: any[]) => T | Promise<T>) => (...args: any[]) => Wrapped<T> // eslint-disable-line @typescript-eslint/no-explicit-any

export function registerSubStoreIpc(ipcErrorWrapper: Wrapper): void {
  registerIpcHandler('startSubStoreFrontendServer', () => ipcErrorWrapper(startSubStoreFrontendServer)())
  registerIpcHandler('stopSubStoreFrontendServer', () => ipcErrorWrapper(stopSubStoreFrontendServer)())
  registerIpcHandler('startSubStoreBackendServer', () => ipcErrorWrapper(startSubStoreBackendServer)())
  registerIpcHandler('stopSubStoreBackendServer', () => ipcErrorWrapper(stopSubStoreBackendServer)())
  registerIpcHandler('downloadSubStore', () => ipcErrorWrapper(downloadSubStore)())
  registerIpcHandler('subStorePort', () => subStorePort)
  registerIpcHandler('subStoreFrontendPort', () => subStoreFrontendPort)
  registerIpcHandler('subStoreSubs', () => ipcErrorWrapper(subStoreSubs)())
  registerIpcHandler('subStoreCollections', () => ipcErrorWrapper(subStoreCollections)())
}
