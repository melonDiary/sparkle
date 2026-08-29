import { ipcMain } from 'electron'

type IpcHandler = Parameters<typeof ipcMain.handle>[1]

const registeredChannels = new Set<string>()

export function registerIpcHandler(channel: string, handler: IpcHandler): void {
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
