import { IpcRendererEvent, webUtils } from 'electron'
import type { IpcArgs, IpcChannelName, IpcEventArgs, IpcEventName, IpcResult } from '../shared/ipc'

declare global {
  interface Window {
    electron: {
      ipcRenderer: {
        // The channel is type-safe; result typing remains defined by each renderer wrapper.
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        invoke: <C extends IpcChannelName>(channel: C, ...args: IpcArgs<C>) => Promise<IpcResult<C>>
        on: (
          channel: IpcEventName,
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
