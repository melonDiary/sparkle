import { execFile } from 'child_process'
import path from 'path'
import { promisify } from 'util'
import { getAppConfig, getProfileConfig } from '../config'
import { mihomoCorePath, mihomoTestDir, mihomoWorkConfigPath } from '../utils/dirs'

export async function checkProfile(): Promise<void> {
  const [appConfig, profileConfig] = await Promise.all([getAppConfig(), getProfileConfig()])
  const { core = 'mihomo', diffWorkDir = false, safePaths = [] } = appConfig
  const { current } = profileConfig
  const corePath = mihomoCorePath(core)
  const execFilePromise = promisify(execFile)
  const env = {
    ...process.env,
    SAFE_PATHS: safePaths.join(path.delimiter)
  }
  try {
    await execFilePromise(
      corePath,
      [
        '-t',
        '-f',
        diffWorkDir ? mihomoWorkConfigPath(current) : mihomoWorkConfigPath('work'),
        '-d',
        mihomoTestDir()
      ],
      { env }
    )
  } catch (error) {
    if (error instanceof Error && 'stdout' in error) {
      const { stdout = '', stderr = '' } = error as { stdout?: string; stderr?: string }
      const output = `${stdout}\n${stderr}`
      const errorLines = output
        .split('\n')
        .filter((line) => /level=error|error:/i.test(line))
        .map((line) => line.replace(/^.*?(level=error|error:)\s*/i, '').trim())
      throw new Error(
        `Profile Check Failed:\n${errorLines.length > 0 ? errorLines.join('\n') : output.trim()}`
      )
    } else {
      throw error
    }
  }
}
