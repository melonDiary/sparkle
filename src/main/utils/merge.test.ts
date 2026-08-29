import { describe, expect, it } from 'vitest'
import { deepMerge } from './merge'

describe('deepMerge', () => {
  it('merges nested objects and replaces normal arrays', () => {
    expect(deepMerge({ dns: { enable: false, nameserver: ['a'] }, rules: ['old'] } as never, {
      dns: { enable: true },
      rules: ['new']
    } as never)).toEqual({ dns: { enable: true, nameserver: ['a'] }, rules: ['new'] })
  })

  it('prepends and appends arrays in override mode', () => {
    expect(deepMerge({ rules: ['base'] }, { '+rules': ['first'], 'rules+': ['last'] } as never, true)).toEqual({
      rules: ['first', 'base', 'last']
    })
  })

  it('supports forced object replacement', () => {
    expect(deepMerge({ dns: { enable: true, nameserver: ['a'] } }, {
      'dns!': { enable: false }
    } as never, true)).toEqual({ dns: { enable: false } })
  })

  it('replaces values with a bang-suffixed wrapped key', () => {
    expect(deepMerge({ rules: { old: true } }, { '<rules>!': { next: true } } as never)).toEqual({
      rules: { next: true }
    })
  })
})
