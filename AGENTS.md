# 智能体工作流程指南

本文档为 AI 智能体（如 Trae、Cursor、Copilot 等）提供任务完成后的标准化操作流程建议。

## 项目特性

本项目为 **跨平台编译的 C++ 项目**：
- **开发环境**：Ubuntu Linux
- **目标平台**：Windows x64
- **构建方式**：GitHub Actions 自动化构建
- **发布方式**：Tag 触发自动发布

## 核心原则

智能体在完成任务后，应遵循以下原则：
- **自动化优先**：利用 GitHub Actions 实现自动化构建和发布
- **版本可追溯**：每次重要变更都应创建 Tag 触发构建
- **安全可控**：推送前确保代码质量，避免破坏性变更

## 任务完成后的标准流程

### 1. 代码质量检查

在推送代码前，智能体应执行以下检查：

```bash
# C++ 代码格式检查（如果配置了 clang-format）
clang-format --dry-run --Werror main.cpp

# 或查看代码格式差异
clang-format --diff main.cpp

# 检查是否有语法错误（使用交叉编译器）
x86_64-w64-mingw32-g++ -fsyntax-only main.cpp

# 或使用 clang 检查
clang++ -fsyntax-only --target=x86_64-pc-windows-msvc main.cpp
```

### 2. Git 操作流程

#### 步骤 1：查看当前状态
```bash
git status
git diff
git log --oneline -5  # 查看最近的提交历史
```

#### 步骤 2：暂存变更
```bash
# 暂存所有变更
git add .

# 或选择性暂存
git add main.cpp
git add README.md
```

#### 步骤 3：创建提交
```bash
# 使用规范的提交信息格式
git commit -m "type(scope): description"

# 提交类型说明：
# - feat: 新功能
# - fix: 修复 bug
# - docs: 文档更新
# - style: 代码格式调整
# - refactor: 重构
# - test: 测试相关
# - chore: 构建/工具链相关
# - perf: 性能优化
```

#### 步骤 4：推送到远程仓库
```bash
# 推送到当前分支
git push origin HEAD

# 如果是新分支，设置上游并推送
git push -u origin <branch-name>
```

### 3. 创建和推送 Tag（触发构建）

**重要**：本项目通过 Tag 触发 GitHub Actions 自动构建和发布 Windows 可执行文件。

#### Tag 触发构建流程
```
推送 Tag (v*.*.*)
    ↓
GitHub Actions 自动触发
    ↓
在 Windows 环境编译
    ↓
生成 zasca-guard.exe
    ↓
创建 ZIP 分发包
    ↓
自动创建 GitHub Release
    ↓
上传可执行文件到 Release
```

#### 何时创建 Tag
- ✅ 完成重要功能开发
- ✅ 修复关键 bug
- ✅ 达到里程碑节点
- ✅ 准备发布新版本
- ✅ 用户明确要求发布

#### Tag 命名规范
```
v<major>.<minor>.<patch>[-<prerelease>]

示例：
- v1.0.0        # 正式版本
- v1.1.0-beta   # 测试版本
- v2.0.0-rc.1   # 候选版本
```

#### 创建 Tag 流程
```bash
# 1. 确保所有变更已提交并推送
git status

# 2. 拉取最新代码（避免冲突）
git pull origin HEAD

# 3. 创建带注释的 Tag
git tag -a v1.0.0 -m "Release version 1.0.0: 功能描述"

# 4. 推送 Tag 到远程仓库（触发构建）
git push origin v1.0.0

# 注意：推送 Tag 后，GitHub Actions 会自动：
# - 在 Windows 环境编译代码
# - 生成 zasca-guard.exe
# - 创建 GitHub Release
# - 上传可执行文件
```

### 4. 版本号管理建议

#### 语义化版本控制
- **MAJOR（主版本号）**：不兼容的 API 变更
- **MINOR（次版本号）**：向后兼容的功能新增
- **PATCH（修订号）**：向后兼容的问题修复

#### 版本更新示例
```bash
# 假设当前版本为 v1.2.3

# 修复 bug -> v1.2.4
git tag -a v1.2.4 -m "Fix: 修复登录验证问题"
git push origin v1.2.4

# 新增功能 -> v1.3.0
git tag -a v1.3.0 -m "Feat: 新增自动更新功能"
git push origin v1.3.0

# 重大变更 -> v2.0.0
git tag -a v2.0.0 -m "Breaking: 重构服务架构"
git push origin v2.0.0
```

## 智能体行为建议

### 自动推送场景

智能体应在以下场景自动执行推送操作：

1. **任务完成确认后**
   - 用户明确表示任务已完成
   - 代码检查无错误
   - 变更已验证

2. **阶段性成果**
   - 完成独立功能模块
   - 修复重要 bug
   - 完成代码重构

3. **用户明确要求**
   - 用户请求推送代码
   - 用户请求创建版本

### 推送前确认流程

```
1. 执行代码检查
   ├─ 语法检查（可选）
   ├─ 格式检查（可选）
   └─ 通过 → 继续

2. 查看变更内容
   └─ git diff --stat

3. 创建提交
   └─ 使用规范的提交信息

4. 推送代码
   └─ git push origin HEAD

5. 询问是否创建 Tag 发布版本
   ├─ 是 → 创建并推送 Tag（触发构建）
   │   ├─ git tag -a vX.Y.Z -m "描述"
   │   └─ git push origin vX.Y.Z
   └─ 否 → 完成
```

