import { describe, expect, it, vi } from 'vitest'
import { createWindowController, type WindowLike } from './window-controller'

function createFakeWindow(visible = false): WindowLike {
  return {
    isVisible: vi.fn(() => visible),
    show: vi.fn(),
    close: vi.fn()
  }
}

describe('createWindowController', () => {
  it('shares an in-flight creation promise', async () => {
    let resolveCreation!: (window: WindowLike) => void
    const window = createFakeWindow()
    const create = vi.fn(() => new Promise<WindowLike>((resolve) => { resolveCreation = resolve }))
    const controller = createWindowController({ create })

    const first = controller.createWindow()
    const second = controller.createWindow()
    expect(create).toHaveBeenCalledOnce()

    resolveCreation(window)
    await expect(first).resolves.toBe(window)
    await expect(second).resolves.toBe(window)
    await expect(controller.createWindow()).resolves.toBe(window)
  })

  it('allows retry after creation failure', async () => {
    const window = createFakeWindow()
    const create = vi.fn()
      .mockRejectedValueOnce(new Error('failed'))
      .mockResolvedValueOnce(window)
    const controller = createWindowController({ create })

    await expect(controller.createWindow()).rejects.toThrow('failed')
    await expect(controller.createWindow()).resolves.toBe(window)
    expect(create).toHaveBeenCalledTimes(2)
  })

  it('shows and closes the current window', async () => {
    const window = createFakeWindow()
    const controller = createWindowController({ create: vi.fn().mockResolvedValue(window) })

    await controller.showWindow()
    controller.closeWindow()

    expect(window.show).toHaveBeenCalledOnce()
    expect(window.close).toHaveBeenCalledOnce()
    expect(controller.getWindow()).toBe(window)
  })

  it('triggers close for a visible window and shows a hidden one', async () => {
    const visibleWindow = createFakeWindow(true)
    const visibleController = createWindowController({ create: vi.fn().mockResolvedValue(visibleWindow) })
    await visibleController.createWindow()
    await visibleController.triggerWindow()
    expect(visibleWindow.close).toHaveBeenCalledOnce()
    expect(visibleWindow.show).not.toHaveBeenCalled()

    const hiddenWindow = createFakeWindow(false)
    const hiddenController = createWindowController({ create: vi.fn().mockResolvedValue(hiddenWindow) })
    await hiddenController.triggerWindow()
    expect(hiddenWindow.show).toHaveBeenCalledOnce()
  })
})
