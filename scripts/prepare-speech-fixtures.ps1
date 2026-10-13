# Run with Windows PowerShell 5.1: System.Speech uses the installed Windows voices.
# Generates speech fixtures, never opens a microphone or records a person.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'artifacts/cloud/inputs'
$null = New-Item -ItemType Directory -Path $output -Force
Add-Type -AssemblyName System.Speech
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $voice = $speaker.GetInstalledVoices() | Where-Object {
        $_.Enabled -and $_.VoiceInfo.Culture.Name -eq 'zh-CN'
    } | Select-Object -First 1
    if (-not $voice) { throw 'An installed zh-CN System.Speech voice is required.' }
    $speaker.SelectVoice($voice.VoiceInfo.Name)
    $speaker.Rate = 1
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
        16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono)
    $cases = @(
        @{name='warm'; text='温暖轻快的吉他伴奏'},
        @{name='sad'; text='悲伤慢的吉他伴奏'},
        @{name='rock'; text='摇滚，一百二十 BPM'}
    )
    foreach ($case in $cases) {
        $path = Join-Path $output ('tts-' + $case.name + '.wav')
        $speaker.SetOutputToWaveFile($path, $format)
        $speaker.Speak($case.text)
        $speaker.SetOutputToNull()
        Write-Output "Generated TTS fixture: $($case.name); expected text=$($case.text)"
    }
    $metadata = @{source='Windows System.Speech TTS, not a human or ESP32 recording';
        voice=$voice.VoiceInfo.Name; sample_rate=16000; channels=1; bits_per_sample=16; cases=$cases}
    [IO.File]::WriteAllText((Join-Path $output 'provenance.json'),
        ($metadata | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
} finally { $speaker.Dispose() }
