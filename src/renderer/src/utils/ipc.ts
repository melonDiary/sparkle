import { TitleBarOverlayOptions } from 'electron'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function ipcErrorWrapper<T>(response: T): T {
  if (typeof response === 'object' && response !== null && 'invokeError' in response) {
    throw response.invokeError
  }
  return response
}

async function invoke<T>(channel: IpcChannel, ...args: unknown[]): Promise<T> {
  return ipcErrorWrapper(await window.electron.ipcRenderer.invoke(channel, ...args)) as T
}

export function mihomoVersion(): Promise<ControllerVersion> {
  return invoke<ControllerVersion>('mihomoVersion')
}

export function mihomoConfig(): Promise<ControllerConfigs> {
  return invoke<ControllerConfigs>('mihomoConfig')
}

export function mihomoCloseConnection(id: string): Promise<void> {
  return invoke<void>('mihomoCloseConnection', id)
}

export function mihomoCloseConnections(name?: string): Promise<void> {
  return invoke<void>('mihomoCloseConnections', name)
}

export function mihomoRules(): Promise<ControllerRules> {
  return invoke<ControllerRules>('mihomoRules')
}

export function mihomoProxies(): Promise<ControllerProxies> {
  return invoke<ControllerProxies>('mihomoProxies')
}

export function mihomoGroups(): Promise<ControllerMixedGroup[]> {
  return invoke<ControllerMixedGroup[]>('mihomoGroups')
}

export function mihomoProxyProviders(): Promise<ControllerProxyProviders> {
  return invoke<ControllerProxyProviders>('mihomoProxyProviders')
}

export function mihomoUpdateProxyProviders(name: string): Promise<void> {
  return invoke<void>('mihomoUpdateProxyProviders', name)
}

export function mihomoRuleProviders(): Promise<ControllerRuleProviders> {
  return invoke<ControllerRuleProviders>('mihomoRuleProviders')
}

export function mihomoUpdateRuleProviders(name: string): Promise<void> {
  return invoke<void>('mihomoUpdateRuleProviders', name)
}

export function mihomoChangeProxy(group: string, proxy: string): Promise<ControllerProxiesDetail> {
  return invoke<ControllerProxiesDetail>('mihomoChangeProxy', group, proxy)
}

export function mihomoUnfixedProxy(group: string): Promise<ControllerProxiesDetail> {
  return invoke<ControllerProxiesDetail>('mihomoUnfixedProxy', group)
}

export function mihomoUpgradeGeo(): Promise<void> {
  return invoke<void>('mihomoUpgradeGeo')
}

export function mihomoUpgradeUI(): Promise<void> {
  return invoke<void>('mihomoUpgradeUI')
}

export function mihomoUpgrade(channel: string): Promise<void> {
  return invoke<void>('mihomoUpgrade', channel)
}

export function mihomoProxyDelay(
  proxy: string,
  url?: string,
  provider?: string
): Promise<ControllerProxiesDelay> {
  return invoke<ControllerProxiesDelay>('mihomoProxyDelay', proxy, url, provider)
}

export function mihomoGroupDelay(group: string, url?: string): Promise<ControllerGroupDelay> {
  return invoke<ControllerGroupDelay>('mihomoGroupDelay', group, url)
}

export function mihomoRulesDisable(rules: Record<string, boolean>): Promise<void> {
  return invoke<void>('mihomoRulesDisable', rules)
}

export function patchMihomoConfig(patch: Partial<MihomoConfig>): Promise<void> {
  return invoke<void>('patchMihomoConfig', patch)
}

export function restartMihomoLogs(): Promise<void> {
  return invoke<void>('restartMihomoLogs')
}

export function checkAutoRun(): Promise<boolean> {
  return invoke<boolean>('checkAutoRun')
}

export function enableAutoRun(): Promise<void> {
  return invoke<void>('enableAutoRun')
}

export function disableAutoRun(): Promise<void> {
  return invoke<void>('disableAutoRun')
}

export function getAppConfig(force = false): Promise<AppConfig> {
  return invoke<AppConfig>('getAppConfig', force)
}

export function getCachedMihomoLogs(): Promise<
  Array<ControllerLog & { id?: string; seq?: number }>
> {
  return invoke<Array<ControllerLog & { id?: string; seq?: number }>>('getCachedMihomoLogs')
}

export function clearCachedMihomoLogs(): Promise<void> {
  return invoke<void>('clearCachedMihomoLogs')
}

export function patchAppConfig(patch: Partial<AppConfig>): Promise<AppConfig> {
  return invoke<AppConfig>('patchAppConfig', patch)
}

export function getControledMihomoConfig(force = false): Promise<Partial<MihomoConfig>> {
  return invoke<Partial<MihomoConfig>>('getControledMihomoConfig', force)
}

