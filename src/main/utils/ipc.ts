import { registerMihomoIpc } from './ipc-mihomo'
import { registerProfileIpc } from './ipc-profile'
import { registerAppIpc } from './ipc-app'
import { registerSubStoreIpc } from './ipc-substore'
import { registerWindowIpc } from './ipc-window'

export function registerIpcMainHandlers(): void {
  registerMihomoIpc()
  registerProfileIpc()
  registerAppIpc()
  registerSubStoreIpc()
  registerWindowIpc()
}
