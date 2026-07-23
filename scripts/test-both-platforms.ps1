#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Write-Host '=== Windows sandbox test ==='
& (Join-Path $PSScriptRoot 'test-win-server.ps1')
if ($LASTEXITCODE -ne 0) {
    throw 'Windows sandbox test failed'
}

Write-Host ''
Write-Host '=== Linux sandbox test ==='
& wsl.exe bash -lc "cd /mnt/c/PrivateProject/fx-wrapper && bash scripts/test-linux-server.sh"
if ($LASTEXITCODE -ne 0) {
    throw 'Linux sandbox test failed'
}

Write-Host ''
Write-Host 'Both platform tests passed.'
