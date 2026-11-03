
$ErrorActionPreference = "Stop"
$src = Join-Path $PSScriptRoot "hover-dock.ps1"
if (-not (Test-Path $src)) { throw "hover-dock.ps1 not found" }

$dst = Join-Path $env:LOCALAPPDATA "HoverDock"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
$ps1 = Join-Path $dst "hover-dock.ps1"
$code = [System.IO.File]::ReadAllText($src, [System.Text.Encoding]::UTF8)
[System.IO.File]::WriteAllText($ps1, $code, (New-Object System.Text.UTF8Encoding($true)))
$csSrc = Join-Path $PSScriptRoot "ui-types.cs"
$csDst = Join-Path $dst "ui-types.cs"
Copy-Item -LiteralPath $csSrc -Destination $csDst -Force
$vbsPath = Join-Path $dst "start-dock.vbs"
$vbs = 'Set s = CreateObject("WScript.Shell")' + [Environment]::NewLine +
       's.Run "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""' + $ps1 + '""", 0, False'
Set-Content -LiteralPath $vbsPath -Value $vbs -Encoding ASCII

# stop old instance
$pidf = Join-Path $dst "dock.pid"
Get-Process powershell -ErrorAction SilentlyContinue | ForEach-Object {
  if ($_.Id -ne $PID) {
    try {
      $cl = (Get-CimInstance Win32_Process -Filter ("ProcessId=" + $_.Id)).CommandLine
      if ($cl -like ("*" + $ps1 + "*")) { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    } catch { }
  }
}
# wait until every old instance is really gone, otherwise two panels can end up on screen
$deadline = (Get-Date).AddSeconds(4)
do {
  $alive = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -like ("*" + $ps1 + "*") })
  if ($alive.Count -gt 0) { $alive | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
  Start-Sleep -Milliseconds 200
} while ($alive.Count -gt 0 -and (Get-Date) -lt $deadline)

# start
Start-Process wscript.exe -ArgumentList ('"' + $vbsPath + '"') -WindowStyle Hidden
Start-Sleep -Milliseconds 3500

# autostart entry
$startup = [Environment]::GetFolderPath("Startup")
$lnkPath = Join-Path $startup "Hover Dock.lnk"
$sh = New-Object -ComObject WScript.Shell
$lnk = $sh.CreateShortcut($lnkPath)
$lnk.TargetPath = "C:\Windows\System32\wscript.exe"
$lnk.Arguments = '"' + $vbsPath + '"'
$lnk.WorkingDirectory = $dst
$lnk.WindowStyle = 7
$lnk.Description = "Hover Dock - edge hover app launcher"
$lnk.Save()

"installed to: $dst"
"autostart: $lnkPath"
$running = Get-Process powershell -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $PID } | ForEach-Object { try { (Get-CimInstance Win32_Process -Filter ("ProcessId=" + $_.Id)).CommandLine } catch { } } | Where-Object { $_ -like ("*" + $ps1 + "*") }
"running instances: " + @($running).Count
if (Test-Path (Join-Path $dst "err.log")) { "stderr: " + (Get-Content (Join-Path $dst "err.log") -Raw) }