#!/usr/bin/env node

/**
 * Windows 图标提取修复验证脚本
 * 用于验证修复是否正确应用
 */

import { readFileSync, existsSync } from 'fs'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)
const projectRoot = __dirname.includes('.sisyphus') ? join(__dirname, '..') : process.cwd()

console.log('=== Windows 图标提取修复验证 ===')

let hasErrors = false

// 验证 1: 检查 icon.ts 中的 execFile 参数
console.log('验证 1: execFile 参数修复')
try {
  const iconPath = join(projectRoot, 'src', 'main', 'utils', 'icon.ts')
  const iconContent = readFileSync(iconPath, 'utf-8')

  // 检查是否包含正确的 execFile 调用
  const hasCorrectExecFile = /execFile\(exePath,\s*\[\]/.test(iconContent)

  if (hasCorrectExecFile) {
    console.log('  ✅ execFile 参数正确: execFile(exePath, [], ...)')
  } else {
    console.log('  ❌ execFile 参数错误: 缺少空数组参数')
    hasErrors = true
  }

  // 检查是否包含 ChildProcess 类型导入
  const hasChildProcessImport = /import.*ChildProcess.*from ['"]child_process['"]/.test(iconContent)

  if (hasChildProcessImport) {
    console.log('  ✅ 已导入 ChildProcess 类型')
  } else {
    console.log('  ❌ 缺少 ChildProcess 类型导入')
    hasErrors = true
  }

  // 检查是否包含子进程清理逻辑
  const hasProcessCleanup = /child\.kill\(\)/.test(iconContent)

  if (hasProcessCleanup) {
    console.log('  ✅ 包含子进程清理逻辑 (child.kill())')
  } else {
    console.log('  ❌ 缺少子进程清理逻辑')
    hasErrors = true
  }
} catch (error) {
  console.log('  ❌ 无法读取 icon.ts:', error.message)
  hasErrors = true
}

console.log('')

// 验证 2: 检查 electron-builder.yml 配置
console.log('验证 2: 打包配置修复')
try {
  const builderPath = join(projectRoot, 'electron-builder.yml')
  const builderContent = readFileSync(builderPath, 'utf-8')

  // 检查 FileIconInfo.exe 的目标路径
  const hasCorrectPath = /to:\s*['"]file-icon-info\/FileIconInfo\.exe['"]/.test(builderContent)

  if (hasCorrectPath) {
    console.log('  ✅ 打包路径正确: file-icon-info/FileIconInfo.exe')
  } else {
    console.log('  ❌ 打包路径错误: 应为 file-icon-info/FileIconInfo.exe')
    hasErrors = true
  }
} catch (error) {
  console.log('  ❌ 无法读取 electron-builder.yml:', error.message)
  hasErrors = true
}

console.log('')

// 验证 3: 检查必要的文件是否存在
console.log('验证 3: 必要文件检查')
try {
  const fileIconInfoPath = join(
    projectRoot,
    'node_modules',
    'file-icon-info',
    'dist',
    'FileIconInfo.exe'
  )

  if (existsSync(fileIconInfoPath)) {
    console.log('  ✅ FileIconInfo.exe 存在于 node_modules')
  } else {
    console.log('  ⚠️  FileIconInfo.exe 不在 node_modules (可能需要运行 npm install)')
  }
} catch (error) {
  console.log('  ⚠️  无法检查 FileIconInfo.exe:', error.message)
}

console.log('')

// 验证 4: 检查 TypeScript 编译
console.log('验证 4: TypeScript 类型检查')
console.log('  ⏳ 运行 TypeScript 编译检查...')
console.log('  请手动运行: npx tsc --noEmit --project tsconfig.node.json')
console.log('  或运行: npm run typecheck:node')

console.log('')

// 总结
console.log('=== 验证总结 ===')
if (hasErrors) {
  console.log('❌ 发现错误，请检查上述标记为 ❌ 的项目')
  process.exit(1)
} else {
  console.log('✅ 所有自动检查通过！')
  console.log('')
  console.log('下一步建议:')
  console.log('  1. 运行: npm run typecheck:node')
  console.log('  2. 运行: npm run dev (测试开发环境)')
  console.log('  3. 运行: npm run build:win (打包测试)')
  console.log('')
  console.log('详细测试指南请查看: .sisyphus/test-guide.md')
  process.exit(0)
}
