[CmdletBinding()]
param(
    [switch]$Worker,
    [int]$IntervalSeconds = 180
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PidFile = Join-Path $ProjectRoot ".cache\wait_sound.pid"
$SoundFile = Join-Path $ProjectRoot "sounds\WaitSound.mp3"

# @brief Play the configured wait sound once.
# @details Uses WPF MediaPlayer so MP3 playback works on the local Windows host.
# The helper waits briefly for metadata before stopping the player.
function Play-WaitSound {
    Add-Type -AssemblyName presentationCore
    $player = New-Object System.Windows.Media.MediaPlayer
    $player.Open([Uri](Resolve-Path $SoundFile))
    Start-Sleep -Milliseconds 700
    $duration = $player.NaturalDuration.TimeSpan.TotalSeconds
    if (-not $duration -or $duration -le 0) {
        $duration = 4
    }

    $player.Volume = 1.0
    $player.Play()
    Start-Sleep -Seconds ([Math]::Ceiling($duration) + 1)
    $player.Stop()
    $player.Close()
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

Start-WaitWorker