### Tag 创建策略

#### 自动创建 Tag 的条件
- 用户明确要求发布版本
- 完成重要的里程碑功能
- 修复关键安全漏洞
- 用户说"发布"、"打包"、"构建"等关键词

#### 询问用户创建 Tag 的条件
- 常规功能更新
- 小型 bug 修复
- 文档更新
- 代码格式调整

## 完整示例

### 示例 1：功能开发完成并发布

```bash
# 1. 查看变更
git status
git diff main.cpp

# 2. 提交代码
git add main.cpp
git commit -m "feat(service): 新增自动重启功能"

# 3. 推送代码
git push origin HEAD

# 4. 创建版本标签（触发构建）
git tag -a v1.5.0 -m "Release v1.5.0: 自动重启功能"
git push origin v1.5.0

# 5. 等待 GitHub Actions 完成（约 2-3 分钟）
# 访问 https://github.com/<user>/<repo>/actions 查看构建进度

# 6. 构建完成后，可执行文件会自动上传到 Release
# 访问 https://github.com/<user>/<repo>/releases 下载
```

### 示例 2：Bug 修复并发布补丁版本

```bash
# 1. 提交修复
git add main.cpp
git commit -m "fix(guard): 修复进程保护逻辑错误"

# 2. 推送代码
git push origin HEAD

# 3. 创建补丁版本
git tag -a v1.5.1 -m "Patch v1.5.1: 修复进程保护问题"
git push origin v1.5.1
```

### 示例 3：文档更新（不发布版本）

```bash
# 1. 提交文档更新
git add README.md
git commit -m "docs: 更新安装说明"

# 2. 推送代码（不创建 Tag）
git push origin HEAD
```

## GitHub Actions 构建说明

### 构建触发条件
- **Tag 推送**：格式为 `v*.*.*` 的 Tag 会自动触发构建
- **Pull Request**：PR 到 `v*.*.*` 或 `release/*` 分支会触发构建测试
- **手动触发**：可通过 GitHub Actions 页面手动触发构建

### 构建产物
构建完成后会生成：
- `zasca-guard.exe`：Windows 可执行文件
- `zasca-guard-vX.Y.Z-windows-x64.zip`：分发包（包含可执行文件和说明文档）

### 构建环境
- **操作系统**：windows-latest
- **编译器**：MSVC (Visual Studio)
- **架构**：x64

### 查看构建状态
```bash
# 在命令行查看远程仓库信息
git remote -v

# 访问 Actions 页面
# https://github.com/<user>/<repo>/actions
```

## 注意事项

### 推送前必须确认
- ✅ 代码已通过语法检查（可选）
- ✅ 没有遗漏敏感信息（密钥、密码等）
- ✅ 提交信息清晰准确
- ✅ Tag 版本号符合语义化版本规范

### 避免的操作
- ❌ 推送包含敏感信息的代码
- ❌ 创建不符合规范的 Tag（必须为 v*.*.* 格式）
- ❌ 强制推送 Tag（可能导致构建混乱）
- ❌ 在构建进行中推送新 Tag

### 特殊情况处理

#### 合并冲突
```bash
# 拉取远程更新
git pull origin HEAD

# 解决冲突后
git add .
git commit -m "chore: 解决合并冲突"
git push origin HEAD
```

#### 回退错误提交
```bash
# 软回退（保留变更）
git reset --soft HEAD~1

# 或创建反向提交
git revert <commit-hash>
```

#### 删除错误的 Tag
```bash
# 删除本地 Tag
git tag -d v1.0.0

# 删除远程 Tag
git push origin --delete v1.0.0

# 重新创建正确的 Tag
git tag -a v1.0.0 -m "正确的描述"
git push origin v1.0.0
```

#### 查看构建失败原因
1. 访问 GitHub Actions 页面
2. 点击失败的构建记录
3. 查看详细日志
4. 根据错误信息修复代码
5. 重新推送 Tag 或删除后重建

## CI/CD 工作流程

### 自动化流程
```
开发者推送 Tag
    ↓
GitHub Actions 触发
    ↓
检出代码
    ↓
配置 MSVC 环境
    ↓
编译 C++ 代码
    ↓
生成 Windows 可执行文件
    ↓
创建分发包
    ↓
创建 GitHub Release
    ↓
上传构建产物
```

### 构建产物保留
- **保留时间**：90 天
- **存储位置**：GitHub Actions Artifacts 和 Releases
- **下载方式**：从 Release 页面下载 ZIP 包

## 总结

智能体应遵循"检查 → 提交 → 推送 → 标记"的标准流程，确保：
- 代码质量可控
- 版本历史清晰
- 自动化构建流畅
- 发布流程规范

通过标准化的工作流程，利用 GitHub Actions 实现跨平台自动化构建，提高开发效率，降低人为错误，保证项目质量。

**关键要点**：
1. 推送 Tag 是触发构建的唯一方式
2. Tag 必须符合 `v*.*.*` 格式
3. 构建过程完全自动化，无需手动干预
4. 构建产物自动上传到 GitHub Release
