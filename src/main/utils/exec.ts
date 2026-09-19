import { execFile } from 'child_process'
import { promisify } from 'util'

/**
 * Shared promisified `execFile`.
 *
 * Every platform command in the main process runs through this one instance.
 * Modules used to rebuild `promisify(execFile)` on each call (or hoist their own
 * copy), which duplicated the wrapper across a dozen files.
 */
export const execFileAsync = promisify(execFile)
