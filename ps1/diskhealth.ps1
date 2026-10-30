$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only disk health report: model, media type, capacity, health and
# (when available) reliability counters such as temperature and power-on hours.
$items = New-Object System.Collections.ArrayList

$disks = @(Get-PhysicalDisk)
foreach ($d in $disks) {
    $counter = $null
    try { $counter = $d | Get-StorageReliabilityCounter -ErrorAction Stop } catch { $counter = $null }

    $temp  = $null
    $wear  = $null
    $hours = $null
    if ($counter) {
        if ($null -ne $counter.Temperature)  { $temp  = [int]$counter.Temperature }
        if ($null -ne $counter.Wear)         { $wear  = [int]$counter.Wear }
        if ($null -ne $counter.PowerOnHours) { $hours = [long]$counter.PowerOnHours }
    }

    $op = ''
    if ($d.OperationalStatus) { $op = [string](@($d.OperationalStatus) -join ', ') }

    [void]$items.Add([pscustomobject]@{
        model       = [string]$d.FriendlyName
        media       = [string]$d.MediaType
        bus         = [string]$d.BusType
        size        = [long]$d.Size
        health      = [string]$d.HealthStatus
        operational = $op
        temperature = $temp
        wear        = $wear
        hours       = $hours
    })
}

Write-Output ('DISKS_JSON:' + (ConvertTo-Json -InputObject @($items) -Compress -Depth 3))
