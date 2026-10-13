# UTF-8 with BOM: readable by Windows PowerShell 5.1.
[CmdletBinding()]
param([switch]$RequireBuild, [string]$Compiler, [string]$Generator)

function Resolve-ProjectTool {
    param([string[]]$Names, [string]$Fallback)
    foreach ($name in $Names) {
        if (-not $name) { continue }
        $tool = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($tool) { return $tool.Source }
    }
    if ($Fallback -and (Test-Path -LiteralPath $Fallback -PathType Leaf)) {
        return (Get-Item -LiteralPath $Fallback).FullName
    }
    throw "Tool missing: $($Names -join ', '). Add it to PATH or provide a toolchain path."
}

function Get-ToolVersion {
    param([string]$Program)
    $output = @(& $Program --version 2>&1)
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "$Program --version failed: exit=$code" }
    return ($output -join "`n")
}

function Get-ProjectEnvironment {
    param([switch]$RequireBuild, [string]$Compiler, [string]$Generator)
    $root = Split-Path -Parent $PSScriptRoot
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'These scripts require Windows; use equivalent shell commands on other systems.'
    }
    if ($PSVersionTable.PSVersion -lt [version]'5.1') { throw 'PowerShell 5.1 or later required.' }
    $cache = @{}
    $cachePath = Join-Path $root 'build/CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
        foreach ($line in [IO.File]::ReadAllLines($cachePath)) {
            if ($line -match '^(CMAKE_[A-Z_]+):[^=]+=(.+)$') { $cache[$matches[1]] = $matches[2] }
        }
    }
    # Resolve PATH first. Valid cache entries are only a compatibility fallback;
    # a fresh checkout needs no cache and never assumes a particular user's paths.
    $cmakePath = Resolve-ProjectTool @('cmake.exe') $cache['CMAKE_COMMAND']
    $ctestPath = Resolve-ProjectTool @('ctest.exe') (Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe')
    $node = Resolve-ProjectTool @('node.exe')
    $npm = Resolve-ProjectTool @('npm.cmd')
    if (-not $Compiler) { $Compiler = $env:CC }
    if ($Compiler) { $compilerPath = Resolve-ProjectTool @($Compiler) }
    else { $compilerPath = Resolve-ProjectTool @('gcc.exe', 'clang.exe') $cache['CMAKE_C_COMPILER'] }
    if (-not $Generator) { $Generator = $env:CMAKE_GENERATOR }
    if (-not $Generator) { $Generator = $cache['CMAKE_GENERATOR'] }
    if (-not $Generator) { $Generator = 'Ninja' }
    if ($Generator -notin @('Ninja', 'Ninja Multi-Config')) {
        throw 'These host checks support Ninja or Ninja Multi-Config; select a separate build directory when changing an existing generator.'
    }
    $makeProgram = Resolve-ProjectTool @('ninja.exe') $cache['CMAKE_MAKE_PROGRAM']
    # The selected portable compiler may need adjacent runtime DLLs. Child scripts
    # receive the same toolchain without replacing a tool that was found on PATH.
    foreach ($path in @($cmakePath, $compilerPath, $makeProgram)) {
        $directory = Split-Path -Parent $path
        if (($env:PATH -split ';') -notcontains $directory) { $env:PATH = "$env:PATH;$directory" }
    }
    $version = Get-ToolVersion $node
    if ([version]($version.TrimStart('v')) -lt [version]'22.0') {
        throw 'Node.js >=22 required by the locked Wrangler version.'
    }
    $cmakeVersion = Get-ToolVersion $cmakePath
    if ($cmakeVersion -notmatch '(\d+\.\d+\.\d+)' -or
        [version]$matches[1] -lt [version]'3.20') { throw 'CMake >=3.20 required.' }
    # Check each native exit code immediately, including tools without a minimum.
    $null = Get-ToolVersion $npm
    $null = Get-ToolVersion $ctestPath
    $null = Get-ToolVersion $compilerPath
    $null = Get-ToolVersion $makeProgram
    $simulator = $null
    foreach ($relative in @('build/simulator.exe', 'build/Release/simulator.exe', 'build/Debug/simulator.exe')) {
        $candidate = Join-Path $root $relative
        if (Test-Path -LiteralPath $candidate) { $simulator = $candidate; break }
    }
    if ($RequireBuild) {
        if (-not $simulator) { throw 'Simulator missing; run scripts/setup.ps1 first.' }
        if (-not (Test-Path -LiteralPath (Join-Path $root 'node_modules/wrangler/bin/wrangler.js'))) {
            throw 'Locked Node dependencies missing; run npm ci first.'
        }
    }
    [pscustomobject]@{ Root = $root; Node = $node; Npm = $npm; CMake = $cmakePath;
        CTest = $ctestPath; Compiler = $compilerPath; Generator = $Generator;
        MakeProgram = $makeProgram; Simulator = $simulator; NodeVersion = $version }
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    try {
        $environment = Get-ProjectEnvironment -RequireBuild:$RequireBuild -Compiler $Compiler -Generator $Generator
        Write-Output "Environment: $([Environment]::OSVersion.VersionString); PowerShell $($PSVersionTable.PSVersion); Node $($environment.NodeVersion)"
        Write-Output "CMake: $($environment.CMake)"
        Write-Output "Compiler: $($environment.Compiler); Generator: $($environment.Generator); Ninja: $($environment.MakeProgram)"
        Write-Output "Simulator: $($environment.Simulator)"
        Write-Output 'Environment: PASS'
        exit 0
    } catch { Write-Error $_ -ErrorAction Continue; exit 5 }
}
