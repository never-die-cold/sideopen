# 架构（原生实现）

## 1. 进程模型

单进程、单线程、一个消息循环。没有后台工作线程：所有交互都在 60ms 定时器里完成。

```
wWinMain
├─ SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)
├─ CoInitializeEx(APARTMENTTHREADED)        // 取图标要用 Shell COM
├─ GdiplusStartup
├─ CreateMutex("SideOpen.SingleInstance")   // 单实例
├─ 读 config.ini → 扫描分类目录 → 算列数
├─ 注册窗口类 / 创建面板窗口（隐藏）/ 托盘 / 全局热键
├─ SetTimer(60ms)
└─ GetMessage 循环
```

## 2. 窗口

| 窗口 | 样式 | 用途 |
|---|---|---|
| rail | LAYERED + TOOLWINDOW + TOPMOST + NOACTIVATE | 分类栏 |
| fly | 同上 | 应用网格 |
| ghost | 同上 | 拖拽时跟随鼠标的"幽灵" |
| search | TOOLWINDOW + TOPMOST（可激活） | 搜索框，需要键盘焦点 |
| msgw | HWND_MESSAGE | 托盘回调 / 定时器 / 热键 |

面板全是 `WS_POPUP` + 分层，无标题栏、不进任务栏、不抢焦点。

## 3. 渲染管线

### 3.1 表面

```cpp
CreateDIBSection(32bpp, BI_RGB, top-down)           // 像素缓冲
new Bitmap(w, h, w*4, PixelFormat32bppPARGB, bits)  // GDI+ 直接画进这块内存
new Graphics(bitmap)                                // 抗锯齿 + 文字
```

画完 `Graphics::Flush(FlushIntentionSync)`，然后：

```cpp
UpdateLayeredWindow(hwnd, screenDC, &dst, &size, memDC, &src, 0, &blend, ULW_ALPHA)
```

`dst` 同时决定窗口位置，所以"展开/收起"只改 `dst`、不改尺寸 —— 这是不闪的根本原因。

### 3.2 为什么必须 PARGB

`UpdateLayeredWindow + AC_SRC_ALPHA` 要求**预乘 alpha**。GDI+ 在 `PixelFormat32bppPARGB` 上绘制时自动预乘，
半透明背景、圆角、文字混在一起结果才正确。误用 `PixelFormat32bppARGB` 会出现黑边。

### 3.3 圆角

`GraphicsPath` 拼四段 `AddArc`。窗口是矩形，但四角 alpha=0，所以视觉上是抗锯齿圆角。
比 `SetWindowRgn` 好：后者没有抗锯齿。

## 4. 交互状态机

**不使用 WM_MOUSEMOVE / WM_LBUTTONDOWN**。面板是 `WS_EX_NOACTIVATE`，焦点行为不可靠，
而且拖拽时光标可能在窗口外。所以统一在 `Tick()` 里轮询：

```
每 60ms:
  p = GetCursorPos();  down = GetAsyncKeyState(VK_LBUTTON)

  if hideFullscreen && FullscreenActive(): 收起; return

  inHot  = PtInRect(hotRect, p)
  inRail = railOn && PtInRect(railRect, p)
  inFly  = flyOn  && PtInRect(flyRect, p)

  if down && !prevDown: 记录按下点 + 候选 tile
  if 按住 <420ms 且位移 <5px: 继续等
  if 按住 >420ms 且未移动: 进入拖拽（显示 ghost）
  if !down && prevDown: 未拖拽 → 点击（启动 / 切分类）

  if inHot || inRail || inFly:
      miss = 0; 确保 rail 显示
      if inRail: 由 Y 算分类行，变了就 ShowFly(row)
  else:
      if ++miss >= 3: 收起
```

**3 帧容差** ≈180ms，光标在边界抖动时不会闪。

## 5. 几何

`ComputeRailGeom()` 按 `edge` 算分类栏 + 热区；`ComputeFlyGeom(idx)` 算应用面板。

- right：分类栏贴右，面板在左
- left：反过来
- top / bottom：分类栏居中贴边，面板在侧边

面板高度按**当前分类自己的行数**算，不是全局最大值。

## 6. 图标

```cpp
SHCreateItemFromParsingName(lnk) → IShellItemImageFactory::GetImage(256, ICONONLY|BIGGERSIZEOK)
```

失败回退 `SHGetFileInfo(SHGFI_ICON | SHGFI_LARGEICON)`。按路径缓存在 `std::map<wstring, HBITMAP>`。

取 256px 再缩到 44 逻辑像素，比 32px 放大清晰得多。

## 7. 拖拽

- 长按判定在 `Tick`（时间 + 位移），不依赖窗口消息
- ghost 是独立分层窗口，跟随光标
- 松手 `RowAt(cursor)` 判断落点分类；同分类 → 取消
- `MoveFileW` 移动，同名生成 `name (2).lnk`
- 之后清图标缓存、重扫目录、重算几何、重绘

## 8. 搜索

`Ctrl+Alt+Space` 用 `RegisterHotKey` 注册在 msgw 上。
搜索窗口是唯一**可激活**的窗口，`SetForegroundWindow + SetFocus` 后才收得到 `WM_CHAR`。

输入 → `StrStrIW` 匹配全部 label → 最多 8 条 → ↑↓ 选择 → Enter 启动 → Esc 关闭。

## 9. 全屏检测

`SHQueryUserNotificationState` 返回 `QUNS_RUNNING_D3D_FULL_SCREEN / PRESENTATION_MODE / BUSY` 时收起。

## 10. 与 PowerShell 版对比

| 项 | PowerShell + WinForms | 原生 C++ |
|---|---|---|
| 常驻内存 | ~130 MB | ~16 MB 空闲 / ~45 MB 图标全载 |
| 冷启动 | 1–2 s | < 100 ms |
| 依赖 | PowerShell 5.1 + .NET Framework | 无 |
| 透明 | 整窗 Opacity（自绘控件会画错） | 逐像素 alpha + 抗锯齿圆角 |
| 闪烁 | 靠预建面板/不改尺寸规避 | 天然没有：整帧一次提交 |
