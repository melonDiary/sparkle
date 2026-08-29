import { registerMihomoIpc } from './ipc-mihomo'
import { registerProfileIpc } from './ipc-profile'
import { registerAppIpc } from './ipc-app'
import { registerSubStoreIpc } from './ipc-substore'
import { registerWindowIpc, type WindowIpcDeps } from './ipc-window'
import { assertIpcChannelsRegistered } from './ipc-registration'

export function registerIpcMainHandlers(deps: { window: WindowIpcDeps }): void {
  registerMihomoIpc()
  registerProfileIpc()
  registerAppIpc()
  registerSubStoreIpc()
  registerWindowIpc(deps.window)
  assertIpcChannelsRegistered()
}
