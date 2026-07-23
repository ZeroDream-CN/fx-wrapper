#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Server = Join-Path $Root 'server'
$Log = Join-Path $Root 'tmp-win-test.log'
$Err = Join-Path $Root 'tmp-win-test.err'
$ServerCfg = Join-Path $Root 'test-resources\test-server.cfg'
$SystemResource = Join-Path $Server 'citizen\system_resources\fx-sandbox-test'
$TimeoutSec = 90

& (Join-Path $PSScriptRoot 'sync-sandbox-test-resource.ps1')

$hookDll = Join-Path $Root 'build-windows-amd64\Release\fx-hook.dll'
if (-not (Test-Path -LiteralPath $hookDll)) {
    & (Join-Path $PSScriptRoot 'build-windows-amd64.ps1')
}
Copy-Item -LiteralPath $hookDll -Destination (Join-Path $Server 'fx-hook.dll') -Force
Copy-Item -LiteralPath (Join-Path $Root 'build-windows-amd64\Release\FXWrapper.exe') -Destination (Join-Path $Server 'fx-wrapper.exe') -Force
Copy-Item -LiteralPath $ServerCfg -Destination (Join-Path $Server 'test-server.cfg') -Force

if (Test-Path -LiteralPath $SystemResource) {
    Remove-Item -LiteralPath $SystemResource -Recurse -Force
}
Copy-Item -Path (Join-Path $Root 'test-resources\fx-sandbox-test') -Destination $SystemResource -Recurse -Force

Get-Process FXServer, fx-wrapper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
foreach ($port in @(30120, 40120)) {
    $connections = Get-NetTCPConnection -LocalPort $port -ErrorAction SilentlyContinue
    foreach ($connection in $connections) {
        Stop-Process -Id $connection.OwningProcess -Force -ErrorAction SilentlyContinue
    }
}
Start-Sleep -Seconds 2

Remove-Item -LiteralPath $Log, $Err -Force -ErrorAction SilentlyContinue

$env:TXHOST_TXA_PORT = '40120'
$env:TXHOST_FXS_PORT = '30120'
$cfgArg = '+set sv_endpointPrivacy true +endpoint_add_tcp 0.0.0.0:30120 +endpoint_add_udp 0.0.0.0:30120 +exec ' + ((Join-Path $Server 'test-server.cfg') -replace '\\', '/')

$p = Start-Process -FilePath (Join-Path $Server 'fx-wrapper.exe') `
    -WorkingDirectory $Server `
    -ArgumentList $cfgArg `
    -PassThru `
    -NoNewWindow `
    -RedirectStandardOutput $Log `
    -RedirectStandardError $Err

$allPass = $false
$luaAllPass = $false
for ($i = 0; $i -lt $TimeoutSec; $i++) {
    Start-Sleep -Seconds 1
    if (Test-Path -LiteralPath $Log) {
        $combined = (Get-Content -LiteralPath $Log -Raw -ErrorAction SilentlyContinue)
        if ($combined -match '\[fx-sandbox-test\] ALL:PASS') {
            $allPass = $true
        }
        if ($combined -match '\[fx-sandbox-test\] LUA_ALL:PASS') {
            $luaAllPass = $true
        }
        if ($allPass -and $luaAllPass) {
            break
        }
        if ($combined -match '\[fx-sandbox-test\] (ALL|LUA_ALL):FAIL|FATAL ERROR') {
            break
        }
    }
    if ($p.HasExited) {
        break
    }
}

if (-not $p.HasExited) {
    Stop-Process -Id $p.Id -Force
}

$combined = ''
if (Test-Path -LiteralPath $Log) {
    $combined += Get-Content -LiteralPath $Log -Raw
}
if (Test-Path -LiteralPath $Err) {
    $combined += Get-Content -LiteralPath $Err -Raw
}

Write-Host '=== hook / wrapper ==='
Select-String -InputObject $combined -Pattern 'fx-wrapper|Hook 安装成功|NodePermission' -AllMatches | ForEach-Object { $_.Line }

Write-Host '=== sandbox test ==='
Select-String -InputObject $combined -Pattern '\[fx-sandbox-test\]' -AllMatches | ForEach-Object { $_.Line }

$hookOk = $combined -match 'Hook 安装成功'
$sandboxOk = $allPass -and $luaAllPass
Write-Host "=== result: hook=$hookOk sandbox=$sandboxOk node=$allPass lua=$luaAllPass ==="
if (-not $hookOk -or -not $sandboxOk) {
    exit 1
}
