$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$versionFile = Join-Path $root "VERSION"

if (-not (Test-Path $versionFile)) {
    throw "VERSION file not found."
}

$current = (Get-Content $versionFile -Raw).Trim()
if ($current -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    throw "Invalid VERSION format: $current"
}

$major = [int]$Matches[1]
$minor = [int]$Matches[2]
$patch = [int]$Matches[3] + 1
$next = "$major.$minor.$patch"

Set-Content -Path $versionFile -Value $next -NoNewline
Write-Host "Version bumped: $current -> $next"
