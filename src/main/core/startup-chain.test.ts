import { vi } from 'vitest'

vi.mock('../utils/dirs', () => ({
  mihomoIpcPath: () => '/tmp/sparkle-mihomo.sock',
  mihomoProfileWorkDir: (id: string | undefined) => `/tmp/work/${id || 'default'}`,
  mihomoWorkDir: () => '/tmp/work'
}))

import { describe, expect, it } from 'vitest'
import {
  createCoreEnvironment,
  createProviderInitializationTracker,
  createCoreSpawnArgs,
  isControllerReadyLog,
  isTunPermissionError
} from './startup-chain'

describe('startup-chain helpers', () => {
  it('creates core environment flags', () => {
    const env = createCoreEnvironment({
      disableLoopbackDetector: true,
      disableEmbedCA: false,
      disableSystemCA: true,
      disableNftables: false,
      safePaths: ['a', 'b']
    })
    expect(env.DISABLE_LOOPBACK_DETECTOR).toBe('true')
    expect(env.DISABLE_EMBED_CA).toBe('false')
    expect(env.DISABLE_SYSTEM_CA).toBe('true')
    expect(env.SAFE_PATHS).toContain('a')
  })

  it('tracks provider initialization and readiness', () => {
    const tracker = createProviderInitializationTracker({
      'proxy-providers': { Foo: {} },
      'rule-providers': { Bar: {} }
    })
    expect(tracker.hasProviders).toBe(true)
    tracker.track('Start initial provider Foo"')
    expect(tracker.isReady('anything')).toBe(false)
    tracker.track('Start initial provider Bar"')
    expect(tracker.isReady('anything')).toBe(true)
  })

  it('recognizes controller and tun errors', () => {
    expect(isControllerReadyLog('RESTful API listening at 127.0.0.1:9090')).toBe(true)
    expect(isTunPermissionError('Start TUN listening error: configure tun interface: operation not permitted')).toBe(true)
  })

  it('adds hook arguments when configured', () => {
    const args = createCoreSpawnArgs({
      current: 'id',
      diffWorkDir: false,
      ctlParam: '-ext-ctl-pipe',
      coreHook: { postUpCommand: 'up', postDownCommand: 'down' } as never
    })
    expect(args).toContain('-post-up')
    expect(args).toContain('up')
  })
})
