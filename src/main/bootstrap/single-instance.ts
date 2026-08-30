import { app } from 'electron'

export interface SingleInstanceContext {
  showMainWindow: () => Promise<void>
  handleDeepLink: (url: string) => Promise<void>
}

export function acquireSingleInstance(context: SingleInstanceContext): boolean {
  const gotTheLock = app.requestSingleInstanceLock()
  if (!gotTheLock) {
    app.quit()
    return false
  }

  app.on('second-instance', async (_event, commandline) => {
    await context.showMainWindow()
    const url = commandline.at(-1)
    if (url) await context.handleDeepLink(url)
  })

  return true
}
