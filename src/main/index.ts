import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import { registerIpcMainHandlers } from './utils/ipc'
import { app, shell, BrowserWindow, Menu, type IpcMainEvent } from 'electron'
import { getAppConfig } from './config'
import { quitWithoutCore, stopCore } from './core/manager'
import { stopNetworkDetection } from './core/network'
import { disableSysProxySync, triggerSysProxy } from './sys/sysproxy'
import icon from '../../resources/icon.png?asset'
import { createApplicationMenu } from './resolve/menu'
import { init } from './utils/init'
import { join } from 'path'
import { runStartupTasks } from './bootstrap/startup-tasks'
import { getAppConfigSync } from './config/app'
import { createMainWindowStateManager } from './resolve/windowState'
import { createWindowController } from './resolve/window-controller'
import { isHttpUrl } from './utils/url'
import {
  applyWindowsGpuWorkaround,
  ensureWindowsElevatedStartup,
  useLinuxCustomRelaunch
} from './sys/startup'
import { handleDeepLink } from './resolve/deepLink'
import { acquireSingleInstance } from './bootstrap/single-instance'
import { initDeepLinkWiring } from './bootstrap/deep-link-wiring'
import { initAppQuitLifecycle } from './resolve/appLifecycle'
import { showNotification } from './utils/notification'
import { appendAppLog } from './utils/log'
import { IPC_EVENTS } from '../shared/ipc'

export { setNotQuitDialog } from './resolve/appLifecycle'

let quitTimeout: NodeJS.Timeout | null = null
export let mainWindow: BrowserWindow | null = null
let isCreatingWindow = false
let windowShown = false
let createWindowPromiseResolve: (() => void) | null = null
let createWindowPromise: Promise<void> | null = null

const windowController = createWindowController({
  create: async () => {
    await createWindow()
    if (!mainWindow) throw new Error('主窗口创建失败')
    return mainWindow
  }
})
let initialWindowDisplayPromiseResolve: (() => void) | null = null
const initialWindowDisplayPromise = new Promise<void>((resolve) => {
  initialWindowDisplayPromiseResolve = resolve
})

function waitForInitialContent(window: BrowserWindow): Promise<void> {
  return new Promise((resolve) => {
    const { webContents } = window
    let finished = false
    const finish = (): void => {
      if (finished) return
      finished = true
      clearTimeout(timeout)
      webContents.off('ipc-message', onIpcMessage)
      window.off('closed', finish)
      resolve()
    }
    const onIpcMessage = (_event: IpcMainEvent, channel: string): void => {
      if (channel === 'renderer-content-ready') finish()
    }
    const timeout = setTimeout(finish, 5000)
    webContents.on('ipc-message', onIpcMessage)
    window.once('closed', finish)
  })
}

async function scheduleLightweightMode(): Promise<void> {
  const {
    autoLightweight = false,
    autoLightweightDelay = 60,
    autoLightweightMode = 'core'
  } = await getAppConfig()

  if (!autoLightweight) return

  if (quitTimeout) {
    clearTimeout(quitTimeout)
  }

  const enterLightweightMode = async (): Promise<void> => {
    if (autoLightweightMode === 'core') {
      await quitWithoutCore()
    } else if (autoLightweightMode === 'tray') {
      if (mainWindow && !mainWindow.isVisible()) {
        mainWindow.destroy()
        if (process.platform === 'darwin' && app.dock) {
          app.dock.hide()
        }
      }
    }
  }

  quitTimeout = setTimeout(enterLightweightMode, autoLightweightDelay * 1000)
}

const syncConfig = getAppConfigSync()

function exitApp(): void {
  disableSysProxySync()
  app.exit()
}

function clearLightweightTimeout(): void {
  if (quitTimeout) {
    clearTimeout(quitTimeout)
    quitTimeout = null
  }
}

function runStartupTask(name: string, task: Promise<unknown>): void {
  task.catch((error) => {
    appendAppLog(`[App]: startup task ${name} failed, ${error}\n`).catch(() => {})
  })
}

ensureWindowsElevatedStartup(syncConfig.corePermissionMode, exitApp)

const gotTheLock = acquireSingleInstance({
  showMainWindow,
  handleDeepLink: (url) => handleDeepLink(url, { getMainWindow: () => mainWindow, createWindow, showWindow })
})

useLinuxCustomRelaunch()
applyWindowsGpuWorkaround()

if (!gotTheLock) {
  // The process is already quitting after a failed single-instance lock.
}

const initPromise = init()

if (syncConfig.disableGPU) {
  app.disableHardwareAcceleration()
}

const handleIncomingDeepLink = (url: string): Promise<void> =>
  handleDeepLink(url, { getMainWindow: () => mainWindow, createWindow, showWindow })

initDeepLinkWiring({ showMainWindow, handleDeepLink: handleIncomingDeepLink })

function showWindow(): number {
  if (mainWindow) {
    if (mainWindow.isMinimized()) {
      mainWindow.restore()
    } else if (!mainWindow.isVisible()) {
      mainWindow.show()
    }
    mainWindow.focusOnWebView()
    mainWindow.setAlwaysOnTop(true, 'pop-up-menu')
    mainWindow.focus()
    mainWindow.setAlwaysOnTop(false)

    if (!mainWindow.isMinimized()) {
      return 100
    }
  }
  return 500
}

