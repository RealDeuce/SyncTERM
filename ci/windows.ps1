# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
param(
    [ValidateSet('msvc', 'mingw')][string]$Compiler = 'msvc',
    [ValidateSet('x86', 'x64')][string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Save-NativeDiagnostic {
    param([string]$Report, [string]$Program, [string[]]$CommandArguments)
    # Windows PowerShell wraps native stderr in ErrorRecords, even for a
    # successful `gcc -v`. Retain it without treating it as a script error.
    $ErrorActionPreference = 'Continue'
    & $Program @CommandArguments 2>&1 | Out-File -Append $Report
    return $LASTEXITCODE
}
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $env:CI_BUILD_DIR) { $env:CI_BUILD_DIR = Join-Path $projectRoot 'build-ci' }
$reports = Join-Path $env:CI_BUILD_DIR 'reports'
New-Item -ItemType Directory -Force $reports | Out-Null
$env:MSBUILDDEBUGPATH = Join-Path $reports 'msbuild-debug'
New-Item -ItemType Directory -Force $env:MSBUILDDEBUGPATH | Out-Null

# Use the host CMake, including Visual Studio's bundle when needed.
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'cmake.exe and vswhere.exe not found' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio C++ tools not found' }
    $cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $env:PATH = $cmakeBin + ';' + $env:PATH
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'cmake.exe not found' }
# Keep the host CMake and its TLS trust configuration when adding compiler
# tools to PATH: the MinGW archive also contains a different cmake.exe.
$cmake = (Get-Command cmake.exe).Source

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
        & $cmake -E tar xf $archive
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
    $toolReport = Join-Path $reports 'mingw-tools.txt'
    if (Test-Path -LiteralPath $toolReport) { Clear-Content $toolReport }
    $null = Save-NativeDiagnostic $toolReport $env:CC @('-v')
    $null = Save-NativeDiagnostic $toolReport $env:CC @('-print-search-dirs')
    $null = Save-NativeDiagnostic $toolReport $env:CC @('-dumpmachine')
    $assembler = & $env:CC -print-prog-name=as
    if ($LASTEXITCODE -ne 0) { throw 'Could not resolve the compiler assembler' }
    "Compiler-selected assembler: $assembler" | Out-File -Append $toolReport
    if ((Save-NativeDiagnostic $toolReport $assembler @('--version')) -ne 0) {
        throw 'Compiler-selected assembler could not run'
    }
    Get-Command gcc.exe, as.exe -All -ErrorAction SilentlyContinue | Select-Object Name, Source |
        Format-Table | Out-File -Append $toolReport
    # Keep the precise compile invocation for a failed target. The x86
    # assembler failure also needs its generated assembly, not just stderr.
    $env:CI_EXTRA_CMAKE_ARGS += ';-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
}

# Do not inherit an interactive session's display selection.
Remove-Item Env:DISPLAY, Env:WAYLAND_DISPLAY -ErrorAction SilentlyContinue
& $cmake -P (Join-Path $PSScriptRoot 'run.cmake')
$buildResult = $LASTEXITCODE
if ($buildResult -ne 0 -and $Compiler -eq 'mingw') {
    $failedTests = Join-Path $env:CI_BUILD_DIR 'Testing/Temporary/LastTestsFailed.log'
    $testLog = Join-Path $reports 'test.log'
    $pixelTest = Join-Path $env:CI_BUILD_DIR 'src/media/pixel_image/st_pixel_image_test.exe'
    $debugger = Join-Path $compilerBin 'gdb.exe'
    if ((Test-Path -LiteralPath $testLog) -and (Test-Path -LiteralPath $failedTests) -and
        (Select-String -LiteralPath $failedTests -Pattern '^[0-9]+:pixel_image$' -Quiet) -and
        (Test-Path -LiteralPath $pixelTest) -and (Test-Path -LiteralPath $debugger)) {
        # Run on the native runner: an access violation otherwise leaves
        # only CTest's SEGFAULT label, with no indication of the fault site.
        $backtrace = Join-Path $reports 'pixel-image-backtrace.txt'
        if (Test-Path -LiteralPath $backtrace) { Clear-Content -LiteralPath $backtrace }
        $null = Save-NativeDiagnostic $backtrace $debugger @(
            '--batch', '--nx', '-ex', 'set pagination off', '-ex', 'run',
            '-ex', 'thread apply all bt full', '-ex', 'info registers',
            '-ex', 'x/12i $pc', '-ex', 'info sharedlibrary', '--args', $pixelTest)
    }
    $commands = Join-Path $env:CI_BUILD_DIR 'compile_commands.json'
    if (Test-Path -LiteralPath $commands) {
        Copy-Item -LiteralPath $commands -Destination $reports
        $failedSource = Get-Content -Raw $commands | ConvertFrom-Json |
            Where-Object { $_.file -match '[/\\]bbs_list[/\\]st_bbs_list\.c$' } |
            Select-Object -First 1
        $buildLog = Join-Path $reports 'build.log'
        if ($failedSource -and (Test-Path -LiteralPath $buildLog) -and
            (Select-String -LiteralPath $buildLog -Pattern 'unknown pseudo-op.*uleb128' -Quiet)) {
            Push-Location $failedSource.directory
            try {
                $reproResult = Save-NativeDiagnostic (Join-Path $reports 'mingw-assembly-repro.txt') cmd.exe @('/d', '/c', ($failedSource.command + ' -v -save-temps=obj'))
                "Reproduction exit: $reproResult" |
                    Out-File -Append (Join-Path $reports 'mingw-assembly-repro.txt')
                Get-ChildItem (Join-Path $env:CI_BUILD_DIR 'bbs_list/CMakeFiles/st_bbs_list.dir') -Filter '*.s' -Recurse |
                    Copy-Item -Destination $reports
            } finally {
                Pop-Location
            }
        }
    }
}
exit $buildResult
