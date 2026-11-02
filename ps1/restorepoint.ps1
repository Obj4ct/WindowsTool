param(
    [ValidateSet('list', 'create')][string]$Action = 'list',
    [string]$Description = 'WindowsTool',
    [string]$Result = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ASCII-only on purpose (PowerShell 5.1 reads BOM-less .ps1 as ANSI).
# List: returns restore points and whether System Restore is enabled.
# Create: makes a checkpoint; relaunches itself elevated when needed.
# Elevated results are written to the -Result file (UAC child output is lost).

function Write-Restore-Result([bool]$Ok, [string]$Code) {
    $json = [pscustomobject]@{ ok = $Ok; code = $Code } | ConvertTo-Json -Compress
    if ($Result) {
        [System.IO.File]::WriteAllText($Result, $json, (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output ('RESTOREPOINT_JSON:' + $json)
}

if ($Action -eq 'list') {
    $enabled = $true
    $points = @()

    try {
        $points = @(Get-ComputerRestorePoint -ErrorAction Stop)
    } catch {
        $enabled = $false
        $points = @()
    }

    $list = @()
    foreach ($point in @($points | Sort-Object -Property SequenceNumber -Descending | Select-Object -First 10)) {
        $timeText = ''
        try {
            $dt = [System.Management.ManagementDateTimeConverter]::ToDateTime([string]$point.CreationTime)
            $timeText = $dt.ToString('yyyy-MM-dd HH:mm')
        } catch { }

        $list += [pscustomobject]@{
            seq  = [int]$point.SequenceNumber
            time = $timeText
            desc = [string]$point.Description
        }
    }

    $data = [pscustomobject]@{
        ok      = $true
        enabled = $enabled
        points  = @($list)
    }

    Write-Output ('RESTOREPOINT_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
    exit 0
}

# Creating a restore point requires administrator rights; relaunch elevated.
$isAdmin = $false
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} catch {
    $isAdmin = $false
}

if (-not $isAdmin) {
    $tempResult = Join-Path $env:TEMP ('wt-restore-' + [guid]::NewGuid().ToString('N') + '.json')
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action create -Description `"$Description`" -Result `"$tempResult`""

    $started = $true
    try {
        Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait | Out-Null
    } catch {
        $started = $false
    }

    if (-not $started) {
        Write-Restore-Result $false 'canceled'
        exit 0
    }

    if (Test-Path -LiteralPath $tempResult) {
        $raw = [System.IO.File]::ReadAllText($tempResult)
        Remove-Item -LiteralPath $tempResult -Force -ErrorAction SilentlyContinue
        $raw = $raw.Trim().TrimStart([char]0xFEFF)
        Write-Output ('RESTOREPOINT_JSON:' + $raw)
    } else {
        Write-Restore-Result $false 'no-result'
    }
    exit 0
}

# System Restore must be enabled on the system drive, otherwise the
# checkpoint cannot be created and Windows reports a generic failure.
try {
    Enable-ComputerRestore -Drive "$env:SystemDrive\" -ErrorAction Stop
} catch {
    Write-Restore-Result $false 'enable-failed'
    exit 0
}

# Windows allows one checkpoint per 24 hours by default; temporarily lift
# that limit so the button works whenever the user clicks it, then restore it.
$srKey = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SystemRestore'
$freqName = 'SystemRestorePointCreationFrequency'
$hadFrequency = $false
$oldFrequency = $null

try {
    $current = Get-ItemProperty -LiteralPath $srKey -Name $freqName -ErrorAction SilentlyContinue
    if ($null -ne $current) {
        $hadFrequency = $true
        $oldFrequency = $current.$freqName
    }
    Set-ItemProperty -LiteralPath $srKey -Name $freqName -Value 0 -Type DWord -ErrorAction Stop
} catch { }

try {
    Checkpoint-Computer -Description $Description -RestorePointType 'MODIFY_SETTINGS' -ErrorAction Stop
    Write-Restore-Result $true 'created'
} catch {
    Write-Restore-Result $false 'failed'
} finally {
    try {
        if ($hadFrequency) {
            Set-ItemProperty -LiteralPath $srKey -Name $freqName -Value $oldFrequency -Type DWord -ErrorAction SilentlyContinue
        } else {
            Remove-ItemProperty -LiteralPath $srKey -Name $freqName -ErrorAction SilentlyContinue
        }
    } catch { }
}
