[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LegacyPidFile = Join-Path $ProjectRoot ".cache\wait_sound.pid"
$RegistryScript = Join-Path $ProjectRoot "scripts\sound_process_registry.ps1"

if (-not (Test-Path $RegistryScript)) {
    throw "Sound process registry helper not found: $RegistryScript"
}

. $RegistryScript

# @brief Stop all tracked wait-sound processes.
# @details Terminates the repeating worker and any in-flight wait playback
# helpers, then removes the legacy PID file kept for backward compatibility.
function Stop-WaitProcesses {
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "wait_stop_request" `
        -Role "wait-loop" `
        -Description "Stop all tracked wait sound processes" `
        -ProcessId $PID

    $stopped = @(Stop-RegisteredSoundProcesses -ProjectRoot $ProjectRoot -Roles @("wait-loop", "wait-playback"))

    foreach ($entry in $stopped) {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_stopped" `
            -Role $entry.role `
            -Description $entry.description `
            -ProcessId ([int]$entry.pid)
        Write-Output ("Stopped sound PID {0} [{1}] - {2}" -f $entry.pid, $entry.role, $entry.description)
    }

    if ($stopped.Count -eq 0) {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_stop_noop" `
            -Role "wait-loop" `
            -Description "No tracked wait sound processes were running" `
            -ProcessId $PID
        Write-Output "No tracked wait sound processes are running."
    }

    Remove-Item $LegacyPidFile -Force -ErrorAction SilentlyContinue
}

Stop-WaitProcesses
