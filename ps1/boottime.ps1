$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only boot timeline collector.
#
# The ideal source (Microsoft-Windows-Diagnostics-Performance/Operational,
# event 100) requires administrator rights, so this script uses data that a
# normal user can read:
#   - System log event 12 = kernel start time
#   - explorer.exe start time = desktop ready (approximation)
#
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.

$payload = [pscustomobject]@{
    bootTime        = ''
    bootDate        = ''
    kernelTime      = ''
    desktopSeconds  = -1
    desktopReliable = $false
    uptimeMinutes   = 0
    startupCount    = 0
}

# ---- kernel start (event 12) ----
$kernel = $null
try {
    $kernel = Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 12 } -MaxEvents 1 -ErrorAction Stop
} catch { }

$os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
$boot = $null
if ($os -ne $null) { $boot = $os.LastBootUpTime }
if ($boot -eq $null -and $kernel -ne $null) { $boot = $kernel.TimeCreated }

if ($boot -ne $null) {
    $payload.bootTime = $boot.ToString('yyyy-MM-dd HH:mm:ss')
    $payload.bootDate = $boot.ToString('yyyy-MM-dd')
}
if ($kernel -ne $null) {
    $payload.kernelTime = $kernel.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')
}

# ---- desktop ready (explorer start) ----
# explorer may have been restarted long after boot; anything beyond an hour
# is treated as unreliable instead of reporting a bogus "slow boot".
$explorer = $null
try {
    $explorer = Get-Process -Name explorer -ErrorAction Stop |
        Sort-Object StartTime | Select-Object -First 1
} catch { }

if ($boot -ne $null -and $explorer -ne $null) {
    $seconds = [int]($explorer.StartTime - $boot).TotalSeconds
    if ($seconds -ge 0 -and $seconds -le 3600) {
        $payload.desktopSeconds = $seconds
        $payload.desktopReliable = $true
    }
}

if ($boot -ne $null) {
    $payload.uptimeMinutes = [int]((Get-Date) - $boot).TotalMinutes
}

# ---- startup item count (Run keys + Startup folders) ----
$runKeys = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run'
)
$count = 0
foreach ($k in $runKeys) {
    if (-not (Test-Path -LiteralPath $k)) { continue }
    $item = Get-ItemProperty -LiteralPath $k -ErrorAction SilentlyContinue
    if ($item -eq $null) { continue }
    $count += @($item.PSObject.Properties | Where-Object {
        $_.Name -notlike 'PS*'
    }).Count
}

$startupFolders = @(
    [Environment]::GetFolderPath('Startup'),
    [Environment]::GetFolderPath('CommonStartup')
)
foreach ($f in $startupFolders) {
    if ([string]::IsNullOrWhiteSpace($f)) { continue }
    if (-not (Test-Path -LiteralPath $f)) { continue }
    $count += @(Get-ChildItem -LiteralPath $f -Filter *.lnk -ErrorAction SilentlyContinue).Count
}

$payload.startupCount = $count

Write-Output ('BOOTTIME_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 3))
