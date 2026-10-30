$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

$os = Get-CimInstance Win32_OperatingSystem
$cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'

$uptime = 0
if ($os.LastBootUpTime) {
    $uptime = [int]((Get-Date) - $os.LastBootUpTime).TotalSeconds
}

$memTotalMb = 0
$memFreeMb = 0
if ($os) {
    $memTotalMb = [int][math]::Round($os.TotalVisibleMemorySize / 1024)
    $memFreeMb = [int][math]::Round($os.FreePhysicalMemory / 1024)
}

$sysDrive = $env:SystemDrive
$disk = Get-CimInstance Win32_LogicalDisk -Filter ("DeviceID='" + $sysDrive + "'")
$diskTotalMb = 0
$diskFreeMb = 0
if ($disk) {
    $diskTotalMb = [int][math]::Round($disk.Size / 1MB)
    $diskFreeMb = [int][math]::Round($disk.FreeSpace / 1MB)
}

$startupCount = 0
$runKeys = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run',
    'HKLM:\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run'
)
foreach ($key in $runKeys) {
    if (Test-Path $key) {
        $item = Get-Item $key
        if ($item -and $item.Property) {
            $startupCount += $item.Property.Count
        }
    }
}

$startupFolders = @(
    [Environment]::GetFolderPath('Startup'),
    "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\StartUp"
)
foreach ($folder in $startupFolders) {
    if ($folder -and (Test-Path $folder)) {
        $startupCount += @(Get-ChildItem -LiteralPath $folder -File).Count
    }
}

$pendingReboot = $false
if (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending') {
    $pendingReboot = $true
}
if (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired') {
    $pendingReboot = $true
}
$pendingRenames = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations).PendingFileRenameOperations
if ($pendingRenames) {
    $pendingReboot = $true
}

$proxyEnabled = $false
$proxy = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -Name ProxyEnable
if ($proxy -and $proxy.ProxyEnable -eq 1) {
    $proxyEnabled = $true
}

$result = [ordered]@{
    os_build       = [int]$cv.CurrentBuildNumber
    os_version     = [string]$cv.DisplayVersion
    uptime_seconds = $uptime
    mem_total_mb   = $memTotalMb
    mem_free_mb    = $memFreeMb
    disk_letter    = $sysDrive
    disk_total_mb  = $diskTotalMb
    disk_free_mb   = $diskFreeMb
    startup_count  = $startupCount
    pending_reboot = [bool]$pendingReboot
    proxy_enabled  = [bool]$proxyEnabled
}

Write-Output ('HEALTHCHECK_JSON:' + ($result | ConvertTo-Json -Compress))
