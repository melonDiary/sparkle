import { controledMihomoConfigPath } from '../utils/dirs'
import { generateProfile } from '../core/factory'
import { getAppConfig } from './app'
import { defaultControledMihomoConfig } from '../utils/template'
import { deepMerge } from '../utils/merge'
import { CachedYamlStore } from './cached-yaml-store'

const controledMihomoConfigStore = new CachedYamlStore<Partial<MihomoConfig>>({
  path: controledMihomoConfigPath(),
  createDefault: () => defaultControledMihomoConfig,
  normalize: (value) =>
    typeof value === 'object' && value !== null
      ? (value as Partial<MihomoConfig>)
      : defaultControledMihomoConfig
})

export function getControledMihomoConfig(force = false): Promise<Partial<MihomoConfig>> {
  return controledMihomoConfigStore.get(force)
}

export async function patchControledMihomoConfig(patch: Partial<MihomoConfig>): Promise<void> {
  const config = await getControledMihomoConfig()
  const { controlDns = true, controlSniff = true } = await getAppConfig()
  if (!controlDns) {
    delete config.dns
    delete config.hosts
  } else {
    // 从不接管状态恢复
    if (config.dns?.ipv6 === undefined) {
      config.dns = defaultControledMihomoConfig.dns
    }
  }
  if (!controlSniff) {
    delete config.sniffer
  } else {
    // 从不接管状态恢复
    if (!config.sniffer) {
      config.sniffer = defaultControledMihomoConfig.sniffer
    }
  }
  if (patch.dns?.['nameserver-policy']) {
    config.dns = config.dns || {}
    config.dns['nameserver-policy'] = patch.dns['nameserver-policy']
  }
  if (patch.dns?.['proxy-server-nameserver-policy']) {
    config.dns = config.dns || {}
    config.dns['proxy-server-nameserver-policy'] = patch.dns['proxy-server-nameserver-policy']
  }
  if (patch.dns?.['use-hosts']) {
    config.hosts = patch.hosts
  }
  const nextConfig = deepMerge(config, patch)
  await generateProfile()
  await controledMihomoConfigStore.set(nextConfig)
}
