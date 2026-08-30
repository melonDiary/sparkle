import { ipcMain } from 'electron'
import { IPC_CHANNELS, type KnownIpcChannel, type TypedIpcContract } from '../../shared/ipc'

type IpcHandler = Parameters<typeof ipcMain.handle>[1]
type TypedHandler<C extends KnownIpcChannel> = (
  event: Electron.IpcMainInvokeEvent,
  ...args: TypedIpcContract[C]['args']
) => TypedIpcContract[C]['result'] | Promise<TypedIpcContract[C]['result']>

const registeredChannels = new Set<IpcChannel>()

export function registerIpcHandler<C extends KnownIpcChannel>(
  channel: C,
  handler: TypedHandler<C>
): void
export function registerIpcHandler(channel: IpcChannel, handler: IpcHandler): void
export function registerIpcHandler(
  channel: IpcChannel,
  handler: IpcHandler | TypedHandler<KnownIpcChannel>
): void {
  if (registeredChannels.has(channel)) {
    ipcMain.removeHandler(channel)
  }
  ipcMain.handle(channel, handler)
  registeredChannels.add(channel)
}

export function unregisterIpcHandlers(): void {
  for (const channel of registeredChannels) {
    ipcMain.removeHandler(channel)
  }
  registeredChannels.clear()
}

/** Fail fast if the runtime registry and shared channel contract drift apart. */
export function assertIpcChannelsRegistered(): void {
  const missing = IPC_CHANNELS.filter((channel) => !registeredChannels.has(channel as IpcChannel))
  if (missing.length > 0) {
    throw new Error(`Missing IPC handlers: ${missing.join(', ')}`)
  }
}
