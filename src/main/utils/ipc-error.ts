export type IpcErrorWrapper = <T>(
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  fn: (...args: any[]) => T | Promise<T>
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
) => (...args: any[]) => Promise<T | { invokeError: unknown }>

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export const ipcErrorWrapper: IpcErrorWrapper = (fn: (...args: any[]) => any) => {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  return async (...args: any[]) => {
    try {
      return await fn(...args)
    } catch (e) {
      if (e && typeof e === 'object') {
        if ('message' in e) {
          return { invokeError: e.message }
        } else {
          return { invokeError: JSON.stringify(e) }
        }
      }
      if (e instanceof Error || typeof e === 'string') {
        return { invokeError: e }
      }
      return { invokeError: 'Unknown Error' }
    }
  }
}
