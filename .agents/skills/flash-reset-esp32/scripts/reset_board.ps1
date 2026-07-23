[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,

    [switch]$EnterBootloader,

    [ValidateRange(100, 2000)]
    [int]$ResetMilliseconds = 200,

    [ValidateRange(40, 500)]
    [int]$BootMilliseconds = 100
)

$ErrorActionPreference = "Stop"

$operation = if ($EnterBootloader) {
    "enter the ESP32 ROM bootloader"
} else {
    "reset the ESP32 application"
}

if (-not $PSCmdlet.ShouldProcess($Port, $operation)) {
    return
}

$serial = [System.IO.Ports.SerialPort]::new()
$serial.PortName = $Port
$serial.BaudRate = 115200
$serial.DataBits = 8
$serial.Parity = [System.IO.Ports.Parity]::None
$serial.StopBits = [System.IO.Ports.StopBits]::One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    try {
        $serial.Open()
    } catch {
        throw "Cannot open $Port. Close any serial monitor or uploader and retry. $($_.Exception.Message)"
    }

    # CH340C control-line levels after the board's documented auto-boot
    # modification. DTR remains inactive during a normal application reset.
    $serial.DtrEnable = $false
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds $ResetMilliseconds

    if ($EnterBootloader) {
        # Release EN while GPIO0 is active, hold it long enough for ROM sampling,
        # then return both control lines to their inactive state.
        $serial.DtrEnable = $true
        $serial.RtsEnable = $false
        Start-Sleep -Milliseconds $BootMilliseconds
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        Write-Host "$Port entered the ESP32 ROM bootloader."
    } else {
        $serial.RtsEnable = $false
        Write-Host "$Port application reset completed."
    }
} finally {
    if ($serial.IsOpen) {
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.Close()
    }
    $serial.Dispose()
}
