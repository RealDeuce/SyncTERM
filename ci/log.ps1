# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
# No named parameters: CMake/compiler switches must remain literal arguments,
# not be interpreted as this script's PowerShell parameters.
if ($args.Count -lt 2) { throw 'Expected log filename, program and optional arguments' }
$logPath = $args[0]
$program = $args[1]
$commandArguments = @($args | Select-Object -Skip 2)
# Windows PowerShell can represent native stderr as ErrorRecords. Compiler
# warnings on stderr are not terminating PowerShell errors.
$ErrorActionPreference = 'Continue'
try {
    Get-Command $program -CommandType Application -ErrorAction Stop | Out-Null
    $global:LASTEXITCODE = 1
    & $program @commandArguments 2>&1 | Tee-Object -FilePath $logPath -ErrorAction Stop
    $result = $LASTEXITCODE
} catch {
    Write-Error $_
    exit 1
}
exit $result
