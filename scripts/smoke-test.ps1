[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$exitCode = 5
try {
    $root = Split-Path -Parent $PSScriptRoot
    # A child host keeps the runner's explicit exit codes from exiting this verifier.
    $hostPath = (Get-Process -Id $PID).Path
    & $hostPath -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-simulator.ps1') -Text '生成一段温暖、轻快、适合夏日海边的木吉他伴奏' -Mode rules
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "HTTP-to-WAV runner failed: $exitCode" }
    $exitCode = 2
    $jsonPath = Join-Path $root 'artifacts/generated_music.json'
    $music = [IO.File]::ReadAllText($jsonPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    if ($music.ok -ne $true -or $music.mode -cne 'rules' -or $music.bpm -ne 96 -or
        $music.key.root -cne 'C' -or $music.key.mode -cne 'major' -or $music.seed -ne 691577472 -or
        $music.bars -ne 4 -or (($music.chords | ForEach-Object { $_.symbol }) -join ',') -cne 'C,G,Am,F') {
        throw 'Unexpected HTTP JSON: expected rules, 96 BPM, C major, C/G/Am/F, four bars, seed 691577472.'
    }
    Write-Output 'HTTP JSON: PASS (rules; C major; bpm=96; chords=C,G,Am,F; seed=691577472)'
    $exitCode = 3
    $wavPath = Join-Path $root 'artifacts/generated_music.wav'
    $bytes = [IO.File]::ReadAllBytes($wavPath)
    $ascii = [Text.Encoding]::ASCII
    if ($bytes.Length -lt 44 -or $ascii.GetString($bytes, 0, 4) -cne 'RIFF' -or
        $ascii.GetString($bytes, 8, 8) -cne 'WAVEfmt ' -or $ascii.GetString($bytes, 36, 4) -cne 'data' -or
        [BitConverter]::ToUInt32($bytes, 4) -ne ($bytes.Length - 8) -or
        [BitConverter]::ToUInt32($bytes, 16) -ne 16 -or [BitConverter]::ToUInt16($bytes, 20) -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 22) -ne 1 -or [BitConverter]::ToUInt32($bytes, 24) -ne 32000 -or
        [BitConverter]::ToUInt32($bytes, 28) -ne 64000 -or [BitConverter]::ToUInt16($bytes, 32) -ne 2 -or
        [BitConverter]::ToUInt16($bytes, 34) -ne 16 -or [BitConverter]::ToUInt32($bytes, 40) -ne ($bytes.Length - 44)) {
        throw 'WAV is not valid mono 32000 Hz 16-bit little-endian PCM.'
    }
    $samples = ($bytes.Length - 44) / 2
    $expected = [Math]::Floor(($music.bars * 1920.0 * 32000 * 60 + ($music.bpm * 480) / 2) / ($music.bpm * 480))
    if ($samples -ne $expected -or $samples -ne 320000) { throw "Unexpected sample count: $samples" }
    $nonzero = 0; $firstSecond = 0; $peak = 0; $squareSum = 0.0
    for ($i = 0; $i -lt $samples; ++$i) {
        $sample = [int][BitConverter]::ToInt16($bytes, 44 + 2 * $i)
        if ($sample -ne 0) { ++$nonzero; if ($i -lt 32000) { ++$firstSecond } }
        $peak = [Math]::Max($peak, [Math]::Abs($sample))
        $squareSum += [double]$sample * $sample
    }
    if ($nonzero -eq 0 -or $firstSecond -eq 0) { throw 'WAV is silent (whole track or first second).' }
    $rms = [Math]::Sqrt($squareSum / $samples)
    Write-Output "WAV: PASS (PCM16 LE; rate=32000; channels=1; samples=$samples; bytes=$($bytes.Length); nonzero=$nonzero; first_second_nonzero=$firstSecond; peak=$peak; RMS=$rms)"
    Write-Output "WAV absolute path: $wavPath"
    Write-Output "WAV SHA256: $((Get-FileHash -LiteralPath $wavPath -Algorithm SHA256).Hash)"
    Write-Output 'Smoke test: PASS (whole-track pitch test is not applicable)'
    $exitCode = 0
} catch { Write-Error $_ -ErrorAction Continue }
Write-Output "EXIT_CODE=$exitCode"
exit $exitCode
