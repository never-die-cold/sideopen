# 架构与实现

## 1. 总体模型

一个 PowerShell 进程，一个 WinForms 消息循环，两个无边框顶层窗口：

```
            ┌──────────────────────────┐  ┌──────────────┐
  屏幕右缘   │        应用面板 fly        │  │  目录 rail    │
  6px 感应区 │  (MacPanel × 分类, 3~N 列) │  │ 分类行 × N    │
            └──────────────────────────┘  └──────────────┘
                     ↑ 悬停分类 → 切换内容        ↑ 贴边滑出
```

- **rail**：常驻，平时移出屏幕（`x = 屏宽`），贴边时滑到 `屏宽 - 宽 - 6`
- **fly**：默认移出屏幕，只在鼠标落到某个分类行时出现，出现在 rail 左侧
- 两者**右边缘与左边缘严丝合缝**：`fly.x + fly.w == rail.x`

为什么要零间隙：早期版本留了 9px 空隙，鼠标从目录横移到应用时会在空隙里"两边都不算"，
判定在开/合之间反复横跳，表现为面板一闪一闪。现在用「hot / rail / fly 三个矩形取并集」判定，
并且两个窗口紧贴，视觉上也连贯。

## 2. 状态机

挂在 `System.Windows.Forms.Timer`（70ms）上的单一轮询：

```
每 tick:
  if 正在拖拽:
       if 鼠标键已松开 → End-Drag     // 兜底，防丢捕获
       return
  inHot  = 光标在屏幕右缘 6px 内 且 Y 落在 rail 的纵向范围
  inRail = rail 已展开 且 光标在 rail 矩形内
  inFly  = fly 已展开 且 光标在 fly 矩形内

  if inHot || inRail || inFly:
       miss = 0
       确保 rail 展开
       if inRail: 根据 Y 算出分类索引 idx，变了就 Show-Fly(idx)
  else:
       miss++
       if miss >= 3: 收起 fly、收起 rail      // 3×70ms ≈ 210ms 容差
```

关键点：

- **只有位置变化，没有尺寸变化**。窗口从头到尾保持固定大小，展开/收起只改 `SetBounds` 的 X。
  改尺寸会触发整窗重绘，是可见闪烁的主要来源之一。
- **3 帧容差**：边界上偶尔的抖动不会立刻收起面板。
- **拖拽期间冻结**：拖拽时直接 return，避免面板在拖的过程中换分类或收起。

## 3. 渲染

### 3.1 自绘控件

`ui-types.cs` 里三个类：

| 类 | 职责 |
|---|---|
| `MacTile` | 一个应用格子：底板、悬停圆角高亮、44px 图标、下方标签 |
| `MacRow` | 一个分类行：常态 / 悬停 / 选中（蓝色实心）/ 拖拽目标（蓝色描边） |
| `Win32` | `SHGetFileInfo` 取图标、`SetWindowCompositionAttribute`（预留）、DPI 感知 |

圆角用 `GraphicsPath` 拼四段 `AddArc`，窗口圆角用 `Region` 裁剪。

### 3.2 踩过的坑：空白块

第一版四个自绘类都开了：

```csharp
SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
         ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
```

而 `OnPaint` **只画图标和文字，从不铺底色**。

`UserPaint + AllPaintingInWmPaint` 会让 Windows 跳过 `WM_ERASEBKGND`（原本负责"先铺背景"），
于是窗口只要发生局部重绘（切分类、被别的窗口遮挡再露出、改大小），没被画到的区域就保持"未初始化"，
直接透出后面的窗口——表现为一块块白色/空白，看起来像图标丢了。

修法：每个自绘类在 `OnPaint` 开头先 `FillRectangle(ClientRectangle)` 铺一层 `Base` 底色，
`MacPanel` / `MacForm` 用 `BackColor`。改完之后所有分类都稳定。

### 3.3 为什么没有毛玻璃

试过 DWM 的 Acrylic（`SetWindowCompositionAttribute` + `ACCENT_ENABLE_ACRYLICBLURBEHIND`）：
渐变色的 alpha 一调低（想透出模糊背景），自绘控件的区域就开始出现未重绘的色块；
调高则等于不透明。`Form.Opacity`（分层窗口）同样会让自绘子控件渲染错乱。

结论：WinForms 这条路上，**自绘 + 半透明/模糊不可靠**。想要真毛玻璃得换 WPF / WinUI。
所以现在是**不透明深灰 + 圆角 + 高对比排版**，视觉上仍然成立。

### 3.4 DPI

进程启动时 `[Win32]::SetProcessDPIAware()`，之后所有坐标都是物理像素。
布局里所有尺寸都过一遍 `U()`：

