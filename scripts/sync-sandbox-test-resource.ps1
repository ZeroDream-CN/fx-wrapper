#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Source = Join-Path $Root 'test-resources\fx-sandbox-test'
$Targets = @(
    (Join-Path $Root 'txData\FiveMBasicServerCFXDefault_60318B.base\resources\fx-sandbox-test'),
    (Join-Path $Root 'server-linux\txData\FiveMBasicServerCFXDefault_60318B.base\resources\fx-sandbox-test')
)

foreach ($target in $Targets) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item -Path $Source -Destination $target -Recurse -Force
    Write-Host "Synced sandbox test resource to $target"
}
