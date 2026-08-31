# Refactor Handoff

## Scope

This document records the architecture review and the refactors completed on the `dev` branch. The goal was to improve structure and quality without changing product behavior or public IPC APIs.

## Runtime architecture

The application is an Electron desktop client built with Electron Vite, React, TypeScript, SWR, and HeroUI.

### Startup flow

```text
src/main/index.ts
  -> app.requestSingleInstanceLock()
  -> init()
  -> app.whenReady()
       -> registerIpcMainHandlers(...)
       -> createWindow()
       -> startCore()
       -> startMonitor()
       -> initShortcut()
       -> createTray()
       -> optional floating window
```

The production Electron entrypoint is `out/main/index.js`. Development and builds are defined in `package.json` and `electron.vite.config.ts`.

### Process boundaries

```text
main/       Electron main process
preload/    contextBridge API
renderer/   React application and auxiliary windows
shared/     types and runtime IPC names
```

The main process owns Mihomo, filesystem configuration, system proxy/TUN operations, service mode, tray/window lifecycle, updates, and background servers. The renderer invokes main-process operations through preload and subscribes to push events.

### Data flow

```text
Renderer component/hook
  -> renderer/src/utils/ipc.ts
  -> window.electron.ipcRenderer.invoke()
  -> ipcMain.handle()
  -> src/main/utils/ipc-*.ts
  -> core/config/service/sys implementation
```

Mihomo streams flow in the opposite direction:

```text
Mihomo Unix-socket/service WebSocket
  -> src/main/core/mihomoApi.ts
  -> optional latest-value throttle
  -> BrowserWindow.webContents.send()
  -> ipcRenderer.on()
  -> React component/store
```

Configuration hooks use SWR and call `mutate()` in response to main-process update events.

## Completed changes

### 1. Mihomo stream deduplication

`src/main/core/mihomoApi.ts` now uses the shared `createMihomoStream()` controller for traffic, memory, logs, and connections.

This removed four duplicated reconnect implementations while preserving:

- one active socket per stream;
- bounded retries;
- one-second retry delay;
- retry-budget reset after a message;
- explicit stop/restart behavior.

### 2. High-frequency IPC throttling

`src/main/utils/latest-sender.ts` provides a reusable leading-and-trailing latest-value throttle.

Current settings:

- traffic: 100 ms;
- connections: 200 ms.

The first value is emitted immediately. During a throttle window only the newest value is kept. `resetMihomoApi()` clears pending values and timers. Logs are intentionally not throttled because dropping log events can affect history and diagnostics.

### 3. YAML configuration persistence abstraction

`src/main/config/cached-yaml-store.ts` centralizes:

- lazy loading;
- in-memory caching;
- forced reload;
- YAML parsing/serialization;
- default initialization;
- cache writes;
- cache clearing.

It is used by:

- `config/profile.ts`;
- `config/override.ts`;
- `config/controledMihomo.ts`.

`config/app.ts` intentionally keeps its specialized atomic write and backup behavior rather than being forced into the generic store.

### 4. IPC type safety

`src/shared/types/ipc-channels.d.ts` defines the compile-time channel/event unions. `src/shared/ipc.ts` provides runtime values:

- `IPC_CHANNELS`;
- `IPC_EVENTS`.

The preload boundary restricts `invoke`, `on`, `send`, and `removeAllListeners` to known names. Main-process registration is restricted to `IpcChannel` values.

### 5. IPC registration integrity check

`assertIpcChannelsRegistered()` runs after all main-process IPC feature modules register their handlers. It fails fast when a channel listed in `IPC_CHANNELS` has no registered handler.

This protects against a class of errors that compile-time unions cannot detect when the shared list itself is incomplete or a handler is accidentally omitted.

### 6. IPC/window dependency decoupling

`ipc-app.ts` and `ipc-window.ts` no longer use `index.ts` as a backdoor import for `setNotQuitDialog`.

Window IPC handlers now receive a `WindowIpcDeps` object containing:

- `getMainWindow`;
- `showMainWindow`;
- `closeMainWindow`;
- `triggerMainWindow`.

This removes the `index.ts -> ipc -> index.ts` dependency cycle for window IPC.

### 7. Focused tests

Added coverage for:

- Mihomo stream lifecycle and reconnect behavior;
- latest-value throttle semantics and cleanup;
- missing IPC handler detection.

### 8. Config-store tests

`src/main/config/cached-yaml-store.test.ts` isolates `CachedYamlStore` behavior with mocked `fs/promises`, covering lazy loading, forced reload, normalization, default initialization on `ENOENT`, `ENOENT` preservation when `initializeOnMissing: false`, non-`ENOENT` error propagation, write/cache-on-set, and cache invalidation via `clear()`.

### 9. Event literals replaced with `IPC_EVENTS`

