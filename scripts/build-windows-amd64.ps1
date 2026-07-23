#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $Root "build-windows-amd64"
$DistDir = Join-Path $Root "dist\windows-amd64"
$Configuration = "Release"

function Find-CMakeExecutable {
    if ($env:CMAKE) {
        if (-not (Test-Path -LiteralPath $env:CMAKE)) {
            throw "CMAKE is set but not found: $env:CMAKE"
        }
        return (Resolve-Path -LiteralPath $env:CMAKE).Path
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $visualStudioRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022"
    )

    $editions = @("Community", "Professional", "Enterprise", "BuildTools")
    foreach ($root in $visualStudioRoots) {
        foreach ($edition in $editions) {
            $candidate = Join-Path $root "$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    throw "cmake not found. Install CMake or Visual Studio with the C++ workload, or set CMAKE to cmake.exe."
}

$Cmake = Find-CMakeExecutable
Write-Host "Using CMake: $Cmake"

& $Cmake -S $Root -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $Cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir "$Configuration\FXWrapper.exe") -Destination $DistDir -Force
Copy-Item -LiteralPath (Join-Path $BuildDir "$Configuration\fx-hook.dll") -Destination $DistDir -Force

Write-Host ""
Write-Host "Deployment bundle (copy entire directory next to FXServer.exe):"
Write-Host "  $DistDir\"
Get-ChildItem -LiteralPath $DistDir | Format-Table Name, Length -AutoSize
