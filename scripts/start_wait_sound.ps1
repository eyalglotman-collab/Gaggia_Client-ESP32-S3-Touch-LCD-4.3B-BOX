[CmdletBinding()]
param(
    [switch]$Worker,
    [int]$IntervalSeconds = 180
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LegacyPidFile = Join-Path $ProjectRoot ".cache\wait_sound.pid"
$SoundFile = Join-Path $ProjectRoot "sounds\WaitSound.wav"
$PlaybackScript = Join-Path $ProjectRoot "scripts\play_wait_sound.ps1"
$RegistryScript = Join-Path $ProjectRoot "scripts\sound_process_registry.ps1"

if (-not (Test-Path $RegistryScript)) {
    throw "Sound process registry helper not found: $RegistryScript"
}

. $RegistryScript

# @brief Check whether VS Code is still running on the host.
# @details The wait-sound worker exits automatically if no `Code` process is
# found, which prevents orphaned sound loops after the editor is closed.
function Test-VsCodeRunning {
    return [bool](Get-Process -Name Code -ErrorAction SilentlyContinue)
}

# @brief Play the configured wait sound once.
# @details Launches the playback helper in background mode and tracks the child
# by description so the notification path stays inspectable from the registry.
# @param[in] Description Human-readable reason for the playback request.
function Play-WaitSound {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "wait_play_request" `
        -Role "wait-playback" `
        -Description $Description `
        -ProcessId $PID `
        -Detail ("sound_file={0}" -f $SoundFile)

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PlaybackScript `
        -SoundFile $SoundFile `
        -Background `
        -Role "wait-playback" `
        -Description $Description | Out-Null
}

# @brief Run the repeating wait-sound loop.
# @details Sleeps for the configured interval, verifies that the current worker
# is still the registered wait-loop owner, and triggers repeated background
# playback until the worker is explicitly stopped.
# @param[in] IntervalSeconds Delay between wait-sound replays.
function Start-WaitLoop {
    param(
        [Parameter(Mandatory = $true)]
        [int]$IntervalSeconds
    )

    while ($true) {
        Start-Sleep -Seconds $IntervalSeconds
        if (-not (Test-VsCodeRunning)) {
            break
        }

        $registeredWorker = Get-RegisteredSoundProcesses -ProjectRoot $ProjectRoot -Roles @("wait-loop") |
            Where-Object { [int]$_.pid -eq $PID } |
            Select-Object -First 1
        if (-not $registeredWorker) {
            break
        }

        Play-WaitSound -Description "Repeated wait reminder playback"
    }
}

# @brief Start the detached wait-sound worker if one is not already running.
# @details Registers the worker in the sound-process registry with a descriptive
# role so stop helpers and diagnostics can identify it without relying on one
# opaque PID file.
function Start-WaitWorker {
    if (-not (Test-VsCodeRunning)) {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_worker_skipped" `
            -Role "wait-loop" `
            -Description "VS Code not running; worker start skipped" `
            -ProcessId $PID
        throw "VS Code is not running, so the wait sound worker will not be started."
    }

    $existingWorker = Get-RegisteredSoundProcesses -ProjectRoot $ProjectRoot -Roles @("wait-loop") | Select-Object -First 1
    if ($existingWorker) {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_worker_exists" `
            -Role "wait-loop" `
            -Description $existingWorker.description `
            -ProcessId ([int]$existingWorker.pid)
        Write-Output ("Wait sound worker already running with PID {0} - {1}" -f $existingWorker.pid, $existingWorker.description)
        return
    }

    Remove-Item $LegacyPidFile -Force -ErrorAction SilentlyContinue
    $proc = Start-Process powershell.exe -WindowStyle Hidden -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-Worker",
        "-IntervalSeconds", $IntervalSeconds
    ) -PassThru

    Register-SoundProcess -ProjectRoot $ProjectRoot `
        -ProcessId $proc.Id `
        -Role "wait-loop" `
        -Description "Repeating wait reminder loop" `
        -ScriptPath $PSCommandPath `
        -SoundFile $SoundFile `
        -Mode "loop"
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "wait_worker_started" `
        -Role "wait-loop" `
        -Description "Repeating wait reminder loop" `
        -ProcessId $proc.Id `
        -Detail ("interval_seconds={0}" -f $IntervalSeconds)

    Write-Output ("Started wait sound worker PID {0} [wait-loop] - Repeating wait reminder loop" -f $proc.Id)
}

if (-not (Test-Path $SoundFile)) {
    throw "Wait sound file not found: $SoundFile"
}
if (-not (Test-Path $PlaybackScript)) {
    throw "Wait playback helper not found: $PlaybackScript"
}

if ($Worker) {
    try {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_worker_enter" `
            -Role "wait-loop" `
            -Description "Repeating wait reminder loop" `
            -ProcessId $PID `
            -Detail ("interval_seconds={0}" -f $IntervalSeconds)
        Start-WaitLoop -IntervalSeconds $IntervalSeconds
        exit 0
    } finally {
        Write-SoundEvent -ProjectRoot $ProjectRoot `
            -EventType "wait_worker_exit" `
            -Role "wait-loop" `
            -Description "Repeating wait reminder loop" `
            -ProcessId $PID
        Unregister-SoundProcess -ProjectRoot $ProjectRoot -ProcessId $PID
    }
}

Play-WaitSound -Description "Immediate wait notification playback"
Start-WaitWorker