initAppQuitLifecycle({
  getMainWindow: () => mainWindow,
  showWindow,
  clearLightweightTimeout,
  exitApp
})

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(async () => {
  // Set app user model id for windows
  electronApp.setAppUserModelId('sparkle.app')
  let appConfig: AppConfig
  try {
    appConfig = await initPromise
  } catch (e) {
    void showNotification({ title: '应用初始化失败', body: `${e}`, variant: 'danger' })
    app.quit()
    return
  }

  // Default open or close DevTools by F12 in development
  // and ignore CommandOrControl + R in production.
  // see https://github.com/alex8088/electron-toolkit/tree/master/packages/utils
  app.on('browser-window-created', (_, window) => {
    optimizer.watchWindowShortcuts(window)
  })
  const { showFloatingWindow: showFloating = false, disableTray = false } = appConfig
  registerIpcMainHandlers({
    window: {
      getMainWindow: () => mainWindow,
      showMainWindow,
      closeMainWindow,
      triggerMainWindow
    }
  })

  const createWindowPromise = createWindow(appConfig)
  await runStartupTasks({
    initialWindowDisplayPromise,
    createWindowPromise,
    showFloating,
    disableTray,
    runStartupTask,
    onCoreStarted: () => mainWindow?.webContents.send(IPC_EVENTS.CORE_STARTED)
  })

  app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    showMainWindow()
  })
})

export async function createWindow(appConfig?: AppConfig): Promise<void> {
  if (isCreatingWindow) {
    if (createWindowPromise) {
      await createWindowPromise
    }
    return
  }
  isCreatingWindow = true
  createWindowPromise = new Promise<void>((resolve) => {
    createWindowPromiseResolve = resolve
  })
  try {
    const config = appConfig ?? (await getAppConfig())
    const { useWindowFrame = false, enableWindowDrag = false, silentStart = false } = config
    const useNativeWindowFrame = useWindowFrame && !enableWindowDrag
    const [windowStateManager] = await Promise.all([
      Promise.resolve(createMainWindowStateManager()),
      process.platform === 'darwin'
        ? createApplicationMenu()
        : Promise.resolve(Menu.setApplicationMenu(null))
    ])
    const windowState = windowStateManager.state
    mainWindow = new BrowserWindow({
      minWidth: 800,
      minHeight: 600,
      width: windowState.width,
      height: windowState.height,
      x: windowState.x,
      y: windowState.y,
      show: false,
      frame: useNativeWindowFrame,
      fullscreenable: false,
      titleBarStyle: useNativeWindowFrame ? 'default' : 'hidden',
      titleBarOverlay: useWindowFrame
        ? false
        : {
            height: 49
          },
      autoHideMenuBar: true,
      ...(process.platform === 'linux' ? { icon: icon } : {}),
      webPreferences: {
        preload: join(__dirname, '../preload/index.js'),
        spellcheck: false,
        sandbox: false,
        ...(is.dev ? { webSecurity: false } : {})
      }
    })
    windowStateManager.attach(mainWindow)
    const initialContentPromise = waitForInitialContent(mainWindow)
    mainWindow.webContents.on('did-fail-load', () => {
      mainWindow?.webContents.reload()
    })

    mainWindow.on('close', async (event) => {
      event.preventDefault()
      mainWindow?.hide()
      if (windowShown) {
        await scheduleLightweightMode()
      }
    })

    mainWindow.on('closed', () => {
      mainWindow = null
    })

    mainWindow.on('session-end', async () => {
      stopNetworkDetection()
      disableSysProxySync(true)
      await triggerSysProxy(false, false, true)
      await stopCore()
    })

    mainWindow.webContents.setWindowOpenHandler((details) => {
      if (isHttpUrl(details.url)) {
        void shell.openExternal(details.url)
      }
      return { action: 'deny' }
    })
    // HMR for renderer base on electron-vite cli.
    // Load the remote URL for development or the local html file for production.
    if (is.dev && process.env['ELECTRON_RENDERER_URL']) {
      void mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
    } else {
      void mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
    }
    await initialContentPromise
    if (!mainWindow) return

    if (!silentStart) {
      clearLightweightTimeout()
      windowShown = true
      mainWindow.show()
      mainWindow.focusOnWebView()
    } else {
      await scheduleLightweightMode()
    }
    initialWindowDisplayPromiseResolve?.()
    initialWindowDisplayPromiseResolve = null
  } finally {
    isCreatingWindow = false
    if (createWindowPromiseResolve) {
      createWindowPromiseResolve()
      createWindowPromiseResolve = null
    }
    createWindowPromise = null
  }
}

export async function triggerMainWindow(): Promise<void> {
  await windowController.triggerWindow()
}

export async function showMainWindow(): Promise<void> {
  if (quitTimeout) {
    clearTimeout(quitTimeout)
  }
  if (process.platform === 'darwin' && app.dock) {
    const { useDockIcon = true } = await getAppConfig()
    if (!useDockIcon) {
      app.dock.hide()
    }
  }
  const window = await windowController.createWindow()
  windowShown = true
  window.show()
  window.focusOnWebView()
}

export function closeMainWindow(): void {
  windowController.closeWindow()
}
