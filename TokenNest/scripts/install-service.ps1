# scripts/install-service.ps1
# 用 NSSM 把 TokenNest 装为 Windows 服务，开机自启
# 前置：手动下载 NSSM (https://nssm.cc) 并把 nssm.exe 放到 PATH 或 C:\nssm\

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Root = Resolve-Path "$ScriptDir\.."
Set-Location $Root

$ServiceName = 'TokenNest'

if (-not (Test-Path node_modules)) {
    Write-Host "[install] installing deps..."
    npm install
}

$NodePath = (Get-Command node.exe).Source
$StartCmd = "`"$NodePath`" `"$Root\src\server.js`""
$LogDir = Join-Path $Root 'logs'
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }

Write-Host "[install] installing service '$ServiceName'..."
& nssm install $ServiceName $NodePath "$Root\src\server.js"
& nssm set $ServiceName AppDirectory $Root
& nssm set $ServiceName DisplayName "TokenNest — DeskNest AI Usage Aggregator"
& nssm set $ServiceName Description "Local HTTP aggregator for ChatGPT/MiniMax usage; serves /status.json for DeskNest K10."
& nssm set $ServiceName Start SERVICE_AUTO_START
& nssm set $ServiceName AppStdout (Join-Path $LogDir 'tokennest.log')
& nssm set $ServiceName AppStderr (Join-Path $LogDir 'tokennest.log')
& nssm set $ServiceName AppRotateFiles 1
& nssm set $ServiceName AppRotateBytes 1048576

Write-Host "[install] starting service..."
& nssm start $ServiceName
Write-Host "[install] done. check: nssm status $ServiceName"
Write-Host "[install] uninstall later: nssm stop $ServiceName; nssm remove $ServiceName confirm"
