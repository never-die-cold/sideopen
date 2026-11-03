
param(
  [switch]$Demo,
  [int]$DemoSeconds = 12,
  [int]$DemoCat = 0,
  [string]$Root = ""
)

$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$logf = Join-Path $here "dock.log"
function Log($m) { try { Add-Content -LiteralPath $logf -Value ((Get-Date).ToString("HH:mm:ss.fff") + "  " + $m) } catch { } }
Remove-Item $logf -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$cs = [System.IO.File]::ReadAllText((Join-Path $here "ui-types.cs"), [System.Text.Encoding]::UTF8)
Add-Type -TypeDefinition $cs -ReferencedAssemblies System.Windows.Forms, System.Drawing

Add-Type -TypeDefinition @"
using System.Drawing;
using System.Windows.Forms;
public class MacPanel : Panel {
  public MacPanel() { SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true); }
  protected override void OnPaint(PaintEventArgs e) { using (SolidBrush b = new SolidBrush(BackColor)) e.Graphics.FillRectangle(b, ClientRectangle); }
}
public class MacForm : Form {
  public MacForm() { SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true); }
  protected override void OnPaint(PaintEventArgs e) { using (SolidBrush b = new SolidBrush(BackColor)) e.Graphics.FillRectangle(b, ClientRectangle); }
}
"@ -ReferencedAssemblies System.Windows.Forms, System.Drawing

[Win32]::SetProcessDPIAware() | Out-Null

if (-not $Root) { $Root = [Environment]::GetFolderPath("Desktop") }
if (-not (Test-Path -LiteralPath $Root)) { throw "desktop folder not found: $Root" }
$root = $Root
$scr  = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$S = 1.5
try { $S = [double]([System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)).DpiX / 96.0 } catch { }
if ($S -lt 1) { $S = 1 }
$S = [Math]::Round($S, 2)
function U([double]$v) { [int][Math]::Round($v * $S) }

$cats = @(Get-ChildItem -LiteralPath $root -Directory -Force | Where-Object { $_.Name -match "^\d\d_" } | Sort-Object Name)
if (-not $cats) { throw "no category folders" }

# ---------- metrics ----------
$railW   = U 238
$rowH    = U 50
$headH   = U 46
$railH   = $headH + ($cats.Count * $rowH) + (U 14)
$tileW   = U 106
$tileH   = U 120
$gap     = U 8
$flyPad  = U 18

$railX = $scr.Width - $railW - (U 6)
$railY = [int](($scr.Height - $railH) / 2)
$hotX  = $scr.Width - (U 6)

$bgDark   = [System.Drawing.Color]::FromArgb(30, 30, 33)
$bg2      = [System.Drawing.Color]::FromArgb(43, 47, 56)
$fg       = [System.Drawing.Color]::FromArgb(232, 234, 240)
$dim      = [System.Drawing.Color]::FromArgb(148, 152, 163)
$accent   = [System.Drawing.Color]::FromArgb(10, 132, 255)

function Set-Round([System.Windows.Forms.Form]$f, [int]$rad) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $r = New-Object System.Drawing.Rectangle 0, 0, $f.Width, $f.Height
  $d = $rad * 2
  if ($d -gt $f.Width) { $d = $f.Width }
  if ($d -gt $f.Height) { $d = $f.Height }
  $p.AddArc($r.X, $r.Y, $d, $d, 180, 90)
  $p.AddArc(($r.Right - $d), $r.Y, $d, $d, 270, 90)
  $p.AddArc(($r.Right - $d), ($r.Bottom - $d), $d, $d, 0, 90)
  $p.AddArc($r.X, ($r.Bottom - $d), $d, $d, 90, 90)
  $p.CloseFigure()
  $f.Region = New-Object System.Drawing.Region $p
}

function New-Form([int]$w, [int]$h, [int]$x) {
  $f = New-Object MacForm
  $f.FormBorderStyle = "None"
  $f.StartPosition = "Manual"
  $f.TopMost = $true
  $f.ShowInTaskbar = $false
  $f.BackColor = $bgDark
  $f.SetBounds($x, 0, $w, $h)
  return $f
}

# ---------- rail ----------
$rail = New-Form $railW $railH ($scr.Width)
$rail.Name = "rail"
Set-Round $rail (U 18)

$railTitle = New-Object System.Windows.Forms.Label
$railTitle.Text = "应用"
$railTitle.ForeColor = $dim
$railTitle.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 9, [System.Drawing.FontStyle]::Bold)
$railTitle.BackColor = [System.Drawing.Color]::Transparent
$railTitle.SetBounds((U 22), (U 14), $railW, (U 22))
$rail.Controls.Add($railTitle) | Out-Null

