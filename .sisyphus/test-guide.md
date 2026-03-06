# Windows 图标提取修复 - 测试指南

## 修复概览

### 已修复的问题

1. ✅ **execFile 参数错误** - 添加了空数组参数 `[]`
2. ✅ **子进程超时清理** - 实现了完整的进程生命周期管理
3. ✅ **打包配置错误** - 修正了 FileIconInfo.exe 的目标路径

### 修改的文件

- `src/main/utils/icon.ts` - 主要修复
- `electron-builder.yml` - 打包配置修复

---

## 测试计划

### Phase 1: 开发环境测试

#### 1.1 启动开发服务器

```bash
npm run dev
# 或
pnpm dev
```

**预期结果**:

- 应用正常启动
- 无控制台错误
- 主窗口正常显示

#### 1.2 测试图标提取功能

**测试场景 1: 正常 exe 文件**

1. 在应用中选择一个普通的 .exe 文件（如 notepad.exe）
2. 观察图标是否正确提取并显示
3. 检查控制台是否有错误日志

**预期结果**: ✅ 图标正确显示，无错误

**测试场景 2: 中文路径文件**

1. 选择路径包含中文的 .exe 文件
2. 例如: `C:\测试\应用.exe`
3. 观察图标提取是否正常

**预期结果**: ✅ 正常处理，图标正确显示

**测试场景 3: 无效路径**

1. 输入一个不存在的文件路径
2. 观察错误处理

**预期结果**: ✅ 返回默认图标，应用不崩溃

**测试场景 4: 超时处理**

1. 选择一个非常大的文件（如 >100MB）
2. 观察是否在 5 秒后超时
3. 检查进程列表，确认无进程泄漏

**预期结果**: ✅ 5秒后超时，返回默认图标，无残留进程

---

### Phase 2: 打包测试

#### 2.1 构建 Windows 应用

```bash
npm run build:win
# 或
pnpm build:win
```

**预期结果**:

- 构建成功完成
- 生成安装包和便携版
- 无构建错误

#### 2.2 验证打包结果

**检查文件结构**:

```
dist/
├── Sparkle-windows-{version}-{arch}-setup.exe
├── Sparkle-windows-{version}-{arch}-portable.7z
└── ...
```

**验证 FileIconInfo.exe 位置**:
安装或解压后，检查：

```
{安装目录}/resources/file-icon-info/FileIconInfo.exe
```

**命令验证** (Windows PowerShell):

```powershell
# 如果是安装版
Test-Path "C:\Users\{用户名}\AppData\Local\Programs\Sparkle\resources\file-icon-info\FileIconInfo.exe"

# 如果是便携版（解压后）
Test-Path ".\resources\file-icon-info\FileIconInfo.exe"
```

---

### Phase 3: 打包后应用测试

#### 3.1 安装/运行应用

1. 安装构建的安装包，或解压便携版
2. 启动应用

**预期结果**: ✅ 应用正常启动

#### 3.2 功能测试

重复 Phase 1.2 的所有测试场景

**关键验证点**:

- FileIconInfo.exe 是否被正确调用
- 图标是否正常提取
- 错误处理是否符合预期
- 无进程泄漏

---

## 调试技巧

### 添加调试日志

在打包后的应用中，可以临时添加日志来验证路径：

```typescript
// 在 src/main/utils/icon.ts 中添加（开发阶段）
console.log('=== Icon Extraction Debug ===')
console.log('Platform:', process.platform)
console.log('App Path:', app.getAppPath())
console.log('Exe Path:', exePath)
console.log('File Exists:', existsSync(exePath))
console.log('============================')
```

### 检查进程

Windows 任务管理器中检查：

- `FileIconInfo.exe` 进程是否正常启动和关闭
- 超时后是否有残留进程

### 查看日志

开发环境：控制台输出
打包后：应用日志文件（如果配置了）

---

## 常见问题排查

### 问题 1: 图标提取失败

**可能原因**:

- FileIconInfo.exe 未正确打包
- 路径错误

**检查步骤**:

1. 验证文件是否存在
2. 检查路径逻辑
3. 查看控制台错误日志

### 问题 2: 进程泄漏

**可能原因**:

- 超时后子进程未被终止

**检查方法**:

```powershell
# PowerShell
Get-Process | Where-Object {$_.ProcessName -eq "FileIconInfo"}
```

**解决方案**: 已在代码中实现 `child.kill()` 清理

### 问题 3: 中文路径问题

**可能原因**:

- 编码问题
- 符号链接创建失败

**检查**: 代码中已处理中文路径（创建符号链接）

---

## 成功标准

### 开发环境

- [ ] 应用正常启动
- [ ] 图标提取功能正常
- [ ] 错误场景正确处理
- [ ] 无进程泄漏
- [ ] 无控制台错误

### 打包应用

- [ ] 构建成功
- [ ] FileIconInfo.exe 在正确位置
- [ ] 应用正常启动
- [ ] 图标提取功能正常
- [ ] 错误处理符合预期

---

## 报告格式

测试完成后，请填写：

### 测试环境

- 操作系统: Windows \_\_\_
- Node.js 版本: \_\_\_
- 应用版本: \_\_\_

### 测试结果

- 开发环境测试: ✅/❌
- 打包构建: ✅/❌
- 打包后测试: ✅/❌

### 问题记录

（如有问题，请详细描述）

### 截图/日志

（可选，但有助于问题定位）

---

## 注意事项

1. **Windows 平台**: 此修复仅针对 Windows 平台
2. **权限**: 确保有足够的文件系统权限
3. **杀毒软件**: 某些杀毒软件可能阻止进程启动
4. **测试覆盖**: 尽量覆盖所有测试场景
5. **性能**: 关注大量图标提取时的性能表现

---

## 下一步行动

完成测试后：

1. 如果测试通过，可以提交代码
2. 如果发现问题，记录详细日志
3. 根据问题调整修复方案

祝测试顺利！
