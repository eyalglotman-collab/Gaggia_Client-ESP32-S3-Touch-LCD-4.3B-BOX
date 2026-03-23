$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$startWaitScript = Join-Path $PSScriptRoot "start_wait_sound.ps1"
$stopWaitScript = Join-Path $PSScriptRoot "stop_wait_sound.ps1"
$registryScript = Join-Path $PSScriptRoot "sound_process_registry.ps1"

if (Test-Path $registryScript) {
    . $registryScript
}

& (Join-Path $PSScriptRoot "idfw.ps1") -DIDF_TARGET=esp32s3 reconfigure build flash
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Build and flash completed successfully."
if (Test-Path $startWaitScript) {
    if (Get-Command Write-SoundEvent -ErrorAction SilentlyContinue) {
        Write-SoundEvent -ProjectRoot $root `
            -EventType "prompt_wait_begin" `
            -Role "wait-loop" `
            -Description "Build and flash prompt waiting for user response" `
            -ProcessId $PID
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $startWaitScript | Out-Null
}

try {
    $reply = Read-Host "Commit current changes and create sub-version (bump Z)? [y/N]"
    if (Get-Command Write-SoundEvent -ErrorAction SilentlyContinue) {
        Write-SoundEvent -ProjectRoot $root `
            -EventType "prompt_wait_end" `
            -Role "wait-loop" `
            -Description "Build and flash prompt received user response" `
            -ProcessId $PID `
            -Detail ("reply={0}" -f $reply)
    }
} finally {
    if (Test-Path $stopWaitScript) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $stopWaitScript | Out-Null
    }
}

if ($reply -match '^[Yy]$') {
    & (Join-Path $PSScriptRoot "bump_subversion.ps1")
    git add -A
    $version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
    git commit -m "Release v$version"
    Write-Host "Committed release v$version."
} else {
    Write-Host "No commit/version bump performed."
}
