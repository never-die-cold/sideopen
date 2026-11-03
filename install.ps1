param([string]$Source = '')

# SideOpen installer: copy the exe, stop the old instance, create the startup shortcut, launch.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Source) { $Source = Join-Path $here 'src\SideOpen.exe' }
if (-not (Test-Path -LiteralPath $Source)) { throw "SideOpen.exe not found at $Source - run src\build.bat first" }

$dst = Join-Path $env:LOCALAPPDATA 'SideOpen'
New-Item -ItemType Directory -Force -Path $dst | Out-Null

Get-Process SideOpen -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Copy-Item -LiteralPath $Source -Destination (Join-Path $dst 'SideOpen.exe') -Force

$startup = [Environment]::GetFolderPath('Startup')
Remove-Item (Join-Path $startup 'Hover Dock.lnk') -Force -ErrorAction SilentlyContinue
$sh = New-Object -ComObject WScript.Shell
$lnk = $sh.CreateShortcut((Join-Path $startup 'SideOpen.lnk'))
$lnk.TargetPath = Join-Path $dst 'SideOpen.exe'
$lnk.WorkingDirectory = $dst
$lnk.Description = 'SideOpen edge launcher'
$lnk.Save()

Start-Process -FilePath (Join-Path $dst 'SideOpen.exe')
Write-Output "installed to: $dst"
Write-Output "autostart: $(Join-Path $startup 'SideOpen.lnk')"