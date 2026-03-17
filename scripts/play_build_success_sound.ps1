[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PlaybackScript = Join-Path $ProjectRoot "scripts\play_wait_sound.ps1"
$SoundFile = Join-Path $ProjectRoot "sounds\build-success-monkey-1p5x.wav"

# @brief Play the project build-success sound once.
# @details Reuses the common playback helper so build notifications use the same
# backend selection and fallback behavior as wait notifications.
if (-not (Test-Path $PlaybackScript)) {
    throw "Playback helper not found: $PlaybackScript"
}

if (-not (Test-Path $SoundFile)) {
    throw "Build-success sound file not found: $SoundFile"
}

$RegistryScript = Join-Path $ProjectRoot "scripts\sound_process_registry.ps1"
if (Test-Path $RegistryScript) {
    . $RegistryScript
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "build_success_request" `
        -Role "build-success-playback" `
        -Description "Build success notification playback requested" `
        -ProcessId $PID `
        -Detail ("sound_file={0}" -f $SoundFile)
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PlaybackScript `
    -SoundFile $SoundFile `
    -Background `
    -Role "build-success-playback" `
    -Description "Build success notification playback"

if (-not $?) {
    $exitCodeVar = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    $exitCode = if ($null -ne $exitCodeVar) { [int]$exitCodeVar.Value } else { -1 }
    throw "Build-success playback helper failed with exit code $exitCode."
}
