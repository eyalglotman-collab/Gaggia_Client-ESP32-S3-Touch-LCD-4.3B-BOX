[CmdletBinding()]
param()

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
    $stopped = @(Stop-RegisteredSoundProcesses -ProjectRoot $ProjectRoot -Roles @("wait-loop", "wait-playback"))

    foreach ($entry in $stopped) {
        Write-Output ("Stopped sound PID {0} [{1}] - {2}" -f $entry.pid, $entry.role, $entry.description)
    }

    if ($stopped.Count -eq 0) {
        Write-Output "No tracked wait sound processes are running."
    }

    Remove-Item $LegacyPidFile -Force -ErrorAction SilentlyContinue
}

Stop-WaitProcesses
