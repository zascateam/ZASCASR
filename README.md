# ZASCA Windows Service Guard

## 项目简介

ZASCA（零代理安全管控架构）是一个基于 Django 的企业级 Windows 主机远程管理平台，采用零代理架构，通过 WinRM 协议实现对 Windows 主机的云电脑自动开户。

本工具是 ZASCA 的 Windows 服务端部署守护程序，提供一键部署、自动更新、进程守护等企业级功能。

## 核心特性

### 🚀 一键部署
- **首次启动自动初始化**：检测到首次运行时自动完成环境配置
- **自动安装 uv 包管理器**：无需手动安装 Python 包管理工具
- **依赖自动同步**：自动执行 `uv sync` 安装所有依赖

### 🌐 国内镜像加速
全链路国内镜像加速，解决 GitHub 和 PyPI 访问慢的问题：
- **Python 解释器镜像**：清华大学 TUNA 镜像源
- **PyPI 包镜像**：清华大学 TUNA PyPI 源
- **GitHub 代理**：自动使用国内加速代理下载更新

### 🔒 零信任安全保护
- **进程保护**：拒绝普通用户终止服务进程
- **目录权限控制**：自动设置程序目录为仅管理员和 SYSTEM 可访问
- **隐藏标记文件**：使用系统隐藏文件标记初始化状态

### 🛡️ 企业级守护服务
- **Windows 服务模式**：以系统服务形式运行，开机自启
- **自动重启机制**：Django 服务异常退出后自动重启
- **多端口支持**：支持自定义端口运行（默认 8000）

### 📦 自动更新
- **GitHub Releases 集成**：自动检测最新版本
- **一键更新**：下载、解压、替换全自动完成
- **更新后自动重启**：更新完成自动重启服务

## 使用方法

### 快速开始

1. **以管理员身份运行** `zasca-guard.exe`
   - 首次运行会自动初始化环境
   - 自动设置目录权限
   - 自动安装 uv 并同步依赖
   - 初始化完成后启动服务

### 命令行参数

```bash
# 启动服务（默认端口 8000）
zasca-guard.exe

# 指定端口启动
zasca-guard.exe 9000

# 启动服务（显式指定）
zasca-guard.exe start
zasca-guard.exe start 9000

# 停止服务
zasca-guard.exe stop

# 重启服务
zasca-guard.exe restart
zasca-guard.exe restart 9000

# 初始化环境
zasca-guard.exe init

# 检查并更新到最新版本
zasca-guard.exe update

# 透传命令给 Django manage.py
zasca-guard.exe migrate
zasca-guard.exe createsuperuser
zasca-guard.exe shell
```

### 初始化选项

运行 `zasca-guard.exe init` 时，可以选择：
- **从 GitHub 拉取最新版本**：自动下载最新 Release 并更新
- **使用当前代码初始化**：跳过更新，直接初始化环境

### 服务管理

#### 启动服务
```bash
zasca-guard.exe start [port]
```
- 如果服务已在运行，会提示选择：
  - 关闭现有服务并重新启动
  - 忽略警告，前台运行新实例（用于多端口调试）
  - 取消操作

#### 停止服务
```bash
zasca-guard.exe stop
```
- 停止并卸载 Windows 服务

#### 重启服务
```bash
zasca-guard.exe restart [port]
```
- 先停止现有服务，再启动新服务

## 系统要求

- **操作系统**：Windows 10/11 或 Windows Server 2016+
- **权限要求**：管理员权限（用于服务安装和权限设置）
- **网络要求**：需要访问以下地址（首次运行或更新时）
  - GitHub API：`api.github.com`
  - GitHub 代理：`ghproxy.sectl.top`、`ghfast.top`
  - 清华镜像源：`mirrors.tuna.tsinghua.edu.cn`、`pypi.tuna.tsinghua.edu.cn`

## 技术架构

### 服务名称
- **服务名**：`DjangoGuardSvc`
- **显示名**：`Django Environment Guard Service`

### 环境变量配置
程序自动配置以下环境变量以实现国内镜像加速：

