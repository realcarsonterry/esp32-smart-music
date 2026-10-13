param(
    [string]$Simulator = 'build/simulator.exe',
    [string]$Output = 'artifacts/valid-one-bar.wav'
)

$ErrorActionPreference = 'Stop'
try {
    $projectRoot = Split-Path -Parent $PSScriptRoot
    Push-Location -LiteralPath $projectRoot
    try {
        $executable = (Resolve-Path -LiteralPath $Simulator).Path
        Write-Output "Environment: $([Environment]::OSVersion.VersionString); PowerShell $($PSVersionTable.PSVersion)"
        Write-Output "Command: $Simulator shared/fixtures/valid-one-bar.json $Output"
        & $executable 'shared/fixtures/valid-one-bar.json' $Output
        $validCode = $LASTEXITCODE
        Write-Output "Valid exit code: $validCode"
        if ($validCode -ne 0) { throw 'Valid fixture failed' }

        $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Output).Path)
        if ($bytes.Length -lt 44) { throw 'Truncated WAV' }
        $ascii = [Text.Encoding]::ASCII
        $u16 = { param($offset) [uint32]$bytes[$offset] + ([uint32]$bytes[$offset + 1] -shl 8) }
        $u32 = { param($offset) [uint64]$bytes[$offset] + ([uint64]$bytes[$offset + 1] -shl 8) + ([uint64]$bytes[$offset + 2] -shl 16) + ([uint64]$bytes[$offset + 3] -shl 24) }
        if ($ascii.GetString($bytes, 0, 4) -cne 'RIFF' -or
            $ascii.GetString($bytes, 8, 8) -cne 'WAVEfmt ' -or
            $ascii.GetString($bytes, 36, 4) -cne 'data' -or
            (& $u32 4) -ne ($bytes.Length - 8) -or (& $u32 16) -ne 16 -or
            (& $u16 20) -ne 1 -or (& $u16 22) -ne 1 -or
            (& $u32 24) -ne 32000 -or (& $u32 28) -ne 64000 -or
            (& $u16 32) -ne 2 -or (& $u16 34) -ne 16) { throw 'Unexpected WAV header' }
        $dataBytes = & $u32 40
        $samples = $dataBytes / 2
        if ($dataBytes -ne 160000 -or $bytes.Length -ne 160044 -or $samples -ne 80000) {
            throw "Wrong sample count or file length: $samples / $($bytes.Length)"
        }
        $nonzero = 0
        for ($i = 0; $i -lt 32000; ++$i) {
            if ($bytes[44 + 2 * $i] -ne 0 -or $bytes[45 + 2 * $i] -ne 0) { ++$nonzero }
        }
        if ($nonzero -eq 0) { throw 'First second is all silent' }
        Write-Output "WAV PASS: samples=$samples rate=32000 channels=1 bits=16 bytes=$($bytes.Length) first_second_nonzero_samples=$nonzero"

        $runId = [Guid]::NewGuid().ToString('N')
        foreach ($name in @('invalid-bpm', 'invalid-midi', 'invalid-overflow-time', 'invalid-polyphony', 'invalid-version')) {
            $invalidOutput = "artifacts/stage-4-$runId-$name.wav"
            Write-Output "Command: $Simulator shared/fixtures/$name.json $invalidOutput"
            & $executable "shared/fixtures/$name.json" $invalidOutput
            $invalidCode = $LASTEXITCODE
            $exists = Test-Path -LiteralPath $invalidOutput
            if ($invalidCode -ne 2 -or $exists) { throw "$name exit=$invalidCode output_exists=$exists" }
            Write-Output "$name PASS: exit=$invalidCode output_exists=$exists"
        }
        Write-Output "Result: $((Resolve-Path -LiteralPath $Output).Path)"
        Write-Output 'Stage 4 verification: PASS'
    } finally {
        Pop-Location
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
exit 0
