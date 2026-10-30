$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only battery health snapshot. Capacities come from the WMI battery class
# and, when available, from the powercfg battery report XML (which also carries
# the cycle count). Output is one JSON line prefixed with BATTERY_JSON:.
#
# NOTE: this script is deliberately ASCII-only. PowerShell 5.1 reads BOM-less
# .ps1 files as ANSI, so Chinese text would be mangled. User-facing messages are
# built in Qt from the numeric fields emitted here.

$battery = @(Get-CimInstance -ClassName Win32_Battery -ErrorAction SilentlyContinue)

if ($battery.Count -eq 0) {
    $data = [pscustomobject]@{ present = $false }
    Write-Output ('BATTERY_JSON:' + (ConvertTo-Json -InputObject $data -Compress))
    exit 0
}

$first = $battery[0]
$design = [int64]$first.DesignCapacity
$full = [int64]$first.FullChargeCapacity
$cycles = [int64]0
$reportId = ''

# powercfg writes a detailed report; parsing the XML fills in the capacities
# that some batteries leave empty in WMI, plus the cycle count.
$reportPath = Join-Path $env:TEMP ('wt-battery-' + [guid]::NewGuid().ToString('N') + '.xml')
powercfg.exe /batteryreport /xml /output $reportPath | Out-Null

if (Test-Path -LiteralPath $reportPath) {
    try {
        $doc = New-Object System.Xml.XmlDocument
        $doc.Load($reportPath)
        $node = @($doc.BatteryReport.Batteries.Battery)[0]
        if ($node) {
            if ($design -le 0 -and $node.DesignCapacity) { $design = [int64]$node.DesignCapacity.InnerText }
            if ($full -le 0 -and $node.FullChargeCapacity) { $full = [int64]$node.FullChargeCapacity.InnerText }
            if ($node.CycleCount) { $cycles = [int64]$node.CycleCount.InnerText }
            $reportId = ([string]$node.Id).Trim()
        }
    } catch {
        $cycles = 0
    }
    Remove-Item -LiteralPath $reportPath -Force -ErrorAction SilentlyContinue
}

$name = ([string]$first.Name).Trim()
if ($name -eq '') { $name = $reportId }

$data = [pscustomobject]@{
    present   = $true
    name      = $name
    chemistry = ([string]$first.Chemistry).Trim()
    deviceId  = ([string]$first.DeviceID).Trim()
    design    = $design
    full      = $full
    cycles    = $cycles
    charge    = [int]$first.EstimatedChargeRemaining
    status    = [int]$first.BatteryStatus
    runtime   = [int]$first.EstimatedRunTime
}

Write-Output ('BATTERY_JSON:' + (ConvertTo-Json -InputObject $data -Compress))
exit 0
