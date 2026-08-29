import { IPC_EVENTS } from '../shared/ipc'

/** Electron webContents event channels shared between main and renderer. */
export const EVENTS = IPC_EVENTS

export type EventName = (typeof EVENTS)[keyof typeof EVENTS]
