$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}

$dashboard = Join-Path $PSScriptRoot 'gesture_dashboard.py'
Start-Process 'http://127.0.0.1:8765'
& $python $dashboard --port 8765 @args
