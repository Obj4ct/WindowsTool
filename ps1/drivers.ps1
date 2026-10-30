$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only inventory of PnP device drivers: device name, device class,
# driver version, provider and driver date. Output is one JSON line prefixed
# with DRIVERS_JSON: for the Qt side.

$limit = 40
$items = New-Object System.Collections.ArrayList

foreach ($driver in @(Get-CimInstance -ClassName Win32_PnPSignedDriver -ErrorAction SilentlyContinue)) {
    $name = ([string]$driver.DeviceName).Trim()
    if ($name -eq '') { continue }

    $version = ([string]$driver.DriverVersion).Trim()
    if ($version -eq '') { continue }

    # DriverDate is a CIM datetime such as 20240115000000.000000-000
    $date = ''
    $rawDate = [string]$driver.DriverDate
    if ($rawDate.Length -ge 8 -and $rawDate.Substring(0, 8) -match '^\d{8}$') {
        $date = $rawDate.Substring(0, 4) + '-' + $rawDate.Substring(4, 2) + '-' + $rawDate.Substring(6, 2)
    }

    [void]$items.Add([pscustomobject]@{
        name         = $name
        class        = ([string]$driver.DeviceClass).Trim()
        version      = $version
        manufacturer = ([string]$driver.DriverProviderName).Trim()
        date         = $date
    })
}

# The same driver can be enumerated several times (e.g. Bluetooth profiles);
# keep one row per name + version + class.
$seen = @{}
$unique = New-Object System.Collections.ArrayList
foreach ($entry in $items) {
    $key = $entry.name + '|' + $entry.version + '|' + $entry.class
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = $true
    [void]$unique.Add($entry)
}

# Group by device class so similar devices stay together
$sorted = @($unique | Sort-Object -Property class, name)
$top = @($sorted | Select-Object -First $limit)

$data = [pscustomobject]@{
    total = $sorted.Count
    shown = $top.Count
    items = @($top)
}

Write-Output ('DRIVERS_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
