import { IpcRendererEvent, webUtils } from 'electron'

declare global {
  interface Window {
    electron: {
      ipcRenderer: {
        // The channel is type-safe; result typing remains defined by each renderer wrapper.
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        invoke: (channel: IpcChannel, ...args: unknown[]) => Promise<any>
        on: (
          channel: IpcEventName,
          listener: (event: IpcRendererEvent, ...args: any[]) => void
        ) => () => void
        send: (channel: IpcEventName, ...args: unknown[]) => void
        removeAllListeners: (channel: IpcEventName) => void
      }
    }
    api: { webUtils: typeof webUtils; platform: NodeJS.Platform }
  }
}
export {}
