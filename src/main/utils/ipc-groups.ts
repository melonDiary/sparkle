import { ipcMain } from 'electron'
import { registerIpcHandler as registerHandler } from './ipc-registration'

export type IpcRegistration = (register: typeof registerHandler) => void

export function registerIpcGroup(registrations: IpcRegistration[]): void {
  for (const registration of registrations) {
    registration(registerHandler)
  }
}

export function registerIpcHandler(
  channel: string,
  handler: Parameters<typeof ipcMain.handle>[1]
): void {
  registerHandler(channel, handler)
}
