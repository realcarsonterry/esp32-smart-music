[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$paths = New-Object 'Collections.Generic.List[string]'
# Explicit allowlist: never traverse the workspace wholesale or depend on git tracking.
foreach ($name in @('README.md', 'START_HERE.md', 'SPEC.md', 'PROGRESS.md',
    'CMakeLists.txt', 'package.json', 'package-lock.json', 'tsconfig.json', '.env.example', '.gitignore')) {
    $paths.Add($name)
}
foreach ($directory in @('core', 'simulator', 'shared', 'tests', 'scripts', 'docs',
    'backend/src', 'backend/tests', 'firmware/main', 'firmware/tests')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $directory) -File -Recurse) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Refusing linked files.' }
        $paths.Add([IO.Path]::GetRelativePath($root, $file.FullName))
    }
}
foreach ($name in @('backend/README.md', 'backend/wrangler.jsonc', 'backend/wrangler.production.jsonc',
    'firmware/README.md', 'firmware/CMakeLists.txt', 'firmware/sdkconfig.defaults',
    'artifacts/generated_music.json', 'artifacts/generated_music.wav',
    'artifacts/final-review-regression-local-20261013.txt',
    'artifacts/final-review-probes-20261013.txt', 'artifacts/final-review-idf-version-20261013.txt',
    'artifacts/final-review-idf-build-20261013.txt', 'artifacts/final-review-demo-20261013.txt',
    'artifacts/final-review-api-client-20261013.txt', 'artifacts/final-review-production-dry-run-20261013.txt',
    'artifacts/cloud-deploy-20261013.txt', 'artifacts/cloud-verification-20261013.txt',
    'artifacts/cloud/README.md', 'artifacts/cloud/verification.json', 'artifacts/cloud/inputs/provenance.json')) {
    $paths.Add($name)
}
# Known, non-personal TTS inputs and their 12 verified JSON/WAV output pairs only.
foreach ($profile in @('warm', 'sad', 'rock')) {
    $paths.Add("artifacts/cloud/inputs/tts-$profile.wav")
    foreach ($kind in @('text', 'audio')) {
        foreach ($mode in @('rules', 'ai')) {
            foreach ($extension in @('json', 'wav')) {
                $paths.Add("artifacts/cloud/results/$kind-$mode-$profile.$extension")
            }
        }
    }
}
$out = Join-Path $root 'out'
$null = New-Item -ItemType Directory -Path $out -Force
$zipPath = Join-Path $out ('esp32-smart-music-prototype-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
$zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in ($paths | Sort-Object -Unique)) {
        $path = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing deliverable: $relative" }
        $entry = 'esp32-smart-music/' + $relative.Replace('\', '/')
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $path, $entry,
            [IO.Compression.CompressionLevel]::Optimal)
    }
    # Deliberately omit all binaries and sdkconfig: existing user builds may embed credentials.
    # The fresh IDF build is evidenced by logs; a recipient builds with their own device config.
} finally { $zip.Dispose() }
$check = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    foreach ($entry in $check.Entries) {
        if (($entry.FullName -match '(^|/)(node_modules|\.git|build|\.wrangler|sdkconfig|\.dev\.vars)(/|$)') -or
            (($entry.FullName -match '/\.env(\.|$)') -and ($entry.FullName -notmatch '/\.env\.example$'))) {
            throw "Excluded content detected: $($entry.FullName)"
        }
    }
    Write-Output "Entries: $($check.Entries.Count)"
} finally { $check.Dispose() }
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
[IO.File]::WriteAllText($zipPath + '.sha256.txt', "$hash  $([IO.Path]::GetFileName($zipPath))`n")
Write-Output "ZIP: $zipPath"
Write-Output "Bytes: $((Get-Item -LiteralPath $zipPath).Length)"
Write-Output "SHA256: $hash"
exit 0
