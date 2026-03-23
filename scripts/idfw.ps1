param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

$ErrorActionPreference = "Stop"

# @brief Release the COM port held by the simulator around idf actions.
# @details POSTs to the simulator HTTP API to force-release the serial link.
# All errors are suppressed so the command proceeds even when the sim is not running.
function Invoke-SimulatorComRelease {
    try {
        Invoke-WebRequest -Uri 'http://localhost:8000/api/transport/release-com' `
            -Method Post -TimeoutSec 3 -UseBasicParsing -ErrorAction SilentlyContinue | Out-Null
    } catch { }
    Start-Sleep -Milliseconds 500
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $root ".idfbuild"

. (Join-Path $PSScriptRoot "setup_idf_env.ps1")

$effectiveArgs = @()
if (-not ($IdfArgs -contains "-B")) {
    $effectiveArgs += @("-B", $buildDir)
}

$effectiveArgs += $IdfArgs

$needsComRelease = ($IdfArgs.Count -eq 0) -or ($IdfArgs -contains 'build') -or ($IdfArgs -contains 'flash') -or ($IdfArgs -contains 'monitor')
$exitCode = 1

try {
    if ($needsComRelease) {
        Write-Host "Releasing COM port before idf action..."
        Invoke-SimulatorComRelease
    }

    idf.py @effectiveArgs
    $exitCode = $LASTEXITCODE
} finally {
    if ($needsComRelease) {
        Write-Host "Releasing COM port after idf action..."
        Invoke-SimulatorComRelease
    }
}

exit $exitCode
