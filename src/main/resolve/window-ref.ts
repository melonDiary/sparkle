import type { BrowserWindow } from 'electron'

/**
 * Indirection for the main window and window-level actions.
 *
 * `index.ts` transitively imports nearly every module under src/main, so any
 * module importing `mainWindow` / `showMainWindow` from '..' creates a runtime
 * import cycle back into index.ts. Modules below must go through this registry
 * instead; index.ts wires the real implementations at startup.
 */
export interface MainWindowActions {
  showMainWindow: () => Promise<void>
  triggerMainWindow: () => Promise<void>
  closeMainWindow: () => void
}

let getMain: () => BrowserWindow | null = () => null
let actions: MainWindowActions = {
  showMainWindow: async () => {},
  triggerMainWindow: async () => {},
  closeMainWindow: () => {}
}

export function setMainWindowGetter(getter: () => BrowserWindow | null): void {
  getMain = getter
}

export function getMainWindow(): BrowserWindow | null {
  return getMain()
}

export function setMainWindowActions(next: MainWindowActions): void {
  actions = next
}

export function mainWindowActions(): MainWindowActions {
  return actions
}

// Convenience wrappers so call sites read like the old direct imports. Safe to
// call any time after module load: index.ts wires the real implementations at
// import time, before app.whenReady() fires any user-driven action.
export function showMainWindow(): Promise<void> {
  return actions.showMainWindow()
}

export function triggerMainWindow(): Promise<void> {
  return actions.triggerMainWindow()
}

export function closeMainWindow(): void {
  actions.closeMainWindow()
}
