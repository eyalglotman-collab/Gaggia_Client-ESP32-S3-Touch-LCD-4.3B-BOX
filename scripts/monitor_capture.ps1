param(
    [string]$Port = "COM9",
    [int]$BaudRate = 115200,
    [int]$DurationSec = 10
)

$ErrorActionPreference = "Stop"

if (-not ([System.Management.Automation.PSTypeName]'System.IO.Ports.SerialPort').Type) {
    try {
        Add-Type -AssemblyName System.IO.Ports
    } catch {
        Add-Type -AssemblyName System
    }
}

# @brief Release the COM port held by the simulator around monitor capture.
# @details POSTs to the simulator HTTP API to force-release the serial link.
# All errors are suppressed so monitor proceeds even when the sim is not running.
function Invoke-SimulatorComRelease {
    try {
        Invoke-WebRequest -Uri 'http://localhost:8000/api/transport/release-com' `
            -Method Post -TimeoutSec 3 -UseBasicParsing -ErrorAction SilentlyContinue | Out-Null
    } catch { }
    Start-Sleep -Milliseconds 500
}

# @brief Probe whether a COM port can be opened right now.
# @details Opens/closes a short-lived SerialPort probe with DTR/RTS disabled.
# Returns `$true` when open succeeds; otherwise `$false`.
function Test-ComPortAvailable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName,
        [int]$ProbeBaudRate = 115200
    )

    $probe = $null
    try {
        $probe = New-Object System.IO.Ports.SerialPort $PortName, $ProbeBaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
        $probe.ReadTimeout = 100
        $probe.WriteTimeout = 100
        $probe.DtrEnable = $false
        $probe.RtsEnable = $false
        $probe.Open()
        return $true
    } catch {
        return $false
    } finally {
        if ($probe -ne $null) {
            if ($probe.IsOpen) {
                try { $probe.Close() } catch { }
            }
            try { $probe.Dispose() } catch { }
        }
    }
}

# @brief Ensure the COM port is released, retrying simulator release if needed.
# @details Checks availability first. If busy, requests release and rechecks.
# Throws to caller only when all attempts are exhausted.
function Ensure-ComPortReleased {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PortName,
        [int]$ProbeBaudRate = 115200,
        [int]$MaxAttempts = 4
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        if (Test-ComPortAvailable -PortName $PortName -ProbeBaudRate $ProbeBaudRate) {
            if ($attempt -eq 1) {
                Write-Host "Port $PortName is free."
            } else {
                Write-Host "Port $PortName became free after release attempt $($attempt - 1)."
            }
            return $true
        }

        if ($attempt -lt $MaxAttempts) {
            Write-Host "Port $PortName is busy. Requesting simulator release (attempt $attempt/$($MaxAttempts - 1))..."
            Invoke-SimulatorComRelease
        }
    }

    return $false
}

$serial = $null
$buffer = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($DurationSec)

try {
    Write-Host "Checking COM port state before monitor..."
    if (-not (Ensure-ComPortReleased -PortName $Port -ProbeBaudRate $BaudRate -MaxAttempts 4)) {
        throw "COM port '$Port' is still busy after release attempts."
    }

    # Capture raw serial output for a fixed time without resetting the board.
    # DTR/RTS stay low so attaching does not reset the ESP32.
    $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 200
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.NewLine = "`n"

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
        if (($serial -ne $null) -and $serial.IsOpen) {
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
        if ($serial -ne $null) {
            try {
                $serial.BaseStream.Close()
            } catch {
            }
            $serial.Dispose()
            $serial = $null
        }
        [System.GC]::Collect()
        [System.GC]::WaitForPendingFinalizers()
        Start-Sleep -Milliseconds 200

        Write-Host "Releasing COM port after monitor..."
        Invoke-SimulatorComRelease

        Write-Host "Verifying COM port state after monitor..."
        if (-not (Ensure-ComPortReleased -PortName $Port -ProbeBaudRate $BaudRate -MaxAttempts 3)) {
            throw "Post-monitor check failed: COM port '$Port' is still busy."
        }
    }

    $buffer.ToString()
    exit 0
} catch {
    $message = $_.Exception.Message
    if ([string]::IsNullOrWhiteSpace($message)) {
        $message = $_.ToString()
    }
    Write-Host "Monitor capture failed: $message"
    exit 1
}
