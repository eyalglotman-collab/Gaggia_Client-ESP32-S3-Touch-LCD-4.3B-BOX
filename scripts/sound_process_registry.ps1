[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# @brief Return the registry directory used for tracked sound processes.
# @details Uses one JSON file per tracked process so concurrent background
# workers and one-shot playback helpers can register and unregister without
# rewriting one shared state file.
# @param[in] ProjectRoot Repository root path.
# @return Absolute registry directory path.
function Get-SoundProcessRegistryDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    return (Join-Path $ProjectRoot ".cache\sound_processes")
}

# @brief Return the metadata file path for one tracked sound process.
# @details Stores each process under `<pid>.json` so the registry remains easy
# to inspect manually from the filesystem.
# @param[in] ProjectRoot Repository root path.
# @param[in] ProcessId Process ID to map.
# @return Absolute metadata file path.
function Get-SoundProcessMetadataPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )

    $registryDir = Get-SoundProcessRegistryDirectory -ProjectRoot $ProjectRoot
    return (Join-Path $registryDir ("{0}.json" -f $ProcessId))
}

# @brief Check whether a tracked process is still alive.
# @details A missing process means its metadata file is stale and should be
# removed from the registry on the next cleanup pass.
# @param[in] ProcessId Process ID to inspect.
# @return `true` when the process exists, otherwise `false`.
function Test-SoundProcessAlive {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )

    return ($null -ne (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue))
}

# @brief Register one sound-related background process.
# @details Writes descriptive metadata so later polling can explain which PID
# is a loop worker, wait notification, or build-success playback helper.
# @param[in] ProjectRoot Repository root path.
# @param[in] ProcessId Process ID to register.
# @param[in] Role Stable machine-readable role name.
# @param[in] Description Human-readable process description.
# @param[in] ScriptPath Script associated with the process.
# @param[in] SoundFile Optional sound file path.
# @param[in] Mode Optional execution mode name.
function Register-SoundProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [int]$ProcessId,
        [Parameter(Mandatory = $true)]
        [string]$Role,
        [Parameter(Mandatory = $true)]
        [string]$Description,
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath,
        [string]$SoundFile = "",
        [string]$Mode = ""
    )

    $registryDir = Get-SoundProcessRegistryDirectory -ProjectRoot $ProjectRoot
    New-Item -ItemType Directory -Path $registryDir -Force | Out-Null

    $metadata = [ordered]@{
        pid = $ProcessId
        role = $Role
        description = $Description
        script_path = $ScriptPath
        sound_file = $SoundFile
        mode = $Mode
        started_at_utc = [DateTime]::UtcNow.ToString("o")
    }

    $metadataPath = Get-SoundProcessMetadataPath -ProjectRoot $ProjectRoot -ProcessId $ProcessId
    Set-Content -Path $metadataPath -Value ($metadata | ConvertTo-Json -Depth 4) -Encoding UTF8
}

# @brief Remove one tracked sound-process metadata file.
# @details This is safe to call from process exit paths even when the metadata
# file was already removed by a stop helper or stale cleanup pass.
# @param[in] ProjectRoot Repository root path.
# @param[in] ProcessId Process ID to unregister.
function Unregister-SoundProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )

    $metadataPath = Get-SoundProcessMetadataPath -ProjectRoot $ProjectRoot -ProcessId $ProcessId
    Remove-Item $metadataPath -Force -ErrorAction SilentlyContinue
}

# @brief Remove stale metadata files for already-exited sound processes.
# @details Keeps the registry folder self-healing even after crashes or hard
# process kills.
# @param[in] ProjectRoot Repository root path.
function Remove-StaleSoundProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $registryDir = Get-SoundProcessRegistryDirectory -ProjectRoot $ProjectRoot
    if (-not (Test-Path $registryDir)) {
        return
    }

    foreach ($metadataFile in (Get-ChildItem -Path $registryDir -Filter *.json -File -ErrorAction SilentlyContinue)) {
        $pidText = [System.IO.Path]::GetFileNameWithoutExtension($metadataFile.Name)
        $processId = 0
        if (-not [int]::TryParse($pidText, [ref]$processId)) {
            Remove-Item $metadataFile.FullName -Force -ErrorAction SilentlyContinue
            continue
        }

        if (-not (Test-SoundProcessAlive -ProcessId $processId)) {
            Remove-Item $metadataFile.FullName -Force -ErrorAction SilentlyContinue
        }
    }
}

# @brief Read the currently tracked sound processes.
# @details Returns descriptive metadata objects that can be filtered by role
# and used by start/stop helpers to avoid duplicate workers.
# @param[in] ProjectRoot Repository root path.
# @param[in] Roles Optional set of role names to keep.
# @return Array of tracked metadata objects.
function Get-RegisteredSoundProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [string[]]$Roles = @()
    )

    Remove-StaleSoundProcesses -ProjectRoot $ProjectRoot

    $registryDir = Get-SoundProcessRegistryDirectory -ProjectRoot $ProjectRoot
    if (-not (Test-Path $registryDir)) {
        return @()
    }

    $items = @()
    foreach ($metadataFile in (Get-ChildItem -Path $registryDir -Filter *.json -File -ErrorAction SilentlyContinue)) {
        $entry = Get-Content $metadataFile.FullName -Raw | ConvertFrom-Json
        if ($Roles.Count -gt 0 -and $entry.role -notin $Roles) {
            continue
        }
        $items += $entry
    }

    return $items
}

# @brief Stop one or more tracked sound processes by role.
# @details Terminates matching background workers and removes their metadata so
# later polling reflects the current live set only.
# @param[in] ProjectRoot Repository root path.
# @param[in] Roles Role names to stop.
# @return Array of stopped or cleaned metadata objects.
function Stop-RegisteredSoundProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string[]]$Roles
    )

    $stopped = @()
    foreach ($entry in (Get-RegisteredSoundProcesses -ProjectRoot $ProjectRoot -Roles $Roles)) {
        if (Test-SoundProcessAlive -ProcessId ([int]$entry.pid)) {
            Stop-Process -Id ([int]$entry.pid) -Force -ErrorAction SilentlyContinue
        }
        Unregister-SoundProcess -ProjectRoot $ProjectRoot -ProcessId ([int]$entry.pid)
        $stopped += $entry
    }

    return $stopped
}
