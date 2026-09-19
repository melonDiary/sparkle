import { overrideConfigPath, overridePath } from '../utils/dirs'
import { getControledMihomoConfig } from './controledMihomo'
import { readFile, writeFile, rm } from 'fs/promises'
import { existsSync } from 'fs'
import { requestRemoteText } from '../utils/remote-request'
import { CachedYamlStore } from './cached-yaml-store'

const overrideConfigStore = new CachedYamlStore<OverrideConfig>({
  path: overrideConfigPath(),
  createDefault: () => ({ items: [] }),
  normalize: (value) =>
    typeof value === 'object' && value !== null ? (value as OverrideConfig) : { items: [] },
  initializeOnMissing: false
})

export function getOverrideConfig(force = false): Promise<OverrideConfig> {
  return overrideConfigStore.get(force)
}

export function setOverrideConfig(config: OverrideConfig): Promise<void> {
  return overrideConfigStore.set(config)
}

export async function getOverrideItem(id: string | undefined): Promise<OverrideItem | undefined> {
  const { items } = await getOverrideConfig()
  return items.find((item) => item.id === id)
}

export async function updateOverrideItem(item: OverrideItem): Promise<void> {
  const config = await getOverrideConfig()
  const index = config.items.findIndex((i) => i.id === item.id)
  if (index === -1) {
    throw new Error('Override not found')
  }
  config.items[index] = item
  await setOverrideConfig(config)
}

export async function addOverrideItem(item: Partial<OverrideItem>): Promise<void> {
  const config = await getOverrideConfig()
  const newItem = await createOverride(item)
  if (await getOverrideItem(newItem.id)) {
    await updateOverrideItem(newItem)
  } else {
    config.items.push(newItem)
  }
  await setOverrideConfig(config)
}

export async function removeOverrideItem(id: string): Promise<void> {
  const config = await getOverrideConfig()
  const item = await getOverrideItem(id)
  config.items = config.items?.filter((item) => item.id !== id)
  await setOverrideConfig(config)

  const overrideFilePath = overridePath(id, item?.ext || 'js')
  if (existsSync(overrideFilePath)) {
    await rm(overrideFilePath)
  }
}

export async function createOverride(item: Partial<OverrideItem>): Promise<OverrideItem> {
  const id = item.id || new Date().getTime().toString(16)
  const newItem = {
    id,
    name: item.name || (item.type === 'remote' ? 'Remote File' : 'Local File'),
    type: item.type,
    ext: item.ext || 'js',
    url: item.url,
    global: item.global || false,
    updated: new Date().getTime()
  } as OverrideItem
  switch (newItem.type) {
    case 'remote': {
      const { 'mixed-port': mixedPort = 7890 } = await getControledMihomoConfig()
      if (!item.url) throw new Error('Empty URL')
      const res = await requestRemoteText({
        url: item.url,
        fingerprint: item.fingerprint,
        proxyPort: mixedPort
      })
      const data = res.data
      await setOverride(id, newItem.ext, data)
      break
    }
    case 'local': {
      const data = item.file || ''
      await setOverride(id, newItem.ext, data)
      break
    }
  }

  return newItem
}

export async function getOverride(id: string, ext: 'js' | 'yaml' | 'log'): Promise<string> {
  if (!existsSync(overridePath(id, ext))) {
    return ''
  }
  return await readFile(overridePath(id, ext), 'utf-8')
}

export async function setOverride(id: string, ext: 'js' | 'yaml', content: string): Promise<void> {
  await writeFile(overridePath(id, ext), content, 'utf-8')
}
