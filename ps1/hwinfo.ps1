$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only hardware snapshot: device, CPU, mainboard/BIOS, memory modules and GPU.
# WMI/CIM classes are language-independent, so no localized text is parsed here.
# Output is a single JSON line prefixed with HWINFO_JSON: for the Qt side.

function Clean([object]$value) {
    if ($null -eq $value) { return '' }
    return ([string]$value).Trim()
}

# ---- device ---------------------------------------------------------------
$system = Get-CimInstance -ClassName Win32_ComputerSystem
$device = [pscustomobject]@{
    manufacturer = Clean $system.Manufacturer
    model        = Clean $system.Model
    name         = Clean $system.Name
}

# ---- CPU ------------------------------------------------------------------
$cpuList = New-Object System.Collections.ArrayList
foreach ($c in @(Get-CimInstance -ClassName Win32_Processor)) {
    [void]$cpuList.Add([pscustomobject]@{
        name    = Clean $c.Name
        cores   = [int]$c.NumberOfCores
        threads = [int]$c.NumberOfLogicalProcessors
        clock   = [int]$c.MaxClockSpeed
        socket  = Clean $c.SocketDesignation
    })
}

# ---- mainboard / BIOS -----------------------------------------------------
$board = $null
$baseBoard = Get-CimInstance -ClassName Win32_BaseBoard
if ($baseBoard) {
    $bios = Get-CimInstance -ClassName Win32_BIOS
    $vendor = ''
    $version = ''
    $date = ''
    if ($bios) {
        $vendor = Clean $bios.Manufacturer
        $version = Clean $bios.SMBIOSBIOSVersion
        if ($bios.ReleaseDate) {
            $date = ([datetime]$bios.ReleaseDate).ToString('yyyy-MM-dd')
        }
    }
    $board = [pscustomobject]@{
        manufacturer = Clean $baseBoard.Manufacturer
        product      = Clean $baseBoard.Product
        version      = Clean $baseBoard.Version
        bios_vendor  = $vendor
        bios_version = $version
        bios_date    = $date
    }
}

# ---- memory ---------------------------------------------------------------
$memList = New-Object System.Collections.ArrayList
$totalBytes = [int64]0
foreach ($m in @(Get-CimInstance -ClassName Win32_PhysicalMemory)) {
    $bytes = [int64]$m.Capacity
    $totalBytes += $bytes
    [void]$memList.Add([pscustomobject]@{
        capacity_gb  = [int][math]::Round([double]$bytes / 1GB, 0)
        speed        = [int]$m.Speed
        manufacturer = Clean $m.Manufacturer
        part         = Clean $m.PartNumber
        slot         = Clean $m.DeviceLocator
    })
}
$memory = [pscustomobject]@{
    total_gb = [int][math]::Round([double]$totalBytes / 1GB, 0)
    modules  = @($memList)
}

# ---- GPU ------------------------------------------------------------------
$gpuList = New-Object System.Collections.ArrayList
foreach ($g in @(Get-CimInstance -ClassName Win32_VideoController)) {
    # AdapterRAM is a 32-bit field and caps near 4 GB; keep it as a hint only.
    $vram = $null
    if ($g.AdapterRAM) { $vram = [int][math]::Round([double]$g.AdapterRAM / 1MB, 0) }

    $resolution = ''
    if ($g.CurrentHorizontalResolution -and $g.CurrentVerticalResolution) {
        $resolution = '{0}x{1}' -f $g.CurrentHorizontalResolution, $g.CurrentVerticalResolution
    }

    $driverDate = ''
    if ($g.DriverDate) {
        $driverDate = ([datetime]$g.DriverDate).ToString('yyyy-MM-dd')
    }

    [void]$gpuList.Add([pscustomobject]@{
        name           = Clean $g.Name
        vram_mb        = $vram
        driver_version = Clean $g.DriverVersion
        driver_date    = $driverDate
        resolution     = $resolution
    })
}

$data = [pscustomobject]@{
    device = $device
    cpu    = @($cpuList)
    board  = $board
    memory = $memory
    gpu    = @($gpuList)
}

Write-Output ('HWINFO_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 5))
