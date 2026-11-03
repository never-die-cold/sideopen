# SideOpen

> 贴在屏幕边缘的悬浮应用面板。鼠标碰到边缘就滑出来，划到分类上立刻展开该分类的应用，点一下启动。
> **原生 C++ / Win32 + GDI+**，单个 exe，零运行时依赖，空闲约 16 MB 内存。

## 特性

| 能力 | 说明 |
|---|---|
| 边缘触发 | 光标贴住屏幕边缘的 6px 热区，面板滑出；移开 200ms 自动收起 |
| 目录 + 应用二级面板 | 右侧分类列表，悬停分类 → 左侧展开应用网格，两块无缝拼接 |
| 自动列数 | 条目多的分类自动多开列，保证不超屏高 |
| 四边停靠 | `edge = right / left / top / bottom`，面板贴着对应边 |
| 长按拖拽 | 按住 400ms 拖到别的分类行，松手即把快捷方式移动过去（同名自动加后缀） |
| 快速搜索 | `Ctrl+Alt+Space` 唤出搜索框，输入即过滤全部分类，↑↓ 选择，回车启动，Esc 关闭 |
| 全屏自动隐藏 | 检测到全屏应用/演示模式时自动隐身，退出即恢复 |
| 系统托盘 | 显示/隐藏、打开配置、重新加载、退出 |
| 单实例 | 命名互斥量，重复启动直接退出 |
| 高分辨率图标 | 走 `IShellItemImageFactory` 取 256px 图标，再缩放到面板尺寸 |
| 可配置 | `%LOCALAPPDATA%\SideOpen\config.ini`，首次运行自动生成 |
| 开机自启 | 安装脚本创建启动项，可在托盘退出 |

## 环境与构建

需要 **Visual Studio 2022/2026（含 C++ 工具链）+ Windows 10 SDK**。

```bat
cd src
build.bat
```

产物：`src\SideOpen.exe`（约 220 KB，静态链接 C 运行库之外无外部依赖）。

## 安装

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

做的事：复制 exe 到 `%LOCALAPPDATA%\SideOpen\` → 结束旧实例 → 建启动项 `SideOpen.lnk` → 启动。

卸载：删掉该目录 + 启动项里的 `SideOpen.lnk`。


## 视觉

macOS 风格深色材质：

- 面板是竖向渐变 + 半透明的材质，不是纯色块
- 外圈一圈柔和投影（分层窗口里用 20 层递减 alpha 的圆角矩形堆出来）
- 1px 内高光 + 1px 外描边，模拟 macOS 的 rim light
- 分类栏行高 40、圆角 7 的胶囊选中态，右侧对齐条目计数
- 应用网格悬停时出现圆角底板 + 图标轻微上浮

## 数据来源

读桌面（或 `config.ini` 里 `root` 指定的目录）下所有形如 `01_xxx` 的文件夹，一个文件夹 = 一个分类：

```
桌面/
├─ 01_开发工具/     ← 面板第一个分类
├─ 02_EDA与硬件/
└─ ...
```

往文件夹里丢/删快捷方式，面板跟着变（切一次分类或托盘「重新加载」）。

## 配置

`%LOCALAPPDATA%\SideOpen\config.ini`：

| 键 | 默认 | 说明 |
|---|---|---|
| `edge` | `right` | 停靠边：right / left / top / bottom |
| `root` | 桌面 | 分类文件夹所在目录，留空 = 系统桌面 |
| `railWidth` | 236 | 分类栏宽度（逻辑像素，自动按 DPI 缩放） |
| `rowHeight` | 50 | 分类行高 |
| `tileW` / `tileH` | 106 / 120 | 应用格子尺寸 |
| `hotZone` | 6 | 边缘热区宽度 |
| `minCols` | 3 | 最少列数 |
| `opacity` | 238 | 面板不透明度 0–255 |
| `bg` / `fg` / `dim` / `accent` | 1E1E21 / E8EAF0 / 949AA3 / 0A84FF | 颜色（RRGGBB） |
| `hideFullscreen` | 1 | 全屏时自动隐藏 |
| `iconSize` | 44 | 图标显示尺寸 |

改完重启程序生效（或托盘「重新加载」只重载分类）。

## 命令行参数

| 参数 | 用途 |
|---|---|
| `/show` | 启动后立即展开面板（截图、调试） |
| `/pin` | 强制常开，忽略鼠标位置（调试用） |

## 实现要点

见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。简单说：

- **分层窗口**（`WS_EX_LAYERED` + `UpdateLayeredWindow`）做真正的逐像素透明和抗锯齿圆角，不需要 DWM 魔法
- 每帧画进 32bpp **DIB**（GDI+ 的 `PixelFormat32bppPARGB` 表面），然后一次性提交换
- 交互**不用窗口消息**：一个 60ms 定时器读 `GetCursorPos` / `GetAsyncKeyState`，状态机在纯数据上跑，避免 NOACTIVATE 窗口收不到鼠标事件的坑
- 图标走 Shell 的 256px 取图接口，内存里按路径缓存

## 旧版（已归档）

最初的版本是 PowerShell + WinForms 实现的，代码保留在 [legacy/powershell](legacy/powershell)，
架构说明见 [docs/ARCHITECTURE-powershell.md](docs/ARCHITECTURE-powershell.md)。
它功能等价但常驻内存约 130 MB、冷启动 1–2 秒，已被原生版取代。

## License

暂未指定。
