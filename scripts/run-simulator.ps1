[CmdletBinding()]
param(
    [string]$Text = '生成一段温暖、轻快、适合夏日海边的木吉他伴奏',
    [string]$Mode = 'rules'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-environment.ps1')
$exitCode = 5
$worker = $null
$client = $null
$savedEnvironment = @{}
try {
    $environment = Get-ProjectEnvironment -RequireBuild
    $artifacts = Join-Path $environment.Root 'artifacts'
    $null = New-Item -ItemType Directory -Path $artifacts -Force
    $jsonPath = Join-Path $artifacts 'generated_music.json'
    $wavPath = Join-Path $artifacts 'generated_music.wav'
    $logPath = Join-Path $artifacts 'stage-6-worker.log'
    $port = $null
    foreach ($candidate in @(8787, 8877)) {
        $probe = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $candidate)
        $probe.Server.ExclusiveAddressUse = $true
        try { $probe.Start(); $port = $candidate; break } catch { } finally { $probe.Stop() }
    }
    if (-not $port) { throw 'Ports 8787 and 8877 are occupied; no process was stopped.' }
    if (-not ('MusicWorkerJob' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'WorkerJob.cs') }
    foreach ($name in @('CI', 'WRANGLER_SEND_METRICS', 'WRANGLER_LOG_PATH')) {
        $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $env:CI = 'true'
    $env:WRANGLER_SEND_METRICS = 'false'
    # Wrangler debug logs include private runtime state; keep them in ignored storage.
    $env:WRANGLER_LOG_PATH = Join-Path $environment.Root 'backend/.wrangler/stage-6-internal.log'
    # Wrangler 4.149 rejects --offline. User approved --local, which disables remote bindings.
    $arguments = 'node_modules/wrangler/bin/wrangler.js dev --local --config backend/wrangler.jsonc --ip 127.0.0.1 --port ' + $port + ' --inspector-port 0 --show-interactive-dev-session=false'
    Write-Output "Command: node $arguments"
    $worker = New-Object MusicWorkerJob($environment.Node, $arguments, $environment.Root, $logPath)
    Write-Output "Worker PID: $($worker.ProcessId); port=$port"
    Add-Type -AssemblyName System.Net.Http
    $handler = New-Object Net.Http.HttpClientHandler
    $handler.UseProxy = $false
    $client = New-Object Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(3)
    $base = "http://127.0.0.1:$port"
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        # Non-interactive stdin prevents OAuth prompts; detect and stop any login request.
        $log = ''
        if (Test-Path -LiteralPath $logPath) {
            $stream = [IO.File]::Open($logPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
            try { $log = $reader.ReadToEnd() } finally { $reader.Dispose() }
        }
        if ($log -match '(?i)(wrangler login|oauth|log\s*in|logging in|not logged in|authentication required|not authenticated)') {
            throw 'BLOCKED: Wrangler requested login; login was not attempted.'
        }
        if ($worker.HasExited) { throw "Worker exited before /health; see $logPath" }
        try {
            $response = $client.GetAsync("$base/health").GetAwaiter().GetResult()
            try {
                $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
                $health = $body | ConvertFrom-Json
                if ([int]$response.StatusCode -eq 200 -and $health.ok -eq $true -and $health.protocol_version -ceq '1.0') {
                    # A port can be claimed after probing; do not accept somebody else's /health.
                    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction Stop |
                        Where-Object { $_.LocalAddress -eq '127.0.0.1' })
                    if ($listeners.Count -eq 1 -and $worker.Contains([int]$listeners[0].OwningProcess)) { $ready = $true }
                }
            } finally { $response.Dispose() }
        } catch { }
        if ($ready) { break }
        Start-Sleep -Milliseconds 200
    }
    $exitCode = 4
    if (-not $ready) { throw "Worker /health did not become ready; see $logPath" }
    Write-Output "Health: PASS ($base/health; owned listener)"
    $requestId = 'stage-6-' + [Guid]::NewGuid().ToString('N')
    $requestBody = @{ text = $Text; mode = $Mode; request_id = $requestId } | ConvertTo-Json -Compress
    $content = New-Object Net.Http.StringContent($requestBody, [Text.Encoding]::UTF8, 'application/json')
    try {
        $response = $client.PostAsync("$base/api/v1/generate-from-text", $content).GetAwaiter().GetResult()
        try {
            $responseBody = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            [IO.File]::WriteAllText($jsonPath, $responseBody, (New-Object Text.UTF8Encoding($false)))
            Write-Output "HTTP status: $([int]$response.StatusCode); JSON: $jsonPath"
            if ([int]$response.StatusCode -ne 200) { throw "HTTP failed: $([int]$response.StatusCode); response saved." }
        } finally { $response.Dispose() }
    } finally { $content.Dispose() }
    $exitCode = 2
    $music = $responseBody | ConvertFrom-Json
    if ($music.ok -ne $true -or $music.protocol_version -cne '1.0' -or $music.request_id -cne $requestId -or $music.mode -cne $Mode) {
        throw 'Invalid success JSON, request_id or mode mismatch.'
    }
    # The stage 4 executable validates the entire JSON before invoking the same C core.
    & $environment.Simulator $jsonPath $wavPath
    $renderCode = $LASTEXITCODE
    if ($renderCode -eq 2) { throw 'Simulator rejected Music JSON.' }
    $exitCode = 3
    if ($renderCode -ne 0) { throw "Simulator failed: $renderCode" }
    Write-Output "request_id: $($music.request_id)"
    Write-Output "mode: $($music.mode)"
    Write-Output "bpm: $($music.bpm)"
    Write-Output "调式: $($music.key.root) $($music.key.mode)"
    Write-Output "和弦: $(($music.chords | ForEach-Object { $_.symbol }) -join ', ')"
    Write-Output "WAV: $wavPath"
    Write-Output 'Simulator: PASS'
    $exitCode = 0
} catch { Write-Error $_ -ErrorAction Continue }
finally {
    if ($client) { $client.Dispose() }
    if ($worker) { $worker.Dispose(); Write-Output 'Worker cleanup: private job closed (only owned processes).' }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}
Write-Output "EXIT_CODE=$exitCode"
exit $exitCode
