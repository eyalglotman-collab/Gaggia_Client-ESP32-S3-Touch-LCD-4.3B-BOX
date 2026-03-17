param(
    [string]$IdfPath = "C:\Espressif\.espressif\v5.5.2\esp-idf",
    [string]$IdfToolsPath = "C:\Espressif"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$cacheDir = Join-Path $root ".cache"
$buildDir = Join-Path $root ".idfbuild"
$idfToolsPy = Join-Path $IdfPath "tools\idf_tools.py"
$pythonCandidates = @(
    (Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"),
    "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe",
    "C:\Espressif\frameworks\esp-idf-v5.5.2\.venv\Scripts\python.exe",
    "C:\Espressif\Eyal_Projects_ESP32_S3\Eyal_espresso_client\.venv\Scripts\python.exe"
)
$gitCandidates = @(
    ((Get-Command git -ErrorAction SilentlyContinue).Source),
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files\Git\bin\git.exe"
)
$toolBinCandidates = @()
$extraPaths = @(
    (Join-Path $IdfPath "components\espcoredump"),
    (Join-Path $IdfPath "components\partition_table"),
    (Join-Path $IdfPath "components\app_update")
) -join ";"

function Get-LatestToolBinDir {
    param(
        [string]$ToolRoot,
        [string]$ExeName
    )

    if (-not (Test-Path $ToolRoot)) {
        return $null
    }

    $match = Get-ChildItem -Path $ToolRoot -Recurse -Filter $ExeName -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($null -eq $match) {
        return $null
    }

    return Split-Path -Parent $match.FullName
}

if (-not (Test-Path $idfToolsPy)) {
    throw "ESP-IDF tools script not found at '$idfToolsPy'. Update -IdfPath."
}

New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $cacheDir "Espressif\ComponentManager") | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$env:XDG_CACHE_HOME = $cacheDir
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:IDF_PATH = $IdfPath

$pythonCommand = $null
foreach ($candidate in $pythonCandidates) {
    if ($candidate -and (Test-Path $candidate)) {
        $pythonCommand = $candidate
        break
    }
}

if (-not $pythonCommand -or -not (Test-Path $pythonCommand)) {
    throw "Configured ESP-IDF Python executable not found: '$pythonCommand'"
}

$gitCommand = $null
foreach ($candidate in $gitCandidates) {
    if ($candidate -and (Test-Path $candidate)) {
        $gitCommand = $candidate
        break
    }
}

$pythonDir = Split-Path -Parent $pythonCommand
$env:IDF_PYTHON_ENV_PATH = Split-Path -Parent $pythonDir

if ($env:PYTHONPATH) {
    $env:PYTHONPATH = $null
}

if ($env:PYTHONHOME) {
    $env:PYTHONHOME = $null
}

if (-not $env:PYTHONNOUSERSITE) {
    $env:PYTHONNOUSERSITE = "True"
}

if ($gitCommand) {
    $gitDir = Split-Path -Parent $gitCommand
    $gitRoot = Split-Path -Parent $gitDir
    $gitMingwBin = Join-Path $gitRoot "mingw64\bin"
    $gitUsrBin = Join-Path $gitRoot "usr\bin"
    $pathParts = @($pythonDir, $gitDir, $IdfToolsPath)
    if (Test-Path $gitMingwBin) {
        $pathParts += $gitMingwBin
    }
    if (Test-Path $gitUsrBin) {
        $pathParts += $gitUsrBin
    }
    $env:PATH = (($pathParts -join ";") + ";$env:PATH")
} else {
    $env:PATH = "$pythonDir;$IdfToolsPath;$env:PATH"
}

$toolBinCandidates += Get-LatestToolBinDir -ToolRoot "C:\Espressif\tools\cmake" -ExeName "cmake.exe"
$toolBinCandidates += Get-LatestToolBinDir -ToolRoot "C:\Espressif\tools\ninja" -ExeName "ninja.exe"
$toolBinCandidates += Get-LatestToolBinDir -ToolRoot "C:\Espressif\tools\xtensa-esp-elf" -ExeName "xtensa-esp32s3-elf-gcc.exe"
$toolBinCandidates += Get-LatestToolBinDir -ToolRoot "C:\Espressif\tools\riscv32-esp-elf" -ExeName "riscv32-esp-elf-gcc.exe"
$toolBinCandidates += Get-LatestToolBinDir -ToolRoot "C:\Espressif\tools\ccache" -ExeName "ccache.exe"

foreach ($toolBin in ($toolBinCandidates | Where-Object { $_ } | Select-Object -Unique)) {
    $env:PATH = "$toolBin;$env:PATH"
}

$envarsRaw = & $pythonCommand $idfToolsPy export --format key-value --add_paths_extras $extraPaths
if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
    throw "ESP-IDF tools export failed with exit code $LASTEXITCODE."
}

foreach ($line in $envarsRaw) {
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }

    $pair = $line -split "=", 2
    if ($pair.Length -ne 2) {
        continue
    }

    Set-Item -Path "Env:$($pair[0].Trim())" -Value $pair[1].Trim()
}

Set-Alias -Name python -Value $pythonCommand -Scope Global

function global:idf.py {
    & $pythonCommand "$IdfPath\tools\idf.py" @args
}

Write-Host "ESP-IDF environment ready"
Write-Host "IDF_PATH=$env:IDF_PATH"
Write-Host "IDF_PYTHON_ENV_PATH=$env:IDF_PYTHON_ENV_PATH"
Write-Host "XDG_CACHE_HOME=$env:XDG_CACHE_HOME"
Write-Host "Build dir: $buildDir"
Write-Host "Run: idf.py -B `"$buildDir`" -DIDF_TARGET=esp32s3 reconfigure"
Write-Host "Run: idf.py -B `"$buildDir`" build"
Write-Host "Run: idf.py -B `"$buildDir`" -p <PORT> flash"
Write-Host "Run: idf.py -B `"$buildDir`" monitor --port <PORT> --no-reset"