All remaining literal push-event strings were converted to `IPC_EVENTS` in main-process files (`manager.ts`, `autoUpdater.ts`, `deepLink.ts`, `floatingWindow.ts`, `shortcut.ts`, `tray.ts`, `ssid.ts`, `log.ts`, `index.ts`, `service-core-runtime.ts`) and renderer components/hooks (`App.tsx`, `TrayMenuApp.tsx`, notification and resource/provider components, sider switchers, `mihomo-log-store.ts`). Event values are unchanged; only the source of each value now comes from `IPC_EVENTS`.

### 10. Renderer IPC wrapper migration

All 134 repetitive wrappers in `src/renderer/src/utils/ipc.ts` now delegate to the shared `invoke<T>()` helper instead of inline `ipcErrorWrapper(await window.electron.ipcRenderer.invoke(...))`. Exported function names and return types are preserved, including multi-line and nested-generic return types. `applyTheme` (which has serial-queue logic) and the non-exported `alert` wrapper are intentionally left on their existing paths.

### Window lifecycle test foundation

Added `src/main/resolve/window-controller.ts` and focused tests in `window-controller.test.ts`. The controller is Electron-independent and covers in-flight creation deduplication, retry after creation failure, show/close behavior, and visible/hidden trigger behavior. The controller is now integrated into `index.ts` for main-window creation deduplication and show/close/trigger operations. The Electron-specific `createWindow` implementation remains in `index.ts` to preserve lifecycle-sensitive behavior.

## Verification

Last verified commands:

```bash
pnpm run typecheck:node
pnpm run typecheck:web
pnpm test
```

Results:

```text
node typecheck: passed
web typecheck: passed
Test Files: 12 passed
Tests: 46 passed
```

The project uses pnpm (`pnpm@11.1.1`).

## Remaining work

These items are intentionally not fully completed yet.

### P2: Consolidate IPC contract authority

The shared `IpcContract` now defines argument/result pairs for all invoke channels. `IpcArgs`/`IpcResult`, the preload `invoke` boundary, and `registerIpcHandler` derive their types from it. The runtime `IPC_CHANNELS` array remains an explicit ordered list for stable registration assertions, with compile-time checks against the contract.

Future work should decide whether to make the contract the single source of truth by deriving channel names and the runtime list from one const representation. Avoid adding another parallel channel declaration; preserving the explicit runtime order is acceptable if the contract remains the type authority.

### P2: Further split the main-process bootstrap

The low-risk bootstrap split is complete:

```text
src/main/bootstrap/single-instance.ts
src/main/bootstrap/deep-link-wiring.ts
src/main/bootstrap/startup-tasks.ts
src/main/resolve/window-controller.ts
```

These modules own single-instance handling, macOS open-url wiring, startup task orchestration, and Electron-independent window lifecycle behavior. `index.ts` still owns the lifecycle-sensitive Electron window creation and top-level orchestration, including lightweight mode and core startup.

Further extraction should be driven by lifecycle tests. Potential future modules are `src/main/bootstrap/app-bootstrap.ts` and `src/main/resolve/main-window.ts`; do not change startup ordering without dedicated coverage.

### P3: Improve event listener payload typing

`IpcEventContract` now defines payload/result shapes for push and renderer-to-main events, with `IpcEventArgs` available to contract consumers. `send` is typed by event name and payload tuple. The preload listener boundary intentionally remains `any[]` because existing renderer callbacks have narrower independently declared parameter types; tightening it directly would create contravariance errors.

Next step: introduce typed listener helpers per event, then migrate callbacks incrementally without breaking existing renderer APIs.

## Known risks and review notes

1. **Throttle semantics**: traffic and connections are latest-value streams, so intermediate samples are intentionally discarded. Do not apply the same policy to logs or user-confirmation events.
2. **Runtime IPC assertion**: the application now fails during startup if `IPC_CHANNELS` contains a channel with no handler. When adding a new invoke channel, update both the shared list and its handler registration.
3. **Configuration path resolution**: `CachedYamlStore` receives its path during module initialization, matching the current application's stable data-directory behavior. If the app data path becomes mutable at runtime, the store should accept a path factory instead.
4. **Existing workspace changes**: prior work included `src/main/utils/http.test.ts`; it was intentionally not part of the refactor commit because it predated this work and is unrelated.
5. **No production smoke test**: typecheck and unit tests pass, but Electron window/core/service behavior should still be manually smoke-tested on each supported OS before release.

## Suggested next sequence

1. Decide whether `IpcContract` becomes the single source of truth for channel names, runtime registration, and handler types.
2. Add typed listener helpers and migrate renderer event callbacks incrementally.
3. Add lifecycle coverage before extracting the remaining order-sensitive bootstrap and window creation logic.
4. Manually smoke-test Electron window/core/service behavior on each supported OS before release.
