# SideOpen

> 一个贴在屏幕右边缘的悬浮应用面板：鼠标碰到边缘就滑出来，划到分类上立刻展开该分类的应用，点一下启动。
> 纯 PowerShell + WinForms，零第三方依赖，不联网。

## 它解决什么问题

桌面被几十个快捷方式占满，建文件夹又要点两层才能打开程序。
SideOpen 把「桌面分类文件夹」变成屏幕边缘的一块悬浮面板——**碰一下边缘就到，分类悬停即展开，点完即走**。

## 特性

- **边缘触发**：光标贴住屏幕右缘 6px，面板滑出；移开自动收起
- **目录在右、应用在左**：右侧是分类列表，悬停分类 → 左侧展开该分类的应用网格，两端无缝拼接
- **自动列数**：条目多的分类自动多开列，保证不超屏高
- **系统图标**：读快捷方式目标的真实图标（含文件夹图标），没有图标也能优雅降级
- **长按拖拽**：按住格子 0.4 秒拖到别的分类行，松手即把快捷方式移动过去
- **零依赖 / 零安装**：PowerShell 5.1 + WinForms，不需要 .NET SDK、不需要 npm
- **开机自启**：装一次，常驻

## 环境

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 |
| PowerShell | 5.1（系统自带） |
| 运行时 | .NET Framework 4.x（WinForms） |

## 安装

```powershell
git clone https://github.com/never-die-cold/sideopen.git
cd sideopen
powershell -NoProfile -ExecutionPolicy Bypass -File .\src\install.ps1
```

安装脚本会：

1. 把 ``src/hover-dock.ps1`、`src/ui-types.cs` 复制到 `%LOCALAPPDATA%\HoverDock\`
2. 生成一个隐藏启动器 `start-dock.vbs`（避免黑框一闪）
3. 杀掉旧实例、启动新实例
4. 在「启动」目录放一个 `Hover Dock.lnk` 实现开机自启

卸载：删掉 `%LOCALAPPDATA%\HoverDock\` 和启动目录里的 `Hover Dock.lnk` 即可。

## 分类怎么来

默认读你**桌面**下的文件夹，凡是名字形如 `01_名字`、`02_名字` 的，就当成一个分类：

```
桌面/
├─ 01_开发工具/     ← 面板里的第一个分类
├─ 02_EDA与硬件/
├─ 03_学习与办公/
└─ ...
```

往文件夹里丢快捷方式，面板就跟着变（切一次分类或重启面板刷新）。分类名、数量都不限。
文件夹里的子文件夹也会显示成可点击的条目。

想换数据目录（比如不放在桌面）：

```powershell
powershell -File .\src\hover-dock.ps1 -Root "D:\MyLauncher"
```

## 使用

| 操作 | 效果 |
|---|---|
| 光标贴屏幕最右缘 | 右侧目录滑出 |
| 鼠标划过分类 | 左侧展开该分类的应用 |
| 单击图标 | 启动程序 / 打开文件夹 |
| 按住 0.4 秒后拖动 | 拖到别的分类行，松手即移动快捷方式 |
| 鼠标移开 | 约 200ms 后收起 |
| 右键面板 | 刷新列表 / 打开桌面文件夹 / 退出（由宿主脚本控制） |

## 参数

在 `src/hover-dock.ps1` 顶部：

| 变量 | 默认 | 说明 |
|---|---|---|
| `$railW` | `U 238` | 右侧目录宽度 |
| `$rowH` | `U 50` | 分类行高 |
| `$tileW` / `$tileH` | `U 106` / `U 120` | 应用格子尺寸 |
| `$bgDark` | `30,30,33` | 面板底色 |
| `$accent` | `10,132,255` | 选中蓝 |
| 感应区宽度 | `U 6` | 见 `$hotX` / `$railX` |

`U n` 是 DPI 缩放函数：`n × (当前 DPI / 96)`。

## 已知限制

- 只贴**右边缘**，且只作用于**主屏**（多屏需要改几何计算）
- 不透明深灰而非毛玻璃：DWM 的 Acrylic 与 WinForms 自绘控件冲突，会掉灰块（详见架构文档）
- 冷启动约 1–2 秒（要解析几十个图标）
- 常驻内存约 60–90 MB（PowerShell 宿主本身占大头）

## 架构

实现细节、状态机、踩过的坑都在 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：

- 两个无边框置顶窗口（目录 / 应用）如何拼成一块
- 命中测试为什么必须让两块"零间隙"
- `UserPaint + AllPaintingInWmPaint` 导致的空白块 bug
- 长按拖拽的事件链与兜底
- 图标提取的两级回退

## 项目结构

```
sideopen/
├─ src/
│  ├─ hover-dock.ps1   主程序：窗口、布局、状态机、拖拽
│  ├─ ui-types.cs      自绘控件（MacTile / MacRow / Win32 封装）
│  └─ install.ps1      安装 / 部署 / 开机自启
├─ docs/
│  └─ ARCHITECTURE.md  架构与实现说明
└─ README.md
```

## 隐私

SideOpen 只在本地读写你自己指定的桌面/分类文件夹，不上传、不联网、不收集任何信息。
仓库里不包含任何个人数据、路径或截图。

## License

暂未指定。需要的话可以补 MIT。
