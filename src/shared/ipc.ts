export interface IpcContract {
  [channel: string]: { args: unknown[]; result: unknown }
}

export interface IpcEventContract {
  appConfigUpdated: { args: []; result: never }
  controledMihomoConfigUpdated: { args: []; result: never }
  profileConfigUpdated: { args: []; result: never }
  overrideConfigUpdated: { args: []; result: never }
  groupsUpdated: { args: []; result: never }
  rulesUpdated: { args: []; result: never }
  'core-started': { args: [event?: unknown]; result: never }
  'core-stopped': { args: [event?: unknown]; result: never }
  'core-status-changed': { args: [event: unknown]; result: never }
  mihomoTraffic: { args: [data: unknown]; result: never }
  mihomoMemory: { args: [data: ControllerMemory]; result: never }
  mihomoConnections: { args: [data: unknown]; result: never }
  mihomoLogs: { args: [log: ControllerLog]; result: never }
  'app-notification': { args: [notification: unknown]; result: never }
  'app-notification-dismiss': { args: [id: string]; result: never }
  'update-status': { args: [status: unknown]; result: never }
  'show-quit-confirm': { args: []; result: boolean }
  'show-profile-install-confirm': { args: [data: unknown]; result: boolean }
  'show-override-install-confirm': { args: [data: unknown]; result: boolean }
  'renderer-content-ready': { args: []; result: never }
  'app-notification-ready': { args: []; result: never }
  'customTray:close': { args: []; result: never }
  'override-install-confirm-result': { args: [confirmed: boolean]; result: never }
  'profile-install-confirm-result': { args: [confirmed: boolean]; result: never }
  'quit-confirm-result': { args: [confirmed: boolean]; result: never }
  trayIconUpdate: { args: [data?: string]; result: never }
  updateFloatingWindow: { args: []; result: never }
  updateTrayMenu: { args: []; result: never }
}

export type IpcChannelName = keyof IpcContract
export type IpcArgs<C extends IpcChannelName> = IpcContract[C]['args']
export type IpcResult<C extends IpcChannelName> = IpcContract[C]['result']
export type IpcEventName = keyof IpcEventContract
export type IpcChannel = IpcChannelName
export type KnownIpcChannel = IpcChannelName
export type TypedIpcContract = IpcContract
export type IpcEventArgs<E extends IpcEventName> = IpcEventContract[E]['args']

const IPC_CHANNEL_NAMES = [
  'addOverrideItem','addProfileItem','ageIdentityToRecipient','alert','applyTheme','cancelUpdate','changeCurrentProfile','checkAutoRun','checkCorePermission','checkElevateTask','checkUpdate','clearCachedMihomoLogs','closeFloatingWindow','closeMainWindow','closeTrayIcon','copyEnv','createHeapSnapshot','deleteElevateTask','disableAutoRun','downloadAndInstallUpdate','downloadSubStore','enableAutoRun','fetchThemes','findSystemMihomo','generateAgeKeyPair','getAppConfig','getAppName','getCachedMihomoLogs','getControledMihomoConfig','getCurrentProfileItem','getCurrentProfileStr','getFilePath','getFilePreviewStr','getFileStr','getGistUrl','getIconDataURL','getImageDataURL','getInterfaces','getOverride','getOverrideConfig','getOverrideItem','getOverrideProfileStr','getProfileConfig','getProfileItem','getProfileStr','getRawProfileStr','getRuntimeConfig','getRuntimeConfigStr','getUserAgent','getVersion','importThemes','initService','installService','isAlwaysOnTop','listWebdavBackups','manualGrantCorePermition','mihomoChangeProxy','mihomoCloseConnection','mihomoCloseConnections','mihomoConfig','mihomoGroupDelay','mihomoGroups','mihomoProxies','mihomoProxyDelay','mihomoProxyProviders','mihomoRuleProviders','mihomoRules','mihomoRulesDisable','mihomoUnfixedProxy','mihomoUpdateProxyProviders','mihomoUpdateRuleProviders','mihomoUpgrade','mihomoUpgradeGeo','mihomoUpgradeUI','mihomoVersion','notDialogQuit','openDevTools','openFile','openUWPTool','patchAppConfig','patchControledMihomoConfig','patchMihomoConfig','platform','quitApp','quitWithoutCore','readImageFileDataURL','readTextFile','readTheme','registerShortcut','relaunchApp','removeOverrideItem','removeProfileItem','resetAppConfig','resolveThemes','restartCore','restartMihomoConnections','restartMihomoLogs','restartService','revokeCorePermission','saveFileStrWithElevation','serviceStatus','setAlwaysOnTop','setDockVisible','setFileStr','setNativeTheme','setOverride','setOverrideConfig','setProfileConfig','setProfileStr','setTitleBarOverlay','setupFirewall','showContextMenu','showFloatingWindow','showMainWindow','showTrayIcon','startMonitor','startNetworkDetection','startService','startSubStoreBackendServer','startSubStoreFrontendServer','stopCore','stopNetworkDetection','stopService','stopSubStoreBackendServer','stopSubStoreFrontendServer','subStoreCollections','subStoreFrontendPort','subStorePort','subStoreSubs','testServiceConnection','triggerMainWindow','triggerSysProxy','uninstallService','updateOverrideItem','updateProfileItem','updateTrayIcon','webdavBackup','webdavDelete','webdavRestore','writeTheme'
] as const

export const IPC_CHANNELS = IPC_CHANNEL_NAMES as readonly IpcChannelName[]

export const IPC_EVENTS = {
  APP_CONFIG_UPDATED: 'appConfigUpdated', CONTROLLED_MIHOMO_CONFIG_UPDATED: 'controledMihomoConfigUpdated', PROFILE_CONFIG_UPDATED: 'profileConfigUpdated', OVERRIDE_CONFIG_UPDATED: 'overrideConfigUpdated', GROUPS_UPDATED: 'groupsUpdated', RULES_UPDATED: 'rulesUpdated', CORE_STARTED: 'core-started', CORE_STOPPED: 'core-stopped', CORE_STATUS_CHANGED: 'core-status-changed', MIHOMO_TRAFFIC: 'mihomoTraffic', MIHOMO_MEMORY: 'mihomoMemory', MIHOMO_CONNECTIONS: 'mihomoConnections', MIHOMO_LOGS: 'mihomoLogs', APP_NOTIFICATION: 'app-notification', APP_NOTIFICATION_DISMISS: 'app-notification-dismiss', UPDATE_STATUS: 'update-status', SHOW_QUIT_CONFIRM: 'show-quit-confirm', SHOW_PROFILE_INSTALL_CONFIRM: 'show-profile-install-confirm', SHOW_OVERRIDE_INSTALL_CONFIRM: 'show-override-install-confirm', RENDERER_CONTENT_READY: 'renderer-content-ready', APP_NOTIFICATION_READY: 'app-notification-ready', CUSTOM_TRAY_CLOSE: 'customTray:close', OVERRIDE_INSTALL_CONFIRM_RESULT: 'override-install-confirm-result', PROFILE_INSTALL_CONFIRM_RESULT: 'profile-install-confirm-result', QUIT_CONFIRM_RESULT: 'quit-confirm-result', TRAY_ICON_UPDATE: 'trayIconUpdate', UPDATE_FLOATING_WINDOW: 'updateFloatingWindow', UPDATE_TRAY_MENU: 'updateTrayMenu'
} as const satisfies Record<string, IpcEventName>