export function patchControledMihomoConfig(patch: Partial<MihomoConfig>): Promise<void> {
  return invoke<void>('patchControledMihomoConfig', patch)
}

export function getProfileConfig(force = false): Promise<ProfileConfig> {
  return invoke<ProfileConfig>('getProfileConfig', force)
}

export function setProfileConfig(config: ProfileConfig): Promise<void> {
  return invoke<void>('setProfileConfig', config)
}

export function getCurrentProfileItem(): Promise<ProfileItem> {
  return invoke<ProfileItem>('getCurrentProfileItem')
}

export function getProfileItem(id: string | undefined): Promise<ProfileItem> {
  return invoke<ProfileItem>('getProfileItem', id)
}

export function changeCurrentProfile(id: string): Promise<void> {
  return invoke<void>('changeCurrentProfile', id)
}

export function addProfileItem(item: Partial<ProfileItem>): Promise<void> {
  return invoke<void>('addProfileItem', item)
}

export function removeProfileItem(id: string): Promise<void> {
  return invoke<void>('removeProfileItem', id)
}

export function updateProfileItem(item: ProfileItem): Promise<void> {
  return invoke<void>('updateProfileItem', item)
}

export function getProfileStr(id: string): Promise<string> {
  return invoke<string>('getProfileStr', id)
}

export function getFileStr(id: string, ageSecretKey?: string): Promise<string> {
  return invoke<string>('getFileStr', id, ageSecretKey)
}

export function getFilePreviewStr(id: string, format?: string): Promise<string> {
  return invoke<string>('getFilePreviewStr', id, format)
}

export function setFileStr(id: string, str: string): Promise<void> {
  return invoke<void>('setFileStr', id, str)
}

export function saveFileStrWithElevation(id: string, str: string): Promise<void> {
  return invoke<void>('saveFileStrWithElevation', id, str)
}

export function setProfileStr(id: string, str: string): Promise<void> {
  return invoke<void>('setProfileStr', id, str)
}

export function getOverrideConfig(force = false): Promise<OverrideConfig> {
  return invoke<OverrideConfig>('getOverrideConfig', force)
}

export function setOverrideConfig(config: OverrideConfig): Promise<void> {
  return invoke<void>('setOverrideConfig', config)
}

export function getOverrideItem(id: string): Promise<OverrideItem | undefined> {
  return invoke<OverrideItem | undefined>('getOverrideItem', id)
}

export function addOverrideItem(item: Partial<OverrideItem>): Promise<void> {
  return invoke<void>('addOverrideItem', item)
}

export function removeOverrideItem(id: string): Promise<void> {
  return invoke<void>('removeOverrideItem', id)
}

export function updateOverrideItem(item: OverrideItem): Promise<void> {
  return invoke<void>('updateOverrideItem', item)
}

export function getOverride(id: string, ext: 'js' | 'yaml' | 'log'): Promise<string> {
  return invoke<string>('getOverride', id, ext)
}

export function setOverride(id: string, ext: 'js' | 'yaml', str: string): Promise<void> {
  return invoke<void>('setOverride', id, ext, str)
}

export function restartCore(): Promise<void> {
  return invoke<void>('restartCore')
}

export function stopCore(): Promise<void> {
  return invoke<void>('stopCore')
}

export function restartMihomoConnections(): Promise<void> {
  return invoke<void>('restartMihomoConnections')
}

export function startMonitor(): Promise<void> {
  return invoke<void>('startMonitor')
}

export function triggerSysProxy(
  enable: boolean,
  onlyActiveDevice: boolean,
  useRegistry?: boolean
): Promise<void> {
  return invoke<void>('triggerSysProxy', enable, onlyActiveDevice, useRegistry)
}

export function manualGrantCorePermition(cores?: ('mihomo' | 'mihomo-alpha')[]): Promise<void> {
  return invoke<void>('manualGrantCorePermition', cores)
}

export function checkCorePermission(): Promise<{ mihomo: boolean; 'mihomo-alpha': boolean }> {
  return invoke<{ mihomo: boolean; 'mihomo-alpha': boolean }>('checkCorePermission')
}

export function checkElevateTask(): Promise<boolean> {
  return invoke<boolean>('checkElevateTask')
}

export function deleteElevateTask(): Promise<void> {
  return invoke<void>('deleteElevateTask')
}

export function revokeCorePermission(cores?: ('mihomo' | 'mihomo-alpha')[]): Promise<void> {
  return invoke<void>('revokeCorePermission', cores)
}

export function serviceStatus(): Promise<
  'running' | 'stopped' | 'not-installed' | 'paused' | 'unknown' | 'need-init'
> {
  return invoke<'running' | 'stopped' | 'not-installed' | 'paused' | 'unknown' | 'need-init'>(
    'serviceStatus'
  )
}

