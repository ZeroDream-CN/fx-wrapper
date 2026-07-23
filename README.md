# FXWrapper

FiveM 服务端沙盒绕过补丁。通过运行时动态 Hook FXServer，解除 Lua / Node.js 资源沙箱对文件读写、子进程启动、系统命令等操作的限制，无需替换工件 DLL，兼容最新版本 FXServer。

## 功能

- 允许资源脚本读写服务器文件系统
- 支持 Lua `os.execute`、`os.remove`、`os.rename` 等系统调用
- 允许资源创建子进程与 Worker
- 解除 Node.js 脚本权限回调限制
- 子进程自动继承 Hook，整条进程链均生效
- 启动时自动检查更新（Windows）

## 原理简述

FXWrapper 由两部分组成：

| 组件 | 说明 |
|------|------|
| **FXWrapper** | 启动器，替代 `FXServer` 启动并注入 Hook 库 |
| **fx-hook** | Hook 库（Windows: `fx-hook.dll`，Linux: `libfx-hook.so`），拦截沙箱权限检查 |

Windows 使用 DLL 注入 + [MinHook](https://github.com/TsudaKageyu/minhook)；Linux 使用 `LD_PRELOAD` + [SubHook](https://github.com/Zeex/subhook)。

## 系统要求

- 64 位系统（x86-64 / amd64）
- **Windows**：Visual Studio 2022 或 CMake 3.20+
- **Linux**：musl 静态链接构建（见下方 Docker 构建脚本）

## 构建

### Windows

```powershell
.\scripts\build-windows-amd64.ps1
```

产物位于 `dist/windows-amd64/`：

- `FXWrapper.exe`
- `fx-hook.dll`

### Linux (musl)

```bash
./scripts/build-linux-musl.sh
```

产物位于 `dist/linux-musl-amd64/`：

- `FXWrapper`
- `libfx-hook.so`

## 使用

1. 下载或构建对应平台的 FXWrapper 与 Hook 库
2. 将文件放到 FXServer 工件目录（与 `FXServer.exe` / `FXServer` 同级）
3. 修改启动脚本，用 **FXWrapper** 替换 **FXServer**，参数原样透传

```bash
# 示例：原先
./FXServer +exec server.cfg

# 改为
./FXWrapper +exec server.cfg
```

> 若杀毒软件报毒，可忽略误报（存在 DLL 注入行为）。请仅在您拥有合法服务器管理权限的情况下使用。

预编译包与更新信息：[cfdx.zerodream.net/fivem/fxwrapper](https://cfdx.zerodream.net/fivem/fxwrapper/?v=0.0.0)

讨论帖：[GTAOS 论坛](https://forum.gtaos.com/threads/3907/)

## 官网

项目主页源码位于 `web/`，使用 Vue 3 + Vite + TailwindCSS 构建：

```bash
cd web
npm install
npm run dev      # 开发
npm run build    # 构建到 web/dist/
```

## 测试

```powershell
# Windows
.\scripts\test-win-server.ps1
```

```bash
# Linux
./scripts/test-linux-server.sh
```

测试资源位于 `test-resources/fx-sandbox-test/`。

## 目录结构

```
fx-wrapper/
├── src/                 # C++ 源码
│   ├── main.cpp         # 启动器入口
│   ├── hook/            # Hook 库逻辑
│   └── platform/        # 平台相关实现
├── cmake/               # 第三方依赖（MinHook / SubHook）
├── scripts/             # 构建与测试脚本
├── web/                 # 项目主页
├── test-resources/      # 沙箱测试资源
└── dist/                # 构建产物（gitignore）
```

## 第三方依赖

| 依赖 | 用途 | 许可证 |
|------|------|--------|
| [MinHook](https://github.com/TsudaKageyu/minhook) | Windows Hook 引擎 | BSD 2-Clause |
| [SubHook](https://github.com/Zeex/subhook) | Linux Hook 引擎 | BSD 2-Clause |
| [Vue](https://vuejs.org/) | 官网前端 | MIT |
| [Tailwind CSS](https://tailwindcss.com/) | 官网样式 | MIT |
| [Lucide](https://lucide.dev/) | 官网图标 | ISC |

## 许可证

本项目采用 [GNU General Public License v3.0](LICENSE) 发布。

## 免责声明

本工具通过运行时 Hook 解除 FiveM 服务端沙盒限制。因使用本工具产生的任何后果由使用者自行承担；请遵守当地法律法规及 FiveM 服务条款，仅在合法、必要的场景下使用。
