import React, { createContext, useContext, ReactNode, useEffect } from 'react'
import useSWR from 'swr'
import { notify } from '@renderer/utils/notification'
import { IPC_EVENTS } from '../../../shared/ipc'
import {
  getProfileConfig,
  setProfileConfig as set,
  addProfileItem as add,
  removeProfileItem as remove,
  updateProfileItem as update,
  changeCurrentProfile as change
} from '@renderer/utils/ipc'

interface ProfileConfigContextType {
  profileConfig: ProfileConfig | undefined
  setProfileConfig: (config: ProfileConfig) => Promise<void>
  mutateProfileConfig: () => void
  addProfileItem: (item: Partial<ProfileItem>) => Promise<void>
  updateProfileItem: (item: ProfileItem) => Promise<void>
  removeProfileItem: (id: string) => Promise<void>
  changeCurrentProfile: (id: string) => Promise<void>
}

const ProfileConfigContext = createContext<ProfileConfigContextType | undefined>(undefined)

export const ProfileConfigProvider: React.FC<{ children: ReactNode }> = ({ children }) => {
  const { data: profileConfig, mutate: mutateProfileConfig } = useSWR('getProfileConfig', () =>
    getProfileConfig()
  )

  const setProfileConfig = async (config: ProfileConfig): Promise<void> => {
    try {
      await set(config)
    } catch (e) {
      notify(e, { variant: 'danger' })
    } finally {
      mutateProfileConfig()
      window.electron.ipcRenderer.send(IPC_EVENTS.UPDATE_TRAY_MENU)
    }
  }

  const addProfileItem = async (item: Partial<ProfileItem>): Promise<void> => {
    try {
      await add(item)
    } catch (e) {
      notify(e, { variant: 'danger' })
    } finally {
      mutateProfileConfig()
      window.electron.ipcRenderer.send(IPC_EVENTS.UPDATE_TRAY_MENU)
    }
  }

  const removeProfileItem = async (id: string): Promise<void> => {
    try {
      await remove(id)
    } catch (e) {
      notify(e, { variant: 'danger' })
    } finally {
      mutateProfileConfig()
      window.electron.ipcRenderer.send(IPC_EVENTS.UPDATE_TRAY_MENU)
    }
  }

  const updateProfileItem = async (item: ProfileItem): Promise<void> => {
    try {
      await update(item)
    } catch (e) {
      notify(e, { variant: 'danger' })
    } finally {
      mutateProfileConfig()
      window.electron.ipcRenderer.send(IPC_EVENTS.UPDATE_TRAY_MENU)
    }
  }

  const changeCurrentProfile = async (id: string): Promise<void> => {
    try {
      await change(id)
    } catch (e) {
      notify(e, { variant: 'danger' })
    } finally {
      mutateProfileConfig()
      window.electron.ipcRenderer.send(IPC_EVENTS.UPDATE_TRAY_MENU)
    }
  }

  useEffect(() => {
    window.electron.ipcRenderer.on(IPC_EVENTS.PROFILE_CONFIG_UPDATED, () => {
      mutateProfileConfig()
    })
    return (): void => {
      window.electron.ipcRenderer.removeAllListeners(IPC_EVENTS.PROFILE_CONFIG_UPDATED)
    }
  }, [])

  return (
    <ProfileConfigContext.Provider
      value={{
        profileConfig,
        setProfileConfig,
        mutateProfileConfig,
        addProfileItem,
        removeProfileItem,
        updateProfileItem,
        changeCurrentProfile
      }}
    >
      {children}
    </ProfileConfigContext.Provider>
  )
}

export const useProfileConfig = (): ProfileConfigContextType => {
  const context = useContext(ProfileConfigContext)
  if (context === undefined) {
    throw new Error('useProfileConfig must be used within a ProfileConfigProvider')
  }
  return context
}
