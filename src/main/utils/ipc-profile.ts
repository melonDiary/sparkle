import { registerIpcHandler } from './ipc-registration'
import { ipcErrorWrapper } from './ipc-error'
import {
  addOverrideItem,
  addProfileItem,
  changeCurrentProfile,
  getCurrentProfileItem,
  getFilePreviewStr,
  getFileStr,
  getOverride,
  getOverrideConfig,
  getOverrideItem,
  getProfileConfig,
  getProfileItem,
  getProfileStr,
  removeOverrideItem,
  removeProfileItem,
  saveFileStrWithElevation,
  setFileStr,
  setOverride,
  setOverrideConfig,
  setProfileConfig,
  setProfileStr,
  updateOverrideItem,
  updateProfileItem
} from '../config'

export function registerProfileIpc(): void {
  const r = registerIpcHandler
  r('getProfileConfig', (_e, force) => ipcErrorWrapper(getProfileConfig)(force))
  r('setProfileConfig', (_e, config) => ipcErrorWrapper(setProfileConfig)(config))
  r('getCurrentProfileItem', ipcErrorWrapper(getCurrentProfileItem))
  r('getProfileItem', (_e, id) => ipcErrorWrapper(getProfileItem)(id))
  r('getProfileStr', (_e, id) => ipcErrorWrapper(getProfileStr)(id))
  r('getFileStr', (_e, path, ageSecretKey) => ipcErrorWrapper(getFileStr)(path, ageSecretKey))
  r('getFilePreviewStr', (_e, path, format) => ipcErrorWrapper(getFilePreviewStr)(path, format))
  r('setFileStr', (_e, path, str) => ipcErrorWrapper(setFileStr)(path, str))
  r('saveFileStrWithElevation', (_e, path, str) =>
    ipcErrorWrapper(saveFileStrWithElevation)(path, str)
  )
  r('setProfileStr', (_e, id, str) => ipcErrorWrapper(setProfileStr)(id, str))
  r('updateProfileItem', (_e, item) => ipcErrorWrapper(updateProfileItem)(item))
  r('changeCurrentProfile', (_e, id) => ipcErrorWrapper(changeCurrentProfile)(id))
  r('addProfileItem', (_e, item) => ipcErrorWrapper(addProfileItem)(item))
  r('removeProfileItem', (_e, id) => ipcErrorWrapper(removeProfileItem)(id))

  r('getOverrideConfig', (_e, force) => ipcErrorWrapper(getOverrideConfig)(force))
  r('setOverrideConfig', (_e, config) => ipcErrorWrapper(setOverrideConfig)(config))
  r('getOverrideItem', (_e, id) => ipcErrorWrapper(getOverrideItem)(id))
  r('addOverrideItem', (_e, item) => ipcErrorWrapper(addOverrideItem)(item))
  r('removeOverrideItem', (_e, id) => ipcErrorWrapper(removeOverrideItem)(id))
  r('updateOverrideItem', (_e, item) => ipcErrorWrapper(updateOverrideItem)(item))
  r('getOverride', (_e, id, ext) => ipcErrorWrapper(getOverride)(id, ext))
  r('setOverride', (_e, id, ext, str) => ipcErrorWrapper(setOverride)(id, ext, str))
}
