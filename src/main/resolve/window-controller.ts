export interface WindowLike {
  isVisible: () => boolean
  show: () => void
  close: () => void
}

export interface WindowControllerOptions<T extends WindowLike> {
  create: () => Promise<T>
  onCreated?: (window: T) => void
}

export function createWindowController<T extends WindowLike>(
  options: WindowControllerOptions<T>
): {
  getWindow: () => T | null
  createWindow: () => Promise<T>
  showWindow: () => Promise<void>
  closeWindow: () => void
  triggerWindow: () => Promise<void>
} {
  let currentWindow: T | null = null
  let creating: Promise<T> | null = null

  const createWindow = async (): Promise<T> => {
    if (currentWindow) return currentWindow
    if (creating) return creating

    creating = options.create()
      .then((window) => {
        currentWindow = window
        options.onCreated?.(window)
        return window
      })
      .finally(() => {
        creating = null
      })

    return creating
  }

  const showWindow = async (): Promise<void> => {
    const window = await createWindow()
    window.show()
  }

  const closeWindow = (): void => {
    currentWindow?.close()
  }

  const triggerWindow = async (): Promise<void> => {
    if (currentWindow?.isVisible()) {
      closeWindow()
    } else {
      await showWindow()
    }
  }

  return {
    getWindow: () => currentWindow,
    createWindow,
    showWindow,
    closeWindow,
    triggerWindow
  }
}