$rows = @()
for ($i = 0; $i -lt $cats.Count; $i++) {
  $r = New-Object MacRow
  $r.Text = $cats[$i].Name
  $r.Base = $bgDark
  $r.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 10.5)
  $r.SetBounds(0, ($headH + ($i * $rowH)), $railW, $rowH)
  $rail.Controls.Add($r) | Out-Null
  $rows += $r
}

# ---------- flyout ----------
$fly = New-Form (U 400) (U 400) ($scr.Width)
$fly.Name = "fly"
Set-Round $fly (U 20)

$iconCache = @{}
function Get-Icon([string]$path, [bool]$isDir) {
  if ($iconCache.ContainsKey($path)) { return $iconCache[$path] }
  $img = $null
  try {
    if ($isDir) {
      $ico = [Win32]::GetShellIcon($path)
      if (-not $ico) { $ico = [Win32]::GetShellIcon("C:\\Windows") }
      if (-not $ico) { throw "no folder icon" }
      $bmp = $ico.ToBitmap()
    } else {
      $src = $path
      if ($path -like "*.lnk") {
        $sh = New-Object -ComObject WScript.Shell
        $t = $sh.CreateShortcut($path).TargetPath
        if ($t -and (Test-Path -LiteralPath $t)) { $src = $t }
      }
      $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($src)
      if (-not $ico) { $ico = [Win32]::GetShellIcon($path) }
      if (-not $ico) { throw "no icon" }
      $bmp = $ico.ToBitmap()
    }
    $sz = U 48
    $big = New-Object System.Drawing.Bitmap $sz, $sz
    $g = [System.Drawing.Graphics]::FromImage($big)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($bmp, 0, 0, $sz, $sz)
    $g.Dispose()
    $img = $big
  } catch { }
  $iconCache[$path] = $img
  return $img
}

$script:panels = @()
$script:colsFor = @()
$script:rowsFor = @()
$script:flyWs = @()
$script:flyHs = @()
$script:items = @{}

function Rebuild {
  foreach ($p in $script:panels) {
    try { $fly.Controls.Remove($p); $p.Dispose() } catch { }
  }
  $script:panels = @()

  foreach ($c in $cats) {
    $script:items[$c.FullName] = @(Get-ChildItem -LiteralPath $c.FullName -Force | Sort-Object @{Expression={$_.PSIsContainer};Descending=$true}, Name)
  }

  $rowsLimit = [int][Math]::Floor((($scr.Height * 0.84) - $headH - (U 24)) / ($tileH + $gap))
  if ($rowsLimit -lt 3) { $rowsLimit = 3 }

  $script:colsFor = @()
  $script:rowsFor = @()
  $script:flyWs = @()
  $script:flyHs = @()
  for ($i = 0; $i -lt $cats.Count; $i++) {
    $n = [Math]::Max($script:items[$cats[$i].FullName].Count, 1)
    $cc = [int][Math]::Ceiling($n / $rowsLimit)
    if ($cc -lt 3) { $cc = 3 }
    $rr = [int][Math]::Ceiling($n / $cc)
    $script:colsFor += $cc
    $script:rowsFor += $rr
    $script:flyWs += (($flyPad * 2) + ($cc * $tileW) + (($cc - 1) * $gap))
    $script:flyHs += ($headH + ($rr * ($tileH + $gap)) - $gap + (U 14))
    $rows[$i].Text = $cats[$i].Name + "   " + $script:items[$cats[$i].FullName].Count
  }

  for ($i = 0; $i -lt $cats.Count; $i++) {
    $w = $script:flyWs[$i]
    $h = $script:flyHs[$i]
    $p = New-Object MacPanel
    $p.SetBounds(0, 0, $w, $h)
    $p.BackColor = $bgDark
    $p.Visible = $false

    $hd = New-Object System.Windows.Forms.Label
    $hd.Text = "  " + $cats[$i].Name
    $hd.ForeColor = $dim
    $hd.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 9, [System.Drawing.FontStyle]::Bold)
    $hd.BackColor = [System.Drawing.Color]::Transparent
    $hd.SetBounds($flyPad, (U 14), ($w - $flyPad), (U 22))
    $p.Controls.Add($hd) | Out-Null

    $list = $script:items[$cats[$i].FullName]
    for ($k = 0; $k -lt $list.Count; $k++) {
      $it = $list[$k]
      $col = $k % $script:colsFor[$i]
      $row = [Math]::Floor($k / $script:colsFor[$i])
      $t = New-Object MacTile
      $label = $it.Name
      if (-not $it.PSIsContainer) { $label = [System.IO.Path]::GetFileNameWithoutExtension($it.Name) }
      $t.Text = $label
      $t.Base = $bgDark
      $t.ForeColor = $fg
      $t.Font = New-Object System.Drawing.Font("Microsoft YaHei UI", 8.5)
      $t.SetBounds(($flyPad + $col * ($tileW + $gap)), ($headH + $row * ($tileH + $gap)), $tileW, $tileH)
      $t.Payload = $it.FullName
      $t.Tag = $i
      $ic = Get-Icon $it.FullName $it.PSIsContainer
      if ($ic) { $t.Icon = $ic }
      $t.Add_Click({ param($s, $e) if ($s.WasLongPress) { return }; try { Start-Process -FilePath $s.Payload } catch { } })
      $t.Add_LongPress({ param($s, $e) Start-Drag $s })
      $t.Add_DragMoveScreen({ param($s, $e) Update-Drag })
      $t.Add_DragRelease({ param($s, $e) End-Drag })
      $p.Controls.Add($t) | Out-Null
    }
    $fly.Controls.Add($p) | Out-Null
    $script:panels += $p
  }
}

