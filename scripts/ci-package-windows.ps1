#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DistDir = Join-Path $Root "dist\windows-amd64"
$ArtifactsDir = Join-Path $Root "artifacts"

if (-not (Test-Path -LiteralPath $DistDir)) {
    throw "Dist directory not found: $DistDir"
}

if ($env:CI_COMMIT_TAG) {
    $Version = $env:CI_COMMIT_TAG.TrimStart("v")
} elseif ($env:CI_COMMIT_SHA) {
    $Version = $env:CI_COMMIT_SHA.Substring(0, [Math]::Min(8, $env:CI_COMMIT_SHA.Length))
} else {
    $Version = Get-Date -Format "yyyyMMddHHmmss"
}

Write-Host "Packaging version: $Version"
New-Item -ItemType Directory -Force -Path $ArtifactsDir | Out-Null

$zipPath = Join-Path $ArtifactsDir "fx-wrapper-windows-amd64-$Version.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $DistDir "*") -DestinationPath $zipPath -Force

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$zipPath.sha256" -Value "$hash  $(Split-Path -Leaf $zipPath)" -NoNewline

Write-Host ""
Write-Host "Artifacts:"
Get-ChildItem -LiteralPath $ArtifactsDir | Format-Table Name, Length -AutoSize
