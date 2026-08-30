import { is } from '@electron-toolkit/utils'
import { startCore } from '../core/manager'
import { showNotification } from '../utils/notification'
import { initProfileUpdater } from '../core/profileUpdater'
import { startMonitor } from '../resolve/trafficMonitor'
import { initShortcut } from '../resolve/shortcut'
import { showFloatingWindow } from '../resolve/floatingWindow'
import { createTray } from '../resolve/tray'

export interface StartupTasksContext {
  initialWindowDisplayPromise: Promise<void>
  createWindowPromise: Promise<void>
  showFloating: boolean
  disableTray: boolean
  runStartupTask: (name: string, task: Promise<unknown>) => void
  onCoreStarted: () => void
}

export async function runStartupTasks(context: StartupTasksContext): Promise<void> {
  let coreStarted = false

  const coreStartPromise = (async (): Promise<void> => {
    try {
      if (is.dev) {
        await context.initialWindowDisplayPromise
      }
      const [startPromise] = await startCore()
      startPromise.then(async () => {
        await initProfileUpdater()
      })
      coreStarted = true
    } catch (error) {
      void showNotification({ title: '内核启动出错', body: `${error}`, variant: 'danger' })
    }
  })()

  context.runStartupTask('traffic monitor', startMonitor())
  await context.createWindowPromise

  const uiTasks: Promise<void>[] = [initShortcut()]
  if (context.showFloating) uiTasks.push(Promise.resolve(showFloatingWindow()))
  if (!context.disableTray) uiTasks.push(createTray())
  context.runStartupTask('ui extras', Promise.all(uiTasks))

  await coreStartPromise
  if (coreStarted) context.onCoreStarted()
}
