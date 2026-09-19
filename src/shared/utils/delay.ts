/**
 * Single shared timer helper. It used to be redefined in six modules across the
 * main and renderer processes.
 */
export function delay(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms)
  })
}