# ---------- state ----------
$script:railOn = $false
$script:flyOn  = $false
$script:cur    = -1
$script:miss   = 0
$script:dragging = $false
$script:ghost = $null
$script:fx = 0; $script:fy = 0; $script:fw = 0; $script:fh = 0

function Show-Rail {
  if ($script:railOn) { return }
  $script:railOn = $true
  $rail.SetBounds($railX, $railY, $railW, $railH)
}
function Hide-Rail {
  if (-not $script:railOn) { return }
  $script:railOn = $false
  $script:cur = -1
  $rail.SetBounds(($scr.Width), $railY, $railW, $railH)
}
function Show-Fly([int]$idx) {
  if ($idx -eq $script:cur -and $script:flyOn) { return }
  $script:cur = $idx
  for ($i = 0; $i -lt $script:panels.Count; $i++) {
    $script:panels[$i].Visible = ($i -eq $idx)
    $rows[$i].Active = ($i -eq $idx)
    $rows[$i].Invalidate()
  }
  $fw = $script:flyWs[$idx]
  $fh = $script:flyHs[$idx]
  $fy = $railY + $headH + ($idx * $rowH) - (U 10)
  if ($fy -lt 0) { $fy = 0 }
  if (($fy + $fh) -gt $scr.Height) { $fy = $scr.Height - $fh }
  $fx = $railX - $fw
  $fly.SuspendLayout()
  $fly.SetBounds($fx, $fy, $fw, $fh)
  Set-Round $fly (U 20)
  $fly.ResumeLayout()
  $script:fx = $fx; $script:fy = $fy; $script:fw = $fw; $script:fh = $fh
  $script:flyOn = $true
}
function Hide-Fly {
  if (-not $script:flyOn) { return }
  $script:flyOn = $false
  $script:cur = -1
  $fly.SetBounds($scr.Width, $railY, $script:fw, $script:fh)
  for ($i = 0; $i -lt $rows.Count; $i++) { $rows[$i].Active = $false; $rows[$i].Invalidate() }
}

function Row-At([System.Drawing.Point]$pt) {
  if ($pt.X -lt $railX -or $pt.X -gt ($railX + $railW)) { return -1 }
  $rel = $pt.Y - $railY - $headH
  if ($rel -lt 0) { return -1 }
  $i = [int][Math]::Floor($rel / $rowH)
  if ($i -ge 0 -and $i -lt $cats.Count) { return $i }
  return -1
}

function Clear-DropHighlight {
  for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($rows[$i].DropTarget) { $rows[$i].DropTarget = $false; $rows[$i].Invalidate() }
  }
}

# ---------- drag ----------
function Start-Drag($tile) {
  if (-not $script:flyOn) { return }
  $script:dragging = $true
  $script:dragPath = $tile.Payload
  $script:dragText = $tile.Text
  $script:dragIndex = [int]$tile.Tag

  $g = New-Object MacForm
  $g.FormBorderStyle = "None"
  $g.StartPosition = "Manual"
  $g.TopMost = $true
  $g.ShowInTaskbar = $false
  $g.BackColor = $bg2
  $g.SetBounds(0, 0, $tileW, $tileH)
  Set-Round $g (U 12)

  $gt = New-Object MacTile
  $gt.Text = $tile.Text
  $gt.Base = $bg2
  $gt.ForeColor = $fg
  $gt.Font = $tile.Font
  $gt.Icon = $tile.Icon
  $gt.SetBounds(0, 0, $tileW, $tileH)
  $g.Controls.Add($gt) | Out-Null

  $g.Show()
  $script:ghost = $g
  Log ("drag start: " + $script:dragPath)
  Update-Drag
}

