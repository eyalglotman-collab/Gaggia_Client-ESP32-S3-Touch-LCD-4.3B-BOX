param(
    [string]$Port = "COM9",
    [int]$BaudRate = 115200,
    [int]$DurationSec = 10
)

$ErrorActionPreference = "Stop"

# @brief Release the COM port held by the simulator before opening it for capture.
# @details POSTs to the simulator HTTP API to force-release the serial link.
# All errors are suppressed so monitor proceeds even when the sim is not running.
function Invoke-SimulatorComRelease {
    try {
        Invoke-WebRequest -Uri 'http://localhost:8000/api/transport/release-com' `
            -Method Post -TimeoutSec 3 -UseBasicParsing -ErrorAction SilentlyContinue | Out-Null
    } catch { }
    Start-Sleep -Milliseconds 500
}

Write-Host "Releasing COM port before monitor..."
Invoke-SimulatorComRelease

if (-not ([System.Management.Automation.PSTypeName]'System.IO.Ports.SerialPort').Type) {
    try {
        Add-Type -AssemblyName System.IO.Ports
    } catch {
        Add-Type -AssemblyName System
    }
}

# Capture raw serial output for a fixed time without resetting the board.
# DTR/RTS stay low so attaching does not reset the ESP32.
$serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 200
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"

$buffer = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($DurationSec)

try {
    $serial.Open()
    Start-Sleep -Milliseconds 100

    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $serial.ReadExisting()
            if (-not [string]::IsNullOrEmpty($chunk)) {
                [void]$buffer.Append($chunk)
            }
        } catch {
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    if ($serial.IsOpen) {
        try {
            $serial.DiscardInBuffer()
        } catch {
        }
        try {
            $serial.DiscardOutBuffer()
        } catch {
        }
        $serial.Close()
    }
    try {
        $serial.BaseStream.Close()
    } catch {
    }
    $serial.Dispose()
    $serial = $null
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
    Start-Sleep -Milliseconds 200
}

$buffer.ToString()
