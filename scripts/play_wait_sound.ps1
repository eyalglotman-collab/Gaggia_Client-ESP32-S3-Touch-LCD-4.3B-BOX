[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SoundFile,
    [switch]$Background,
    [string]$Role = "sound-once",
    [string]$Description = "Background sound playback"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$RegistryScript = Join-Path $ProjectRoot "scripts\sound_process_registry.ps1"

if (-not (Test-Path $RegistryScript)) {
    throw "Sound process registry helper not found: $RegistryScript"
}

. $RegistryScript

# @brief Start one hidden background playback helper.
# @details Launches the same script in non-background mode, registers the child
# PID with descriptive metadata, and returns immediately to the caller.
# @param[in] SoundFile Absolute or relative sound file path.
# @param[in] Role Stable role name for registry filtering.
# @param[in] Description Human-readable process description.
function Start-BackgroundPlayback {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SoundFile,
        [Parameter(Mandatory = $true)]
        [string]$Role,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $child = Start-Process powershell.exe -WindowStyle Hidden -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-SoundFile", $SoundFile,
        "-Role", $Role,
        "-Description", $Description
    ) -PassThru

    Register-SoundProcess -ProjectRoot $ProjectRoot `
        -ProcessId $child.Id `
        -Role $Role `
        -Description $Description `
        -ScriptPath $PSCommandPath `
        -SoundFile $SoundFile `
        -Mode "one-shot"
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "background_start" `
        -Role $Role `
        -Description $Description `
        -ProcessId $child.Id `
        -Detail ("sound_file={0}" -f $SoundFile)

    Write-Output ("Started sound process PID {0} [{1}] - {2}" -f $child.Id, $Role, $Description)
}

# @brief Play one sound synchronously.
# @details Tries multiple Windows playback backends so notification playback is
# more reliable across local host audio configurations. Falls back to a console
# beep if file playback backends fail.
if (-not (Test-Path $SoundFile)) {
    throw "Sound file not found: $SoundFile"
}

$resolvedSoundFile = (Resolve-Path $SoundFile).Path

if ($Background) {
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "background_request" `
        -Role $Role `
        -Description $Description `
        -ProcessId $PID `
        -Detail ("sound_file={0}" -f $resolvedSoundFile)
    Start-BackgroundPlayback -SoundFile $resolvedSoundFile -Role $Role -Description $Description
    exit 0
}

$playSucceeded = $false
$lastError = $null
$backendUsed = ""
$player = $null
$mediaPlayer = $null
$state = 0

try {
    try {
        Add-Type -AssemblyName System
        $player = New-Object System.Media.SoundPlayer $resolvedSoundFile
        $player.Load()
        $player.PlaySync()
        $playSucceeded = $true
        $backendUsed = "System.Media.SoundPlayer"
    } catch {
        $lastError = $_
    }

    try {
        if (-not $playSucceeded) {
            $mediaPlayer = New-Object -ComObject WMPlayer.OCX
            $mediaPlayer.settings.volume = 100
            $mediaPlayer.URL = $resolvedSoundFile
            $mediaPlayer.controls.play()
            $deadline = (Get-Date).AddSeconds(15)
            do {
                Start-Sleep -Milliseconds 100
                $state = $mediaPlayer.playState
            } while ((Get-Date) -lt $deadline -and $state -ne 1)
            $playSucceeded = $true
            $backendUsed = "WMPlayer.OCX"
        }
    } catch {
        $lastError = $_
    } finally {
        if ($null -ne $mediaPlayer) {
            try { $mediaPlayer.controls.stop() } catch {}
            try { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($mediaPlayer) } catch {}
        }
    }

    try {
        if (-not $playSucceeded) {
            [console]::Beep(1046, 180)
            Start-Sleep -Milliseconds 60
            [console]::Beep(1318, 240)
            $playSucceeded = $true
            $backendUsed = "Console.Beep"
        }
    } catch {
        $lastError = $_
    }

    if (-not $playSucceeded) {
        throw "All sound playback backends failed. Last error: $lastError"
    }
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "play_success" `
        -Role $Role `
        -Description $Description `
        -ProcessId $PID `
        -Detail ("backend={0}; sound_file={1}" -f $backendUsed, $resolvedSoundFile)
} catch {
    Write-SoundEvent -ProjectRoot $ProjectRoot `
        -EventType "play_error" `
        -Role $Role `
        -Description $Description `
        -ProcessId $PID `
        -Detail ("sound_file={0}; error={1}" -f $resolvedSoundFile, $_.Exception.Message)
    throw
} finally {
    Unregister-SoundProcess -ProjectRoot $ProjectRoot -ProcessId $PID
}
