# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
param(
    [ValidateSet('msvc', 'mingw')][string]$Compiler = 'msvc',
    [ValidateSet('x86', 'x64')][string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $env:CI_BUILD_DIR) { $env:CI_BUILD_DIR = Join-Path $projectRoot 'build-ci' }
$reports = Join-Path $env:CI_BUILD_DIR 'reports'
New-Item -ItemType Directory -Force $reports | Out-Null
$env:MSBUILDDEBUGPATH = Join-Path $reports 'msbuild-debug'
New-Item -ItemType Directory -Force $env:MSBUILDDEBUGPATH | Out-Null

# CMake bundled with Visual Studio is sufficient; Ninja is optional.
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'cmake.exe and vswhere.exe not found' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio C++ tools not found' }
    $cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $env:PATH = $cmakeBin + ';' + $env:PATH
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'cmake.exe not found' }

if ($Compiler -eq 'msvc') {
    $env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
    $env:CMAKE_GENERATOR_PLATFORM = if ($Architecture -eq 'x86') { 'Win32' } else { 'x64' }
    # Let the Visual Studio generator choose its matching C and C++ compilers.
    Remove-Item Env:CC, Env:CXX -ErrorAction SilentlyContinue
} else {
    $manifest = Get-Content -Raw (Join-Path $PSScriptRoot 'mingw-toolchains.json') | ConvertFrom-Json
    $toolchain = $manifest.$Architecture
    $cacheRoot = Join-Path $projectRoot '.ci-cache\toolchains'
    New-Item -ItemType Directory -Force $cacheRoot | Out-Null
    $archive = Join-Path $cacheRoot $toolchain.archive
    $download = $archive + '.partial'
    if (-not (Test-Path -LiteralPath $archive)) {
        $url = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/' + $manifest.release + '/' + $toolchain.archive
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $download
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $download).Hash -ne $toolchain.sha256) {
            throw 'Downloaded MinGW archive checksum mismatch'
        }
        Move-Item -LiteralPath $download -Destination $archive -Force
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $toolchain.sha256) {
        throw 'Cached MinGW archive checksum mismatch; remove this cache entry and retry'
    }
    $tools = Join-Path $env:CI_BUILD_DIR 'toolchain'
    New-Item -ItemType Directory -Force $tools | Out-Null
    Push-Location $tools
    try {
        & cmake.exe -E tar xf $archive
        if ($LASTEXITCODE -ne 0) { throw 'MinGW archive extraction failed' }
    } finally {
        Pop-Location
    }
    $compilerBin = Join-Path (Join-Path $tools $toolchain.directory) 'bin'
    $env:PATH = $compilerBin + ';' + $env:PATH
    $env:CC = Join-Path $compilerBin 'gcc.exe'
    $env:CXX = Join-Path $compilerBin 'g++.exe'
    $env:CMAKE_GENERATOR = 'MinGW Makefiles'
    Remove-Item Env:CMAKE_GENERATOR_PLATFORM -ErrorAction SilentlyContinue
    # Also puts libwinpthread/libgcc runtime DLLs on the test process PATH.
    & $env:CC --version | Out-File (Join-Path $reports 'mingw-version.txt')
    if ($LASTEXITCODE -ne 0) { throw 'MinGW compiler could not run' }
}

# Do not inherit an interactive session's display selection.
Remove-Item Env:DISPLAY, Env:WAYLAND_DISPLAY -ErrorAction SilentlyContinue
& cmake.exe -P (Join-Path $PSScriptRoot 'run.cmake')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
