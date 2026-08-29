import { registerIpcHandler } from './ipc-registration'
import { ipcErrorWrapper } from './ipc-error'
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

export function registerSubStoreIpc(): void {
  const r = registerIpcHandler
  const w = ipcErrorWrapper
  r('startSubStoreFrontendServer', () => w(startSubStoreFrontendServer)())
  r('stopSubStoreFrontendServer', () => w(stopSubStoreFrontendServer)())
  r('startSubStoreBackendServer', () => w(startSubStoreBackendServer)())
  r('stopSubStoreBackendServer', () => w(stopSubStoreBackendServer)())
  r('downloadSubStore', () => w(downloadSubStore)())
  r('subStorePort', () => subStorePort)
  r('subStoreFrontendPort', () => subStoreFrontendPort)
  r('subStoreSubs', () => w(subStoreSubs)())
  r('subStoreCollections', () => w(subStoreCollections)())
}
