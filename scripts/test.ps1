[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-environment.ps1')
$exitCode = 5
try {
    $environment = Get-ProjectEnvironment -RequireBuild
    Push-Location -LiteralPath $environment.Root
    try {
        & $environment.CMake --build build --config Release
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
        $exitCode = 3
        & $environment.CTest --test-dir build -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest failed: $LASTEXITCODE" }
        # CTest pitch checks are single-note tests; never apply them to a whole song.
        $hostPath = (Get-Process -Id $PID).Path
        & $hostPath -NoProfile -ExecutionPolicy Bypass -File tests/verify-simulator.ps1 -Simulator $environment.Simulator
        if ($LASTEXITCODE -ne 0) { throw "Stage 4 WAV verification failed: $LASTEXITCODE" }
        $exitCode = 2
        & $environment.Npm run typecheck
        if ($LASTEXITCODE -ne 0) { throw "Typecheck failed: $LASTEXITCODE" }
        & $environment.Npm test
        if ($LASTEXITCODE -ne 0) { throw "Backend tests failed: $LASTEXITCODE" }
        Write-Output 'Stages 4 and 5: PASS'
        $exitCode = 0
    } finally { Pop-Location }
} catch { Write-Error $_ -ErrorAction Continue }
exit $exitCode
