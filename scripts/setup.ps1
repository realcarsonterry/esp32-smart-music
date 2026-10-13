[CmdletBinding()]
param([string]$Compiler, [string]$Generator)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-environment.ps1') -Compiler $Compiler -Generator $Generator
try {
    $environment = Get-ProjectEnvironment -Compiler $Compiler -Generator $Generator
    $env:CC = $environment.Compiler
    $env:CMAKE_GENERATOR = $environment.Generator
    Push-Location -LiteralPath $environment.Root
    try {
        & $environment.Npm ci --no-fund --no-audit
        if ($LASTEXITCODE -ne 0) { throw "npm ci failed: $LASTEXITCODE" }
        & $environment.CMake -S . -B build -G $environment.Generator "-DCMAKE_C_COMPILER=$($environment.Compiler)" "-DCMAKE_MAKE_PROGRAM=$($environment.MakeProgram)"
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
        & $environment.CMake --build build --config Release
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }
        $null = New-Item -ItemType Directory -Path artifacts -Force
        $null = Get-ProjectEnvironment -RequireBuild
        Write-Output 'Setup: PASS'
    } finally { Pop-Location }
} catch { Write-Error $_ -ErrorAction Continue; exit 5 }
exit 0
