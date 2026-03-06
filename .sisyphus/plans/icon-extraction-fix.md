# 修复计划：Windows图标提取问题

## TL;DR

> **快速摘要**: 修复 `execFile` 参数错误和子进程清理问题，确保Windows平台图标提取在打包后正常工作。
>
> **交付物**:
>
> - 修复后的 `src/main/utils/icon.ts`
> - 验证通过的TypeScript类型检查
> - 增强的错误处理和资源清理
>
> **预估工作量**: Quick (30分钟)
> **并行执行**: NO - 单文件修改
> **关键路径**: 修复代码 → 类型检查 → 测试

---

## Context

### 原始问题

用户修改了 `src/main/utils/icon.ts` 以修复Windows打包后无法提取文件图标的问题。代码审查发现关键bug需要修复。

### 已发现的问题

1. **CRITICAL**: `execFile` 参数错误 - 第二个参数应该是数组 `[]`，而不是直接传回调
2. **MEDIUM**: 超时后子进程未清理 - 可能导致进程泄漏
3. **LOW**: 路径验证和模块级副作用

### 类型检查结果

运行 `npx tsc --noEmit --project tsconfig.node.json` 无错误输出，但建议在实际修复后再次验证。

---

## Work Objectives

### 核心目标

修复代码中的关键bug，确保：

1. `execFile` 调用参数正确
2. 超时后正确清理子进程
3. 错误处理更健壮

### 具体交付物

- `src/main/utils/icon.ts` - 修复后的文件
- TypeScript类型检查通过
- 代码质量符合项目标准

### 完成定义

- [ ] `execFile` 第二个参数改为空数组 `[]`
- [ ] 超时时终止子进程
- [ ] 运行 `pnpm typecheck:node` 通过
- [ ] 测试开发环境图标提取正常

### 必须要做

- 修复 CRITICAL 级别的 `execFile` 参数错误
- 保持现有的错误处理逻辑
- 保持回退到默认图标的行为

### 必须不要做

- 不要改变整体架构
- 不要添加新功能
- 不要修改打包配置（除非路径验证发现需要）

---

## Verification Strategy

### 测试决策

- **基础设施存在**: YES (项目有测试框架)
- **自动化测试**: YES (Tests-after)
- **框架**: 项目使用测试框架
- **Agent执行QA**: ALWAYS

### QA策略

每个任务必须包含agent执行的QA场景。证据保存在 `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`。

---

## Execution Strategy

### 并行执行波次

```
Wave 1 (串行 - 代码修复):
└── Task 1: 修复execFile参数和子进程清理 [quick]

Wave 2 (串行 - 验证):
└── Task 2: 运行类型检查和代码质量验证 [quick]

Wave 3 (串行 - 测试):
└── Task 3: 手动测试图标提取功能 [unspecified-low]
```

**关键路径**: Task 1 → Task 2 → Task 3

---

## TODOs

- [ ] 1. 修复 execFile 参数和子进程清理

**要做什么**:

1. 将 `execFile(exePath, callback)` 改为 `execFile(exePath, [], callback)`
2. 添加子进程超时清理逻辑
3. 保存 `child` 进程引用，在超时时调用 `child.kill()`
4. 确保超时定时器在所有分支下都被清理

**必须不要做**:

- 不要改变现有的回调逻辑
- 不要修改超时时间（保持5秒）
- 不要移除现有的错误处理

**推荐Agent配置**:

- **Category**: `quick`
- 原因: 单文件修复，明确的代码变更
- **Skills**: `[]`
- 无需特殊技能

**并行化**:

- **可以并行运行**: NO
- **并行组**: Wave 1 (单独)
- **阻塞**: Task 2, Task 3
- **被阻塞**: 无

**引用** (关键):

- `src/main/utils/icon.ts:35` - 需要修复的 `execFile` 调用
- `src/main/utils/icon.ts:260-288` - Promise 包装逻辑
- Node.js docs: `child_process.execFile(exe, [args], [options], callback)`

**为什么这些引用重要**:

- 第35行是bug的核心位置
- 第260-288行展示了超时处理模式，需要增强以清理子进程

**接受标准**:

- [ ] TypeScript编译通过
- [ ] 代码逻辑正确
- [ ] 子进程在超时时被终止
- [ ] 无新的 lint 错误

**QA场景**:

```
场景: 正常图标提取
工具: Bash (开发环境)
前置条件: 应用在开发模式运行
步骤:
1. 启动应用: pnpm dev
2. 在应用中选择一个exe文件
3. 观察图标是否正确显示
预期结果: 图标正确提取并显示
失败指示: 图标显示为默认图标或应用报错
证据: .sisyphus/evidence/task-1-normal-extraction.log

场景: 超时处理
工具: Bash (模拟)
前置条件: 代码修复完成
步骤:
1. 使用一个超大文件或模拟超时
2. 观察是否在5秒后正确处理超时
3. 检查子进程是否被清理
预期结果: 5秒后超时，返回默认图标，无进程泄漏
失败指示: 应用卡死或进程残留
证据: .sisyphus/evidence/task-1-timeout-handling.log

场景: 错误路径处理
工具: Bash
前置条件: 应用运行中
步骤:
1. 提供一个不存在的文件路径
2. 观察错误处理
预期结果: 返回默认图标，应用不崩溃
失败指示: 应用崩溃或报错
证据: .sisyphus/evidence/task-1-error-path.log
```

