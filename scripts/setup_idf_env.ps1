param(
    [string]$IdfPath = "C:\Espressif\.espressif\v5.5.2\esp-idf"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$cacheDir = Join-Path $root ".cache"
$buildDir = Join-Path $root ".idfbuild"
$exportScript = Join-Path $IdfPath "export.ps1"

if (-not (Test-Path $exportScript)) {
    throw "ESP-IDF export script not found at '$exportScript'. Update -IdfPath."
}

New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $cacheDir "Espressif\ComponentManager") | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$env:XDG_CACHE_HOME = $cacheDir

. $exportScript

Write-Host "ESP-IDF environment ready"
Write-Host "IDF_PATH=$env:IDF_PATH"
Write-Host "XDG_CACHE_HOME=$env:XDG_CACHE_HOME"
Write-Host "Build dir: $buildDir"
Write-Host "Run: idf.py -B `"$buildDir`" -DIDF_TARGET=esp32s3 reconfigure"
Write-Host "Run: idf.py -B `"$buildDir`" build"
Write-Host "Run: idf.py -B `"$buildDir`" -p <PORT> flash"
Write-Host "Run: idf.py -B `"$buildDir`" monitor --port <PORT> --no-reset"
