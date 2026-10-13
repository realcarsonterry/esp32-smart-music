# Integration checks: real Worker, occupied ports, output preservation and cleanup.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $root 'scripts/run-simulator.ps1'
$hostPath = (Get-Process -Id $PID).Path
$first = $null; $second = $null
function Invoke-Runner {
    param([string]$Mode, [int]$ExpectedCode)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $hostPath -NoProfile -ExecutionPolicy Bypass -File $runner -Text '生成一段温暖、轻快、适合夏日海边的木吉他伴奏' -Mode $Mode 2>&1)
        $actual = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    $lines | ForEach-Object { Write-Host $_ }
    if ($actual -ne $ExpectedCode) { throw "Expected exit $ExpectedCode; actual $actual" }
    return ($lines -join "`n")
}
function Assert-FreePort {
    param([int]$Port)
    $probe = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $Port)
    $probe.Server.ExclusiveAddressUse = $true
    try { $probe.Start() } finally { $probe.Stop() }
}
try {
    Write-Output "Environment: $([Environment]::OSVersion.VersionString); PowerShell $($PSVersionTable.PSVersion)"
    Assert-FreePort 8787
    Assert-FreePort 8877
    $first = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 8787)
    $first.Server.ExclusiveAddressUse = $true
    $first.Start()
    Write-Output 'Command: run-simulator.ps1 -Text <specified Chinese text> -Mode rules (8787 held by this test)'
    $result = Invoke-Runner rules 0
    if ($result -notmatch 'port=8877' -or -not $first.Server.IsBound) { throw 'Fallback or preservation of existing listener failed.' }
    Assert-FreePort 8877
    Write-Output 'Fallback: PASS (8877 selected; existing 8787 listener survives; 8877 released)'
    $wavPath = Join-Path $root 'artifacts/generated_music.wav'
    $jsonPath = Join-Path $root 'artifacts/generated_music.json'
    $wavHash = (Get-FileHash -LiteralPath $wavPath -Algorithm SHA256).Hash
    $jsonHash = (Get-FileHash -LiteralPath $jsonPath -Algorithm SHA256).Hash
    $second = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 8877)
    $second.Server.ExclusiveAddressUse = $true
    $second.Start()
    Write-Output 'Command: run-simulator.ps1 -Text <specified Chinese text> -Mode rules (both ports held)'
    $null = Invoke-Runner rules 5
    if (-not $first.Server.IsBound -or -not $second.Server.IsBound -or
        (Get-FileHash -LiteralPath $wavPath -Algorithm SHA256).Hash -cne $wavHash -or
        (Get-FileHash -LiteralPath $jsonPath -Algorithm SHA256).Hash -cne $jsonHash) {
        throw 'Occupied-port failure modified existing listeners or artifacts.'
    }
    Write-Output 'Both occupied: PASS (exit=5; listeners and JSON/WAV unchanged)'
    $second.Stop(); $second = $null
    $first.Stop(); $first = $null
    Write-Output 'Command: run-simulator.ps1 -Text <specified Chinese text> -Mode ai'
    $null = Invoke-Runner ai 4
    $errorJson = [IO.File]::ReadAllText($jsonPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($errorJson.ok -ne $false -or $errorJson.error.code -cne 'AI_UNAVAILABLE' -or
        (Get-FileHash -LiteralPath $wavPath -Algorithm SHA256).Hash -cne $wavHash) {
        throw 'HTTP failure did not preserve error JSON / existing WAV.'
    }
    Assert-FreePort 8787
    Assert-FreePort 8877
    Write-Output 'HTTP failure: PASS (503; exit=4; error JSON saved; WAV unchanged; owned ports released)'
    Write-Output 'Stage 6 integration: PASS'
} catch { Write-Error $_ -ErrorAction Continue; exit 5 }
finally {
    if ($second) { $second.Stop() }
    if ($first) { $first.Stop() }
}
exit 0
