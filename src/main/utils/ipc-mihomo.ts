import { registerIpcHandler } from './ipc-registration'
import { ipcErrorWrapper } from './ipc-error'
import {
  mihomoChangeProxy,
  mihomoCloseConnection,
  mihomoCloseConnections,
  mihomoConfig,
  mihomoGroupDelay,
  mihomoGroups,
  mihomoProxyDelay,
  mihomoProxyProviders,
  mihomoProxies,
  mihomoRuleProviders,
  mihomoRules,
  mihomoRulesDisable,
  mihomoUnfixedProxy,
  mihomoUpdateProxyProviders,
  mihomoUpdateRuleProviders,
  mihomoUpgrade,
  mihomoUpgradeGeo,
  mihomoUpgradeUI,
  mihomoVersion,
  patchMihomoConfig,
  restartMihomoConnections,
  restartMihomoLogs
} from '../core/mihomoApi'

export function registerMihomoIpc(): void {
  const r = registerIpcHandler
  r('mihomoVersion', ipcErrorWrapper(mihomoVersion))
  r('mihomoConfig', ipcErrorWrapper(mihomoConfig))
  r('mihomoCloseConnection', (_e, id) => ipcErrorWrapper(mihomoCloseConnection)(id))
  r('mihomoCloseConnections', (_e, name) => ipcErrorWrapper(mihomoCloseConnections)(name))
  r('mihomoRules', ipcErrorWrapper(mihomoRules))
  r('mihomoProxies', ipcErrorWrapper(mihomoProxies))
  r('mihomoGroups', ipcErrorWrapper(mihomoGroups))
  r('mihomoProxyProviders', ipcErrorWrapper(mihomoProxyProviders))
  r('mihomoUpdateProxyProviders', (_e, name) => ipcErrorWrapper(mihomoUpdateProxyProviders)(name))
  r('mihomoRuleProviders', ipcErrorWrapper(mihomoRuleProviders))
  r('mihomoUpdateRuleProviders', (_e, name) => ipcErrorWrapper(mihomoUpdateRuleProviders)(name))
  r('mihomoChangeProxy', (_e, group, proxy) => ipcErrorWrapper(mihomoChangeProxy)(group, proxy))
  r('mihomoUnfixedProxy', (_e, group) => ipcErrorWrapper(mihomoUnfixedProxy)(group))
  r('mihomoUpgradeGeo', ipcErrorWrapper(mihomoUpgradeGeo))
  r('mihomoUpgradeUI', ipcErrorWrapper(mihomoUpgradeUI))
  r('mihomoUpgrade', (_e, channel) => ipcErrorWrapper(mihomoUpgrade)(channel))
  r('mihomoProxyDelay', (_e, proxy, url, provider) =>
    ipcErrorWrapper(mihomoProxyDelay)(proxy, url, provider)
  )
  r('mihomoGroupDelay', (_e, group, url) => ipcErrorWrapper(mihomoGroupDelay)(group, url))
  r('mihomoRulesDisable', (_e, rules) => ipcErrorWrapper(mihomoRulesDisable)(rules))
  r('patchMihomoConfig', (_e, patch) => ipcErrorWrapper(patchMihomoConfig)(patch))
  r('restartMihomoLogs', ipcErrorWrapper(restartMihomoLogs))
  r('restartMihomoConnections', ipcErrorWrapper(restartMihomoConnections))
}