function Update-Drag {
  if (-not $script:dragging -or -not $script:ghost) { return }
  $p = [System.Windows.Forms.Cursor]::Position
  $gx = $p.X - [int]($tileW / 2)
  $gy = $p.Y - [int]($tileH / 2)
  $script:ghost.SetBounds($gx, $gy, $tileW, $tileH)

  $idx = Row-At $p
  for ($i = 0; $i -lt $rows.Count; $i++) {
    $want = ($i -eq $idx -and $i -ne $script:dragIndex)
    if ($rows[$i].DropTarget -ne $want) { $rows[$i].DropTarget = $want; $rows[$i].Invalidate() }
  }
}

function End-Drag {
  if (-not $script:dragging) { return }
  $p = [System.Windows.Forms.Cursor]::Position
  $idx = Row-At $p
  $script:dragging = $false
  Clear-DropHighlight
  if ($script:ghost) { try { $script:ghost.Close() } catch { }; $script:ghost = $null }

  $src = $script:dragIndex
  $path = $script:dragPath
  if ($idx -lt 0 -or $idx -eq $src -or -not $path) {
    $script:cur = -1
    Show-Fly $src
    return
  }

  $name = Split-Path -Leaf $path
  $destDir = $cats[$idx].FullName
  $dest = Join-Path $destDir $name
  if (Test-Path -LiteralPath $dest) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($name)
    $ext = [System.IO.Path]::GetExtension($name)
    for ($n = 2; $n -lt 100; $n++) {
      $cand = Join-Path $destDir ($base + " (" + $n + ")" + $ext)
      if (-not (Test-Path -LiteralPath $cand)) { $dest = $cand; break }
    }
  }
  try {
    Move-Item -LiteralPath $path -Destination $dest -Force
    Log ("moved " + $name + " -> " + $cats[$idx].Name)
    Rebuild
    $script:cur = -1
    $script:dragging = $false
    Show-Fly $idx
  } catch {
    Log ("move failed: " + $_.Exception.Message)
    $script:cur = -1
    Show-Fly $src
  }
}

# ---------- build ----------
Rebuild
$rail.Show()
$fly.Show()

if ($Demo) {
  Show-Rail
  Show-Fly $DemoCat
  $script:demoTimer = New-Object System.Windows.Forms.Timer
  $script:demoTimer.Interval = [Math]::Max(1000, ($DemoSeconds * 1000))
  $script:demoTimer.Add_Tick({ $script:demoTimer.Stop(); $rail.Close() })
  $script:demoTimer.Start()
  [System.Windows.Forms.Application]::Run($rail)
  return
}

# ---------- hover loop ----------
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 70
$timer.Add_Tick({
  if ($script:dragging) {
    if ([System.Windows.Forms.Control]::MouseButtons -eq [System.Windows.Forms.MouseButtons]::None) { End-Drag }
    return
  }
  $p = [System.Windows.Forms.Cursor]::Position
  $inHot = ($p.X -ge $hotX) -and ($p.Y -ge $railY) -and ($p.Y -le ($railY + $railH))
  $inRail = $script:railOn -and ($p.X -ge $railX) -and ($p.X -lt ($railX + $railW + (U 6))) -and ($p.Y -ge $railY) -and ($p.Y -le ($railY + $railH))
  $inFly = $script:flyOn -and ($p.X -ge $script:fx) -and ($p.X -lt $railX) -and ($p.Y -ge $script:fy) -and ($p.Y -le ($script:fy + $script:fh))

  if ($inHot -or $inRail -or $inFly) {
    $script:miss = 0
    if (-not $script:railOn) { Show-Rail }
    if ($inRail) {
      $rel = $p.Y - $railY - $headH
      if ($rel -ge 0) {
        $idx = [int][Math]::Floor($rel / $rowH)
        if ($idx -ge 0 -and $idx -lt $cats.Count -and $idx -ne $script:cur) { Show-Fly $idx }
      }
    }
  } else {
    $script:miss++
    if ($script:miss -ge 3) {
      Hide-Fly
      Hide-Rail
    }
  }
})
$timer.Start()

[System.Windows.Forms.Application]::Run($rail)