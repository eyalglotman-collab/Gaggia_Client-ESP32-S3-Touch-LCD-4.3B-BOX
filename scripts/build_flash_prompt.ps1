$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")

& (Join-Path $PSScriptRoot "idfw.ps1") -DIDF_TARGET=esp32s3 reconfigure build flash
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Build and flash completed successfully."
$reply = Read-Host "Commit current changes and create sub-version (bump Z)? [y/N]"

if ($reply -match '^[Yy]$') {
    & (Join-Path $PSScriptRoot "bump_subversion.ps1")
    git add -A
    $version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
    git commit -m "Release v$version"
    Write-Host "Committed release v$version."
} else {
    Write-Host "No commit/version bump performed."
}