**提交**: NO (与Task 2一起提交)

---

- [ ] 2. 运行类型检查和代码质量验证

**要做什么**:

1. 运行 `pnpm typecheck:node` 或 `npx tsc --noEmit --project tsconfig.node.json`
2. 检查是否有类型错误
3. 如有错误，返回Task 1修复
4. 运行代码格式化 `pnpm format`

**必须不要做**:

- 不要跳过类型检查
- 不要忽略类型错误

**推荐Agent配置**:

- **Category**: `quick`
- 原因: 验证任务，执行简单命令
- **Skills**: `[]`

**并行化**:

- **可以并行运行**: NO
- **并行组**: Wave 2 (单独)
- **阻塞**: Task 3
- **被阻塞**: Task 1

**引用**:

- `package.json` - typecheck 命令定义
- `tsconfig.node.json` - TypeScript配置

**接受标准**:

- [ ] TypeScript编译无错误
- [ ] 无新的类型警告
- [ ] 代码格式符合项目规范

**QA场景**:

```
场景: 类型检查通过
工具: Bash
前置条件: Task 1完成
步骤:
1. 运行: npx tsc --noEmit --project tsconfig.node.json
2. 检查输出
预期结果: 无错误输出，退出码为0
失败指示: 类型错误或编译失败
证据: .sisyphus/evidence/task-2-typecheck.log
```

**提交**: NO (与Task 1一起提交)

---

- [ ] 3. 手动测试图标提取功能

**要做什么**:

1. 启动开发服务器 `pnpm dev`
2. 在应用中测试图标提取
3. 测试多种场景：
   - 正常exe文件
   - 中文路径文件
   - 不存在的文件路径
4. 验证错误处理和回退逻辑

**必须不要做**:

- 不要在未完成Task 1和2的情况下测试
- 不要跳过任何测试场景

**推荐Agent配置**:

- **Category**: `unspecified-low`
- 原因: 手动测试任务，需要用户交互
- **Skills**: `[]`

**并行化**:

- **可以并行运行**: NO
- **并行组**: Wave 3 (单独)
- **阻塞**: 无
- **被阻塞**: Task 2

**引用**:

- `README.md` - 开发环境启动说明

**接受标准**:

- [ ] 开发服务器正常启动
- [ ] 图标提取功能正常
- [ ] 错误场景正确处理
- [ ] 无控制台错误

**QA场景**:

```
场景: 完整功能测试
工具: Bash + 用户观察
前置条件: Task 1和2完成
步骤:
1. 启动应用: pnpm dev
2. 测试正常exe文件图标提取
3. 测试中文路径文件
4. 测试无效路径
5. 观察应用行为和控制台输出
预期结果: 所有场景正确处理，无崩溃
失败指示: 应用崩溃、卡死或报错
证据: .sisyphus/evidence/task-3-full-testing.log
```

**提交**: YES

- 消息: `fix(icon): fix execFile parameter and cleanup child process on timeout`
- 文件: `src/main/utils/icon.ts`
- 预提交: `pnpm typecheck:node`

---

## Final Verification Wave (强制)

- [ ] F1. **计划合规性审计** - `oracle`
      读取计划，验证每个"必须"都实现了，每个"必须不要做"都遵守了。

- [ ] F2. **代码质量审查** - `unspecified-high`
      运行 `tsc --noEmit` + linter + 格式检查。检查常见问题。

- [ ] F3. **实际功能测试** - `unspecified-high`
      在开发环境运行应用，执行所有QA场景。

- [ ] F4. **范围一致性检查** - `deep`
      验证只修复了计划中的问题，没有超出范围的修改。

---

## Commit Strategy

- **1**: `fix(icon): fix execFile parameter and cleanup child process on timeout`
  - 文件: `src/main/utils/icon.ts`
  - 预提交: `pnpm typecheck:node`

---

## Success Criteria

### 验证命令

```bash
# 类型检查
npx tsc --noEmit --project tsconfig.node.json
# 预期: 无错误输出

# 代码格式化
pnpm format
# 预期: 格式化完成

# 启动开发服务器
pnpm dev
# 预期: 应用正常启动
```

### 最终检查清单

- [ ] 所有"必须有"存在
- [ ] 所有"必须不要做"不存在
- [ ] 所有测试通过
- [ ] 代码已格式化
- [ ] TypeScript编译通过

---

## 执行指南

完成本计划后，运行：

```
/start-work icon-extraction-fix
```

这将：

1. 注册计划为活动任务
2. 跟踪进度
3. 启用自动继续功能