export function testServiceConnection(): Promise<boolean> {
  return invoke<boolean>('testServiceConnection')
}

export function initService(): Promise<void> {
  return invoke<void>('initService')
}

export function installService(): Promise<void> {
  return invoke<void>('installService')
}

export function uninstallService(): Promise<void> {
  return invoke<void>('uninstallService')
}

export function startService(): Promise<void> {
  return invoke<void>('startService')
}

export function restartService(): Promise<void> {
  return invoke<void>('restartService')
}

export function stopService(): Promise<void> {
  return invoke<void>('stopService')
}

export function findSystemMihomo(): Promise<string[]> {
  return invoke<string[]>('findSystemMihomo')
}

export function getFilePath(
  ext: string[],
  title?: string,
  filterName?: string
): Promise<string[] | undefined> {
  return invoke<string[] | undefined>('getFilePath', ext, title, filterName)
}

export function readTextFile(filePath: string): Promise<string> {
  return invoke<string>('readTextFile', filePath)
}

export function readImageFileDataURL(filePath: string): Promise<string> {
  return invoke<string>('readImageFileDataURL', filePath)
}

export function getRuntimeConfigStr(): Promise<string> {
  return invoke<string>('getRuntimeConfigStr')
}

export function getRawProfileStr(): Promise<string> {
  return invoke<string>('getRawProfileStr')
}

export function getCurrentProfileStr(): Promise<string> {
  return invoke<string>('getCurrentProfileStr')
}

export function getOverrideProfileStr(): Promise<string> {
  return invoke<string>('getOverrideProfileStr')
}

export function getRuntimeConfig(): Promise<MihomoConfig> {
  return invoke<MihomoConfig>('getRuntimeConfig')
}

export function checkUpdate(): Promise<AppVersion | undefined> {
  return invoke<AppVersion | undefined>('checkUpdate')
}

export function downloadAndInstallUpdate(version: string, tag?: string): Promise<void> {
  return invoke<void>('downloadAndInstallUpdate', version, tag)
}

export function cancelUpdate(): Promise<void> {
  return invoke<void>('cancelUpdate')
}

export function getVersion(): Promise<string> {
  return invoke<string>('getVersion')
}

export function getPlatform(): Promise<NodeJS.Platform> {
  return invoke<NodeJS.Platform>('platform')
}

export function openUWPTool(): Promise<void> {
  return invoke<void>('openUWPTool')
}

export function setupFirewall(): Promise<void> {
  return invoke<void>('setupFirewall')
}

export function getInterfaces(): Promise<Record<string, NetworkInterfaceInfo[]>> {
  return invoke<Record<string, NetworkInterfaceInfo[]>>('getInterfaces')
}

export function webdavBackup(): Promise<boolean> {
  return invoke<boolean>('webdavBackup')
}

export function webdavRestore(filename: string): Promise<void> {
  return invoke<void>('webdavRestore', filename)
}

export function listWebdavBackups(): Promise<string[]> {
  return invoke<string[]>('listWebdavBackups')
}

export function webdavDelete(filename: string): Promise<void> {
  return invoke<void>('webdavDelete', filename)
}

export function setTitleBarOverlay(overlay: TitleBarOverlayOptions): Promise<void> {
  return invoke<void>('setTitleBarOverlay', overlay)
}

export function setAlwaysOnTop(alwaysOnTop: boolean): Promise<void> {
  return invoke<void>('setAlwaysOnTop', alwaysOnTop)
}

export function isAlwaysOnTop(): Promise<boolean> {
  return invoke<boolean>('isAlwaysOnTop')
}

export function relaunchApp(): Promise<void> {
  return invoke<void>('relaunchApp')
}

export function quitWithoutCore(): Promise<void> {
  return invoke<void>('quitWithoutCore')
}

export function quitApp(): Promise<void> {
  return invoke<void>('quitApp')
}

export function notDialogQuit(): Promise<void> {
  return invoke<void>('notDialogQuit')
}

export function setNativeTheme(theme: 'system' | 'light' | 'dark'): Promise<void> {
  return invoke<void>('setNativeTheme', theme)
}

export function getGistUrl(): Promise<string> {
  return invoke<string>('getGistUrl')
}

export function startSubStoreFrontendServer(): Promise<void> {
  return invoke<void>('startSubStoreFrontendServer')
}

export function stopSubStoreFrontendServer(): Promise<void> {
  return invoke<void>('stopSubStoreFrontendServer')
}

export function startSubStoreBackendServer(): Promise<void> {
  return invoke<void>('startSubStoreBackendServer')
}

export function stopSubStoreBackendServer(): Promise<void> {
  return invoke<void>('stopSubStoreBackendServer')
}
export function downloadSubStore(): Promise<void> {
  return invoke<void>('downloadSubStore')
}

