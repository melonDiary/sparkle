/** Runtime names shared by the main and renderer processes. */
export const IPC_EVENTS = {
  APP_CONFIG_UPDATED: 'appConfigUpdated',
  CONTROLLED_MIHOMO_CONFIG_UPDATED: 'controledMihomoConfigUpdated',
  PROFILE_CONFIG_UPDATED: 'profileConfigUpdated',
  OVERRIDE_CONFIG_UPDATED: 'overrideConfigUpdated',
  GROUPS_UPDATED: 'groupsUpdated',
  RULES_UPDATED: 'rulesUpdated',
  CORE_STARTED: 'core-started',
  CORE_STOPPED: 'core-stopped',
  CORE_STATUS_CHANGED: 'core-status-changed',
  MIHOMO_TRAFFIC: 'mihomoTraffic',
  MIHOMO_MEMORY: 'mihomoMemory',
  MIHOMO_CONNECTIONS: 'mihomoConnections',
  MIHOMO_LOGS: 'mihomoLogs',
  APP_NOTIFICATION: 'app-notification',
  APP_NOTIFICATION_DISMISS: 'app-notification-dismiss',
  UPDATE_STATUS: 'update-status',
  SHOW_QUIT_CONFIRM: 'show-quit-confirm',
  SHOW_PROFILE_INSTALL_CONFIRM: 'show-profile-install-confirm',
  SHOW_OVERRIDE_INSTALL_CONFIRM: 'show-override-install-confirm',
  RENDERER_CONTENT_READY: 'renderer-content-ready',
  APP_NOTIFICATION_READY: 'app-notification-ready',
  CUSTOM_TRAY_CLOSE: 'customTray:close',
  OVERRIDE_INSTALL_CONFIRM_RESULT: 'override-install-confirm-result',
  PROFILE_INSTALL_CONFIRM_RESULT: 'profile-install-confirm-result',
  QUIT_CONFIRM_RESULT: 'quit-confirm-result',
  TRAY_ICON_UPDATE: 'trayIconUpdate',
  UPDATE_FLOATING_WINDOW: 'updateFloatingWindow',
  UPDATE_TRAY_MENU: 'updateTrayMenu'
} as const satisfies Record<string, IpcEventName>

export type IpcEvent = (typeof IPC_EVENTS)[keyof typeof IPC_EVENTS]
