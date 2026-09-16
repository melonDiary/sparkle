import { IpcRendererEvent, webUtils } from 'electron'
import type { IpcArgs, IpcChannelName, IpcEventArgs, IpcEventName, IpcResult } from '../shared/ipc'

declare global {
  interface Window {
    electron: {
      ipcRenderer: {
        // The channel is type-safe; result typing remains defined by each renderer wrapper.
        invoke: <C extends IpcChannelName>(channel: C, ...args: IpcArgs<C>) => Promise<IpcResult<C>>
        on: (
          channel: IpcEventName,
          // Electron mirrors this listener signature, so forwarded args stay untyped.
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          listener: (event: IpcRendererEvent, ...args: any[]) => void
        ) => () => void
        send: <E extends IpcEventName>(channel: E, ...args: IpcEventArgs<E>) => void
        removeAllListeners: (channel: IpcEventName) => void
      }
    }
    api: { webUtils: typeof webUtils; platform: NodeJS.Platform }
  }
}
export {}
