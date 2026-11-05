# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

param(
    [ValidateSet('read', 'apply', 'restore')][string]$Action = 'read',
    [string]$Servers = '',
    [switch]$Auto,
    [string]$Result = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ASCII-only on purpose (PowerShell 5.1 reads BOM-less .ps1 as ANSI).
# User-facing text is assembled in Qt from the emitted "code" value.

function Write-Dns-Result([bool]$Ok, [string]$Code, [string]$Backup = '') {
    $json = [pscustomobject]@{ ok = $Ok; code = $Code; backup = $Backup } | ConvertTo-Json -Compress
    if ($Result) {
        [System.IO.File]::WriteAllText($Result, $json, (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output ('DNS_WRITE_JSON:' + $json)
}

function Get-DefaultAdapterIndex {
    $route = @(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
        Sort-Object -Property RouteMetric | Select-Object -First 1)
    if ($route.Count -eq 0) { return -1 }
    return [int]$route[0].InterfaceIndex
}

if ($Action -eq 'read') {
    $index = Get-DefaultAdapterIndex

    $alias = ''
    $dns = @()
    $autoMode = $true
    $connection = ''

    if ($index -ge 0) {
        $adapter = Get-NetAdapter -InterfaceIndex $index -ErrorAction SilentlyContinue
        if ($adapter) {
            $alias = $adapter.Name
            $connection = $adapter.InterfaceDescription
        }
        $dns = @(Get-DnsClientServerAddress -InterfaceIndex $index -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty ServerAddresses)
        $autoMode = ($dns.Count -eq 0)
    }

    $data = [pscustomobject]@{
        index      = $index
        alias      = $alias
        connection = $connection
        dns        = @($dns)
        auto       = $autoMode
    }

    Write-Output ('DNS_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
    exit 0
}

# Changing DNS requires administrator rights; relaunch ourselves elevated.
$isAdmin = $false
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} catch {
    $isAdmin = $false
}

if (-not $isAdmin) {
    $tempResult = Join-Path $env:TEMP ('wt-dns-' + [guid]::NewGuid().ToString('N') + '.json')
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action $Action -Result `"$tempResult`""
    if ($Auto) { $argList += ' -Auto' }
    if ($Servers) { $argList += " -Servers `"$Servers`"" }

    $started = $true
    try {
        Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait | Out-Null
    } catch {
        $started = $false
    }

    if (-not $started) {
        Write-Dns-Result $false 'canceled'
        exit 0
    }

    if (Test-Path -LiteralPath $tempResult) {
        $raw = [System.IO.File]::ReadAllText($tempResult)
        Remove-Item -LiteralPath $tempResult -Force -ErrorAction SilentlyContinue
        $raw = $raw.Trim().TrimStart([char]0xFEFF)
        Write-Output ('DNS_WRITE_JSON:' + $raw)
    } else {
        Write-Dns-Result $false 'no-result'
    }
    exit 0
}

$backupDir = Join-Path $env:ProgramData 'WindowsTool'
$backupFile = Join-Path $backupDir 'dns-backup.json'

if ($Action -eq 'restore') {
    if (-not (Test-Path -LiteralPath $backupFile)) {
        Write-Dns-Result $false 'no-backup'
        exit 0
    }

    try {
        $raw = [System.IO.File]::ReadAllText($backupFile)
        $data = ConvertFrom-Json $raw
        $index = [int]$data.index
        if ($index -lt 0) { $index = Get-DefaultAdapterIndex }
        if ($index -lt 0) {
            Write-Dns-Result $false 'no-adapter'
            exit 0
        }
        if (-not (Get-NetAdapter -InterfaceIndex $index -ErrorAction SilentlyContinue)) {
            Write-Dns-Result $false 'no-adapter'
            exit 0
        }

        $addresses = @($data.dns | ForEach-Object { [string]$_ } | Where-Object { $_ -ne '' })
        if ($data.auto -or $addresses.Count -eq 0) {
            Set-DnsClientServerAddress -InterfaceIndex $index -ResetServerAddresses -ErrorAction Stop
        } else {
            Set-DnsClientServerAddress -InterfaceIndex $index -ServerAddresses $addresses -ErrorAction Stop
        }
        Clear-DnsClientCache -ErrorAction SilentlyContinue
        Write-Dns-Result $true 'restored'
    } catch {
        Write-Dns-Result $false 'restore-error'
    }
    exit 0
}

try {
    $index = Get-DefaultAdapterIndex
    if ($index -lt 0) {
        Write-Dns-Result $false 'no-adapter'
        exit 0
    }

    # Back up the current DNS before changing it; abort if the backup fails.
    $currentDns = @(Get-DnsClientServerAddress -InterfaceIndex $index -AddressFamily IPv4 -ErrorAction Stop |
        Select-Object -ExpandProperty ServerAddresses)
    $adapter = Get-NetAdapter -InterfaceIndex $index -ErrorAction SilentlyContinue
    $backupAlias = ''
    if ($adapter) { $backupAlias = $adapter.Name }
    $backupData = [pscustomobject]@{
        index = $index
        alias = $backupAlias
        auto  = ($currentDns.Count -eq 0)
        dns   = @($currentDns)
        time  = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    }

    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force -ErrorAction Stop | Out-Null
    }
    $backupJson = ConvertTo-Json -InputObject $backupData -Compress -Depth 4
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($backupFile, $backupJson, $utf8)
    $stampFile = Join-Path $backupDir ('dns-backup-' + (Get-Date).ToString('yyyyMMdd-HHmmss') + '.json')
    [System.IO.File]::WriteAllText($stampFile, $backupJson, $utf8)
} catch {
    Write-Dns-Result $false 'backup-failed'
    exit 0
}

try {
    if ($Auto) {
        Set-DnsClientServerAddress -InterfaceIndex $index -ResetServerAddresses -ErrorAction Stop
    } else {
        $addresses = @($Servers.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
        if ($addresses.Count -eq 0) {
            Write-Dns-Result $false 'no-servers'
            exit 0
        }
        Set-DnsClientServerAddress -InterfaceIndex $index -ServerAddresses $addresses -ErrorAction Stop
    }

    Clear-DnsClientCache -ErrorAction SilentlyContinue
    Write-Dns-Result $true 'applied' $backupFile
} catch {
    Write-Dns-Result $false 'error'
}
