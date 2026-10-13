[CmdletBinding()]
param(
    [string]$Text = '生成一段温暖、轻快、适合夏日海边的木吉他伴奏',
    [switch]$NoPlay
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
    Write-Output 'Windows software demonstration: local HTTP -> Music JSON -> shared C KS -> WAV'
    Write-Output 'This does not demonstrate an ESP32, physical microphone, or real ASR.'
    $hostPath = (Get-Process -Id $PID).Path
    & $hostPath -NoProfile -ExecutionPolicy Bypass -File scripts/run-simulator.ps1 -Text $Text -Mode rules
    if ($LASTEXITCODE -ne 0) { throw "Generation failed: $LASTEXITCODE" }
    if (-not $NoPlay) {
        # Open the registered player. This does not record audio.
        Start-Process -FilePath (Join-Path $root 'artifacts/generated_music.wav')
    }
    Write-Output 'Demo generation: PASS. Listening and judging sound quality require the user.'
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
finally { Pop-Location }
exit 0