```bash
UV_INSTALLER_GITHUB_BASE_URL=https://ghfast.top/https://github.com/
UV_PYTHON_INSTALL_MIRROR=https://mirrors.tuna.tsinghua.edu.cn/python/
UV_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
```

### 安全机制

#### 进程保护
通过 Windows 安全描述符（Security Descriptor）实现：
- **拒绝**：INTERACTIVE 用户组的 `PROCESS_TERMINATE` 权限
- **允许**：SYSTEM 和 Administrators 组的完全访问权限

#### 目录权限
通过 DACL（自主访问控制列表）设置：
- **Administrators**：完全控制（继承到子目录和文件）
- **SYSTEM**：完全控制（继承到子目录和文件）
- **其他用户**：无访问权限

### 日志系统
- **日志文件**：`zasca-guard.log`（与程序同目录）
- **日志级别**：INFO、ERROR、DEBUG、HTTP、JSON
- **日志内容**：
  - 服务启动/停止事件
  - HTTP 请求和响应详情
  - JSON 解析过程
  - 错误和异常信息

## 工作流程

### 首次启动流程

```
1. 检测 .initialized 文件是否存在
   ↓
2. 设置目录权限（仅管理员和 SYSTEM）
   ↓
3. 检测 uv 是否已安装
   ├─ 未安装 → 通过 PowerShell 脚本安装
   └─ 已安装 → 跳过
   ↓
4. 执行 uv sync 安装依赖
   ↓
5. 创建 .initialized 标记文件
   ↓
6. 启动服务
```

### 服务守护流程

```
1. 服务启动
   ↓
2. 检测并安装 uv（如果需要）
   ↓
3. 执行 uv sync（受保护进程）
   ↓
4. 启动 Django 开发服务器（受保护进程）
   ↓
5. 监控服务进程
   ├─ 进程异常退出 → 等待 3 秒后重启
   └─ 收到停止信号 → 清理并退出
```

### 更新流程

```
1. 调用 GitHub API 获取最新 Release
   ↓
2. 解析 JSON 获取 zipball_url 和 tag_name
   ↓
3. 用户确认是否更新
   ↓
4. 通过国内代理下载 ZIP 文件
   ↓
5. 解压到临时目录
   ↓
6. 复制文件到程序目录
   ↓
7. 清理临时文件
   ↓
8. 重启程序
```

## 常见问题

### 1. 提示"无法打开服务控制管理器"
**原因**：未以管理员身份运行  
**解决**：右键点击程序，选择"以管理员身份运行"

### 2. uv 安装失败
**原因**：GitHub 代理失效或网络问题  
**解决**：
- 手动安装 uv：`powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"`
- 或从官方下载：https://github.com/astral-sh/uv

### 3. 服务无法启动
**原因**：端口被占用或依赖未安装  
**解决**：
- 检查端口占用：`netstat -ano | findstr :8000`
- 手动初始化：`zasca-guard.exe init`
- 查看日志：`zasca-guard.log`

### 4. 无法停止服务
**原因**：进程受保护，普通用户无权终止  
**解决**：以管理员身份运行 `zasca-guard.exe stop`

### 5. 更新失败
**原因**：网络问题或 GitHub API 限流  
**解决**：
- 检查网络连接
- 等待几分钟后重试
- 手动下载最新版本

## 开发说明

### 编译要求
- Visual Studio 2019+ 或 MinGW-w64
- Windows SDK 10.0+
- C++17 标准

### 链接库
```cpp
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
```

### 主要依赖
- `winhttp.h`：HTTP 请求和文件下载
- `aclapi.h`：安全描述符和权限管理
- `shellapi.h`：ZIP 文件解压

## 许可证

本项目为 ZASCA 平台的一部分，遵循项目主许可证。

## 相关链接

- **ZASCA 主项目**：[GitHub Repository](https://github.com/trustedinster/ZASCA)
- **uv 包管理器**：https://github.com/astral-sh/uv
- **清华大学开源软件镜像站**：https://mirrors.tuna.tsinghua.edu.cn/

---

**注意**：本工具专为 Windows 平台设计，不支持 Linux 或 macOS 系统。
