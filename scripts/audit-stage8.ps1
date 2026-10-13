[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
    Write-Output "Environment: $([Environment]::OSVersion.VersionString); PowerShell $($PSVersionTable.PSVersion)"
    $spec = Get-Content -LiteralPath SPEC.md -Raw -Encoding UTF8
    foreach ($token in @('80000', '691577472', '0.996', '640', 'i2s_std.h', 'valid-one-bar')) {
        if (-not $spec.Contains($token)) { throw "Missing SPEC token: $token" }
        Write-Output "SPEC PASS: $token"
    }
    & git diff --exit-code -- SPEC.md core simulator firmware tests CMakeLists.txt shared backend/wrangler.jsonc package.json package-lock.json
    if ($LASTEXITCODE -ne 0) { throw 'Protected contract/core/firmware/tests/config changed' }
    Write-Output 'Protected source/config: PASS (unchanged against HEAD)'
    $oldTest = (& git show HEAD:backend/tests/backend.test.mjs) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read baseline backend tests' }
    $newTest = (Get-Content backend/tests/backend.test.mjs -Encoding UTF8) -join "`n"
    $oldTest = $oldTest.Replace('health/info and all modes never call network/model; AI always unavailable',
        'health/info/rules/mock never call network/model; missing or inaccessible AI unavailable')
    if ($oldTest -cne $newTest) { throw 'Existing backend assertions changed' }
    Write-Output 'Existing backend assertions: PASS (only test title updated)'
    foreach ($path in @('docs/architecture.md', 'docs/protocol.md', 'docs/testing.md')) {
        $count = @(Get-Content -LiteralPath $path -Encoding UTF8).Count
        if ($count -gt 150) { throw "$path exceeds 150 lines" }
        Write-Output "Doc PASS: $path lines=$count <=150"
    }
    $forbidden = @(Get-ChildItem backend/src -File | Select-String -SimpleMatch 'whisper-tiny-en')
    if ($forbidden.Count -ne 0) { throw 'Forbidden model in backend source' }
    Write-Output 'Model source audit: PASS (no forbidden ASR model; no ASR calls implemented)'
    $idf = Get-Command idf.py -ErrorAction SilentlyContinue
    Write-Output "idf.py_present=$([bool]$idf)"
    Write-Output "IDF_PATH_present=$([bool]$env:IDF_PATH); IDF_TOOLS_PATH_present=$([bool]$env:IDF_TOOLS_PATH)"
    Write-Output "CLOUDFLARE_ACCOUNT_ID_present=$([bool]$env:CLOUDFLARE_ACCOUNT_ID); CLOUDFLARE_API_TOKEN_present=$([bool]$env:CLOUDFLARE_API_TOKEN)"
    Write-Output 'AI real calls: BLOCKED (stage 8 prohibits real calls regardless of credentials)'
    Write-Output 'Firmware platform build: BLOCKED (not performed in stage 8)'
    Write-Output 'Physical hardware: BLOCKED (not tested)'
    Write-Output 'Cloud deployment: BLOCKED (prohibited; not performed)'
    Write-Output 'Stage 8 audit: PASS'
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
finally { Pop-Location }
exit 0
