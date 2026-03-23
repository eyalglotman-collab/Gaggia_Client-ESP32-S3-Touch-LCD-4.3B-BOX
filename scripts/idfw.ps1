param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

$ErrorActionPreference = "Stop"

# @brief Release the COM port held by the simulator before flash or monitor.
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

if (($IdfArgs -contains 'flash') -or ($IdfArgs -contains 'monitor')) {
    Write-Host "Releasing COM port before $( if ($IdfArgs -contains 'flash') { 'flash' } else { 'monitor' } )..."
    Invoke-SimulatorComRelease
}

idf.py @effectiveArgs
exit $LASTEXITCODE
