import { Toast } from '@heroui-v3/react'
import { useEffect } from 'react'
import { dismissToastNotification, showToastNotification } from '@renderer/utils/notification'
import { IPC_EVENTS } from '../../../../shared/ipc'

const maxVisibleAppNotifications = 10

const AppNotificationProvider: React.FC = () => {
  useEffect(() => {
    const handleNotification = (
      _event: Electron.IpcRendererEvent,
      payload: Parameters<typeof showToastNotification>[0]
    ): void => {
      showToastNotification(payload)
    }
    const handleNotificationDismiss = (_event: Electron.IpcRendererEvent, id: string): void => {
      dismissToastNotification(id)
    }

    window.electron.ipcRenderer.on(IPC_EVENTS.APP_NOTIFICATION, handleNotification)
    window.electron.ipcRenderer.on(IPC_EVENTS.APP_NOTIFICATION_DISMISS, handleNotificationDismiss)
    window.electron.ipcRenderer.send(IPC_EVENTS.APP_NOTIFICATION_READY)
    return (): void => {
      window.electron.ipcRenderer.removeAllListeners(IPC_EVENTS.APP_NOTIFICATION)
      window.electron.ipcRenderer.removeAllListeners(IPC_EVENTS.APP_NOTIFICATION_DISMISS)
    }
  }, [])

  return (
    <Toast.Provider
      className="app-nodrag top-14 right-4"
      maxVisibleToasts={maxVisibleAppNotifications}
      placement="top end"
    />
  )
}

export default AppNotificationProvider
