# scripts/start.ps1
# TokenNest 桌面启动脚本（透明 console）
# 用法：powershell -ExecutionPolicy Bypass -File scripts\start.ps1

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Root = Resolve-Path "$ScriptDir\.."
Set-Location $Root

Write-Host "[start] TokenNest root: $Root"
if (-not (Test-Path node_modules)) {
    Write-Host "[start] installing deps..."
    npm install
}
if (-not (Test-Path config\minimax.json)) {
    Write-Host "[start] copying minimax config template -> config\minimax.json"
    Copy-Item config\minimax.example.json config\minimax.json
    Write-Host "[start] !!! please fill your MiniMax apiKey in config\minimax.json !!!"
}
if (-not (Test-Path config\tokennest.yaml)) {
    Write-Host "[start] copying config template -> config\tokennest.yaml"
    Copy-Item config\tokennest.example.yaml config\tokennest.yaml
}
Write-Host "[start] starting TokenNest..."
npm start
