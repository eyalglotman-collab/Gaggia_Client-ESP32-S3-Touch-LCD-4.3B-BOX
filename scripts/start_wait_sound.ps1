[CmdletBinding()]
param(
    [switch]$Worker,
    [int]$IntervalSeconds = 180
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PidFile = Join-Path $ProjectRoot ".cache\wait_sound.pid"
$SoundFile = Join-Path $ProjectRoot "sounds\WaitSound.wav"

# @brief Check whether VS Code is still running on the host.
# @details The wait-sound worker exits automatically if no `Code` process is
# found, which prevents orphaned sound loops after the editor is closed.
function Test-VsCodeRunning {
    return [bool](Get-Process -Name Code -ErrorAction SilentlyContinue)
}

# @brief Play the configured wait sound once.
# @details Uses `System.Media.SoundPlayer` for reliable WAV playback on the
# local Windows host.
function Play-WaitSound {
    Add-Type -AssemblyName System
    $player = New-Object System.Media.SoundPlayer $SoundFile
    $player.PlaySync()
}

# @brief Run the repeating wait-sound loop.
# @details Sleeps for the configured interval, re-checks the PID handle, and
# only continues while the handle still points at the current worker process.
# @param[in] IntervalSeconds Delay between wait-sound replays.
function Start-WaitLoop {
    param(
        [Parameter(Mandatory = $true)]
        [int]$IntervalSeconds
    )

    while ($true) {
        Start-Sleep -Seconds $IntervalSeconds
        if (-not (Test-VsCodeRunning)) {
            Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
            break
        }

        if (-not (Test-Path $PidFile)) {
            break
        }

        $trackedPid = (Get-Content $PidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($trackedPid -ne "$PID") {
            break
        }

        Play-WaitSound
    }
}

# @brief Start the detached wait-sound worker if one is not already running.
# @details Persists the worker PID in `.cache/wait_sound.pid` so a separate
# stop helper can terminate it as soon as user interaction resumes.
function Start-WaitWorker {
    if (-not (Test-VsCodeRunning)) {
        throw "VS Code is not running, so the wait sound worker will not be started."
    }

    if (Test-Path $PidFile) {
        $existingPid = (Get-Content $PidFile -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($existingPid) {
            $existing = Get-Process -Id $existingPid -ErrorAction SilentlyContinue
            if ($existing) {
                Write-Output "Wait sound worker already running with PID $existingPid."
                return
            }
        }
        Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
    }

    New-Item -ItemType Directory -Force (Split-Path -Parent $PidFile) | Out-Null
    $command = "& '$PSCommandPath' -Worker -IntervalSeconds $IntervalSeconds"
    $proc = Start-Process powershell.exe -ArgumentList @("-NoProfile", "-WindowStyle", "Hidden", "-ExecutionPolicy", "Bypass", "-Command", $command) -PassThru
    Set-Content -Path $PidFile -Value $proc.Id -NoNewline
    Write-Output "Started wait sound worker PID $($proc.Id)."
}

if (-not (Test-Path $SoundFile)) {
    throw "Wait sound file not found: $SoundFile"
}

if ($Worker) {
    Start-WaitLoop -IntervalSeconds $IntervalSeconds
    exit 0
}

Play-WaitSound
Start-WaitWorker
