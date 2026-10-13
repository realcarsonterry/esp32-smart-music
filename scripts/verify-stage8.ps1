[CmdletBinding()]
param([string]$Compiler, [string]$Generator)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-environment.ps1') -Compiler $Compiler -Generator $Generator
$root = Split-Path -Parent $PSScriptRoot
$summary = Join-Path $root 'artifacts/stage-8-verification.txt'
$utf8 = New-Object Text.UTF8Encoding($false)
$script:stage8Results = New-Object Collections.Generic.List[string]

function Invoke-Check {
    param([string]$Name, [string]$Program, [string[]]$Arguments)
    $command = "$Program $($Arguments -join ' ')"
    Write-Output "Command: $command"
    $logPath = Join-Path $root "artifacts/stage-8-$Name.txt"
    # Native stderr must be captured without PowerShell 5.1 treating it as fatal.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $Program @Arguments 2>&1)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    [IO.File]::WriteAllLines($logPath, @("Command: $command", "Environment: $script:stage8Environment") +
        @($output | ForEach-Object { "$_" }) + @("Exit code: $code"), $utf8)
    $output | ForEach-Object { Write-Output "$_" }
    $script:stage8Results.Add("$Name | exit=$code | $command | $logPath")
    [IO.File]::WriteAllLines($summary, @($script:stage8Environment) + $script:stage8Results.ToArray(), $utf8)
    if ($code -ne 0) { throw "$Name failed: exit=$code; see $logPath" }
}

Push-Location -LiteralPath $root
try {
    $environment = Get-ProjectEnvironment -Compiler $Compiler -Generator $Generator
    $null = New-Item -ItemType Directory -Path (Join-Path $root 'artifacts') -Force
    if (-not (Test-Path -LiteralPath 'node_modules/wrangler/bin/wrangler.js')) {
        throw 'Locked Node dependencies missing; run npm ci first.'
    }
    $env:CC = $environment.Compiler
    $env:CMAKE_GENERATOR = $environment.Generator
    $hostPath = (Get-Process -Id $PID).Path
    $env:WRANGLER_LOG_PATH = Join-Path $root 'backend/.wrangler/stage-8-internal.log'
    $env:WRANGLER_SEND_METRICS = 'false'
    $env:CI = 'true'
    $script:stage8Environment = "$([Environment]::OSVersion.VersionString); $([Runtime.InteropServices.RuntimeInformation]::OSArchitecture); PowerShell $($PSVersionTable.PSVersion); Node $($environment.NodeVersion)"
    Write-Output $script:stage8Environment
    Invoke-Check tools-node $environment.Node @('--version')
    Invoke-Check tools-npm $environment.Npm @('--version')
    Invoke-Check tools-cmake $environment.CMake @('--version')
    Invoke-Check tools-ctest $environment.CTest @('--version')
    Invoke-Check tools-compiler $environment.Compiler @('--version')
    Invoke-Check tools-ninja $environment.MakeProgram @('--version')
    Invoke-Check tools-wrangler $environment.Node @('node_modules/wrangler/bin/wrangler.js', '--version')
    $toolchain = @('-G', $environment.Generator, "-DCMAKE_C_COMPILER=$($environment.Compiler)", "-DCMAKE_MAKE_PROGRAM=$($environment.MakeProgram)")
    Invoke-Check configure $environment.CMake (@('-S', '.', '-B', 'build') + $toolchain)
    Invoke-Check build $environment.CMake @('--build', 'build', '--config', 'Release')
    Invoke-Check tests $hostPath @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/test.ps1')
    Invoke-Check ctest $environment.CTest @('--test-dir', 'build', '-C', 'Release', '--output-on-failure', '-V')
    Invoke-Check host-configure $environment.CMake (@('-S', 'firmware/tests', '-B', 'build/stage7-host') + $toolchain)
    Invoke-Check host-build $environment.CMake @('--build', 'build/stage7-host', '--config', 'Release')
    Invoke-Check host-tests $environment.CTest @('--test-dir', 'build/stage7-host', '-C', 'Release', '--output-on-failure', '-V')
    Invoke-Check smoke $hostPath @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/smoke-test.ps1')
    Invoke-Check integration $hostPath @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tests/verify-stage6.ps1')
    # Integration deliberately leaves an error JSON. Restore the successful pair.
    Invoke-Check smoke-final $hostPath @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/smoke-test.ps1')
    Write-Output 'Stage 8 regression: PASS'
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
finally { Pop-Location }
exit 0
