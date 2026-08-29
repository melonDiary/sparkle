import { addProfileItem, getCurrentProfileItem, getProfileConfig } from '../config'
import { appendAppLog } from '../utils/log'

const intervalPool: Record<string, NodeJS.Timeout> = {}

function getErrorMessage(error: unknown): string {
  if (error instanceof Error) return error.stack || error.message
  return String(error)
}

async function updateProfile(item: ProfileItem): Promise<void> {
  try {
    await addProfileItem(item)
    await appendAppLog(`[profile-updater] 订阅更新成功：${item.name} (${item.id})\n`)
  } catch (error) {
    await appendAppLog(
      `[profile-updater] 订阅更新失败：${item.name} (${item.id})\n${getErrorMessage(error)}\n`
    ).catch(() => {})
  }
}

function calculateUpdateDelay(item: ProfileItem): number {
  if (!item.interval) {
    return -1
  }

  const now = Date.now()
  const lastUpdated = item.updated || 0
  const intervalMs = item.interval * 60 * 1000
  const timeSinceLastUpdate = now - lastUpdated

  if (timeSinceLastUpdate >= intervalMs) {
    return 0
  }

  return intervalMs - timeSinceLastUpdate
}

export async function initProfileUpdater(): Promise<void> {
  const { items, current } = await getProfileConfig()
  const currentItem = await getCurrentProfileItem()
  for (const item of items.filter((i) => i.id !== current)) {
    if (item.type === 'remote' && item.interval && item.autoUpdate !== false) {
      const delay = calculateUpdateDelay(item)

      if (delay === -1) {
        continue
      }

      if (delay === 0) {
        await updateProfile(item)
      }

      if (intervalPool[item.id]) {
        clearTimeout(intervalPool[item.id])
      }

      intervalPool[item.id] = setTimeout(
        async () => {
          try {
            await addProfileItem(item)
          } catch (e) {
            // ignore
          }
        },
        delay === 0 ? item.interval * 60 * 1000 : delay
      )
    }
  }

  if (currentItem?.type === 'remote' && currentItem.interval && currentItem.autoUpdate !== false) {
    const delay = calculateUpdateDelay(currentItem)

    if (delay === 0) {
      try {
        await addProfileItem(currentItem)
      } catch (e) {
        // ignore
      }
    }

    if (intervalPool[currentItem.id]) {
      clearTimeout(intervalPool[currentItem.id])
    }

    intervalPool[currentItem.id] = setTimeout(
      async () => {
        await updateProfile(currentItem)
      },
      (delay === 0 ? currentItem.interval * 60 * 1000 : delay) + 10000 // +10s
    )
  }
}

export async function addProfileUpdater(item: ProfileItem): Promise<void> {
  if (item.type === 'remote' && item.interval && item.autoUpdate !== false) {
    if (intervalPool[item.id]) {
      clearTimeout(intervalPool[item.id])
    }

    const delay = calculateUpdateDelay(item)

    if (delay === -1) {
      return
    }

    if (delay === 0) {
      try {
        await addProfileItem(item)
      } catch (e) {
        // ignore
      }
    }

    intervalPool[item.id] = setTimeout(
      async () => {
        await updateProfile(item)
      },
      delay === 0 ? item.interval * 60 * 1000 : delay
    )
  }
}

export async function delProfileUpdater(id: string): Promise<void> {
  if (intervalPool[id]) {
    clearTimeout(intervalPool[id])
    delete intervalPool[id]
  }
}