```powershell
$S = [double]([System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)).DpiX / 96.0
function U([double]$v) { [int][Math]::Round($v * $S) }
```

这样 150% 缩放的屏幕上不会糊、也不会出现半个像素的缝。

## 4. 布局计算

每个分类独立算列数，避免"一个分类条目多，所有分类都被撑成宽面板"：

```
rowsLimit = floor((屏高 × 0.84 - 标题高) / (行高 + 间距))      // 本机约 6 行
cols[i]   = max(3, ceil(条目数[i] / rowsLimit))
rows[i]   = ceil(条目数[i] / cols[i])
flyW[i]   = pad×2 + cols[i]×格子宽 + (cols[i]-1)×间距
flyH[i]   = 标题高 + rows[i]×(格子高+间距) - 间距 + pad
```

面板宽度/高度取当前分类自己的值，展开时按需 `SetBounds` + 重设 `Region`。

## 5. 图标

两级回退：

1. `[System.Drawing.Icon]::ExtractAssociatedIcon(target)`
   —— 对 `.lnk` 先解析目标（`WScript.Shell` 的 `CreateShortcut`），拿目标 exe 的图标
2. `SHGetFileInfo(path, SHGFI_ICON)`
   —— 文件夹、`.url`、以及第 1 步失败时的兜底

拿到的 32px 图标用 `HighQualityBicubic` 放大到 48px 再用。结果按文件路径缓存在哈希表里，
拖拽/切换分类不会重复提取。

## 6. 长按拖拽

### 6.1 事件链

`MacTile` 内置一个 420ms 的 `System.Windows.Forms.Timer`：

```
MouseDown  → 记录按下点，armed = true，启动计时器，Capture = true
Timer.Tick → 若仍 armed 且未进入拖拽：置 dragging，触发 LongPress
MouseMove  → 拖拽前若位移 > 5px：取消（保证普通点击/滑动不受影响）
             拖拽中：触发 DragMoveScreen
MouseUp    → 停表；若在拖拽：触发 DragRelease
```

PowerShell 侧订阅这三个事件：`Start-Drag` / `Update-Drag` / `End-Drag`。

### 6.2 幽灵窗口

长按后新建一个小 `MacForm`（无边框、置顶、圆角），里面塞一个只用于显示的 `MacTile`，
每次 `DragMove` 时把它的位置设成 `光标 - 半格`。真实格子本身不动。

### 6.3 落点判定

`End-Drag` 时用 `Row-At(光标)` 把 Y 换算成分类索引：

```
rel = cursorY - railY - 标题高
idx = floor(rel / 行高)      // 越界或 X 不在 rail 内 → -1
```

- `idx < 0` 或等于源分类 → 取消
- 否则 `Move-Item` 到目标分类文件夹；同名冲突自动加 `(2)`、`(3)` 后缀
- 移动完 `Rebuild` 重建六个面板，并把 fly 重新指向目标分类

### 6.4 兜底

主循环里：拖拽状态为真但 `Control.MouseButtons == None` → 强制结束拖拽。
防止焦点/鼠标捕获丢失时幽灵窗口永远跟着鼠标。

## 7. 性能

| 措施 | 原因 |
|---|---|
| 面板**预建**，切换只改 `Visible` | 早期版本每次切换都 `Controls.Clear()` 重建十几个控件，一帧白屏 |
| 图标缓存 | 避免切换分类时重复解析 |
| 70ms 轮询 | 只做几次矩形包含判断，CPU 占用接近 0 |
| 尺寸不变只改位置 | 避免整窗重绘带来的闪烁 |

## 8. 部署

`install.ps1` 做四件事：

1. 复制 `hover-dock.ps1` / `ui-types.cs` 到 `%LOCALAPPDATA%\HoverDock\`
2. 写 `start-dock.vbs`（`WScript.Shell.Run` + 窗口样式 0，避免控制台黑框）
3. 杀掉旧实例（按命令行匹配脚本路径）并启动新实例
4. 在「启动」目录建 `Hover Dock.lnk` 指向 `wscript.exe start-dock.vbs`

## 9. PowerShell 5.1 的两个坑

1. **编码**：Windows PowerShell 5.1 读 `.ps1` 时，若无 BOM 会按系统 ANSI（中文环境是 GBK）解析，
   文件里的中文全变乱码，甚至直接语法错误。所有含中文的 `.ps1` **必须带 UTF-8 BOM**。
   `install.ps1` 里复制时显式用 `UTF8Encoding($true)` 重写，就是为了保证这一点。
2. **事件作用域**：事件脚本块里读写脚本级变量必须显式 `$script:xxx`，
   否则赋值会落到函数局部，表现为"状态改了但下一帧又变回去"。