export function subStorePort(): Promise<number> {
  return invoke<number>('subStorePort')
}

export function subStoreFrontendPort(): Promise<number> {
  return invoke<number>('subStoreFrontendPort')
}

export function subStoreSubs(): Promise<SubStoreSub[]> {
  return invoke<SubStoreSub[]>('subStoreSubs')
}

export function subStoreCollections(): Promise<SubStoreSub[]> {
  return invoke<SubStoreSub[]>('subStoreCollections')
}

export function showTrayIcon(): Promise<void> {
  return invoke<void>('showTrayIcon')
}

export function closeTrayIcon(): Promise<void> {
  return invoke<void>('closeTrayIcon')
}

export function updateTrayIcon(): Promise<void> {
  return invoke<void>('updateTrayIcon')
}

export function setDockVisible(visible: boolean): Promise<void> {
  return invoke<void>('setDockVisible', visible)
}

export function showMainWindow(): Promise<void> {
  return invoke<void>('showMainWindow')
}

export function closeMainWindow(): Promise<void> {
  return invoke<void>('closeMainWindow')
}

export function triggerMainWindow(): Promise<void> {
  return invoke<void>('triggerMainWindow')
}

export function showFloatingWindow(): Promise<void> {
  return invoke<void>('showFloatingWindow')
}

export function closeFloatingWindow(): Promise<void> {
  return invoke<void>('closeFloatingWindow')
}

export function showContextMenu(): Promise<void> {
  return invoke<void>('showContextMenu')
}

export function openFile(
  type: 'profile' | 'override',
  id: string,
  ext?: 'yaml' | 'js'
): Promise<void> {
  return invoke<void>('openFile', type, id, ext)
}

export function openDevTools(): Promise<void> {
  return invoke<void>('openDevTools')
}

export function resetAppConfig(): Promise<void> {
  return invoke<void>('resetAppConfig')
}

export function createHeapSnapshot(): Promise<string> {
  return invoke<string>('createHeapSnapshot')
}

export function getUserAgent(): Promise<string> {
  return invoke<string>('getUserAgent')
}

export function generateAgeKeyPair(): Promise<{ identity: string; recipient: string }> {
  return invoke<{ identity: string; recipient: string }>('generateAgeKeyPair')
}

export function ageIdentityToRecipient(identity: string): Promise<string> {
  return invoke<string>('ageIdentityToRecipient', identity)
}

export function getAppName(appPath: string): Promise<string> {
  return invoke<string>('getAppName', appPath)
}

export function getImageDataURL(url: string): Promise<string> {
  return invoke<string>('getImageDataURL', url)
}

export function getIconDataURL(appPath: string): Promise<string> {
  return invoke<string>('getIconDataURL', appPath)
}

export function resolveThemes(): Promise<{ key: string; label: string; content: string }[]> {
  return invoke<{ key: string; label: string; content: string }[]>('resolveThemes')
}

export function fetchThemes(): Promise<void> {
  return invoke<void>('fetchThemes')
}

export function importThemes(files: string[]): Promise<void> {
  return invoke<void>('importThemes', files)
}

export function readTheme(theme: string): Promise<string> {
  return invoke<string>('readTheme', theme)
}

export function writeTheme(theme: string, css: string): Promise<void> {
  return invoke<void>('writeTheme', theme, css)
}

export function startNetworkDetection(): Promise<void> {
  return invoke<void>('startNetworkDetection')
}

export function stopNetworkDetection(): Promise<void> {
  return invoke<void>('stopNetworkDetection')
}

let applyThemeRunning = false
const waitList: string[] = []
export async function applyTheme(theme: string): Promise<void> {
  if (applyThemeRunning) {
    waitList.push(theme)
    return
  }
  applyThemeRunning = true
  try {
    return await ipcErrorWrapper(
      window.electron.ipcRenderer.invoke('applyTheme', theme) as Promise<void>
    )
  } finally {
    applyThemeRunning = false
    if (waitList.length > 0) {
      await applyTheme(waitList.shift() || '')
    }
  }
}

export function registerShortcut(
  oldShortcut: string,
  newShortcut: string,
  action: string
): Promise<boolean> {
  return invoke<boolean>('registerShortcut', oldShortcut, newShortcut, action)
}

export function copyEnv(type: 'bash' | 'fish' | 'cmd' | 'powershell' | 'nushell'): Promise<void> {
  return invoke<void>('copyEnv', type)
}

async function alert<T>(msg: T): Promise<void> {
  const msgStr = typeof msg === 'string' ? msg : JSON.stringify(msg)
  await window.electron.ipcRenderer.invoke('alert', msgStr)
}

window.alert = alert
