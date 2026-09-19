// eslint-disable-next-line @typescript-eslint/no-explicit-any
function isObject(item: any): boolean {
  return item && typeof item === 'object' && !Array.isArray(item)
}

function trimWrap(str: string): string {
  if (str.startsWith('<') && str.endsWith('>')) {
    return str.slice(1, -1)
  }
  return str
}

const dangerousMergeKeys = new Set(['__proto__', 'prototype', 'constructor'])

/**
 * Strip the override markers (`+key`, `key+`, `key!`, `<key>`) so a disguised key
 * such as `+__proto__` or `<__proto__>` cannot bypass the safety check below.
 */
function normalizeMergeKey(key: string): string {
  return key.replace(/^[+<]+/, '').replace(/[+!>]+$/, '')
}

// Merged input can come from remote subscriptions and hand-written override files,
// so prototype-mutating keys must never reach the assignment below.
function isSafeMergeKey(key: string): boolean {
  return !dangerousMergeKeys.has(normalizeMergeKey(key))
}

export function deepMerge<T extends object>(target: T, other: Partial<T>, isOverride?: boolean): T {
  for (const key in other) {
    if (!Object.prototype.hasOwnProperty.call(other, key)) continue
    if (!isSafeMergeKey(key)) continue

    if (isObject(other[key])) {
      if (key.endsWith('!')) {
        const k = trimWrap(key.slice(0, -1))
        target[k] = other[key]
      } else {
        const k = trimWrap(key)
        if (!target[k]) Object.assign(target, { [k]: {} })
        deepMerge(target[k] as object, other[k] as object, isOverride)
      }
    } else if (Array.isArray(other[key])) {
      if (isOverride && key.startsWith('+')) {
        const k = trimWrap(key.slice(1))
        if (!target[k]) Object.assign(target, { [k]: [] })
        target[k] = [...other[key], ...(target[k] as never[])]
      } else if (isOverride && key.endsWith('+')) {
        const k = trimWrap(key.slice(0, -1))
        if (!target[k]) Object.assign(target, { [k]: [] })
        target[k] = [...(target[k] as never[]), ...other[key]]
      } else {
        const k = trimWrap(key)
        Object.assign(target, { [k]: other[key] })
      }
    } else {
      Object.assign(target, { [key]: other[key] })
    }
  }
  return target as T
}
