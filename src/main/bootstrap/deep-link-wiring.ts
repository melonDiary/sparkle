import { app } from 'electron'

export interface DeepLinkWiringContext {
  showMainWindow: () => Promise<void>
  handleDeepLink: (url: string) => Promise<void>
}

export function initDeepLinkWiring(context: DeepLinkWiringContext): void {
  app.on('open-url', async (event, url) => {
    event.preventDefault()
    await context.showMainWindow()
    await context.handleDeepLink(url)
  })
}
