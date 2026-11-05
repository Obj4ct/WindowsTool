/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

# Network repair: reset DNS on the physical adapter that carries the default
# route, back up the previous configuration first, and report a machine
# readable result.
#
# Output contract: exactly one line starting with FIXDNS_JSON: followed by a
# compressed JSON object. All non-ASCII characters are escaped to \uXXXX so the
# payload stays ASCII no matter how the console encodes text.
#
# When started without administrator rights the script relaunches itself
# elevated (UAC). The child writes the result to %ProgramData%\WindowsTool\
# fixdns-last.json because an elevated process gets a separate console; the
# parent reads that file back and prints the JSON for the caller.

param(
    [string]$ResultFile = '',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    $file = Join-Path $env:ProgramData 'WindowsTool\fixdns-last.json'
    if (Test-Path $file) {
        Remove-Item -Path $file -Force -ErrorAction SilentlyContinue
    }

    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -ResultFile "' + $file + '"'
    try {
        $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    } catch {
        Write-Output 'FIXDNS_JSON:{"ok":false,"target":"","interfaceIndex":0,"dhcp":"","backup":"","dnsBackup":"","actions":[],"error":"not-elevated"}'
        exit 1
    }

    if (Test-Path $file) {
        $payload = (Get-Content -Path $file -Raw -Encoding UTF8).Trim()
        Write-Output ('FIXDNS_JSON:' + $payload)
        if ($proc -and $proc.ExitCode -ne 0) {
            exit $proc.ExitCode
        }
        exit 0
    }

    Write-Output 'FIXDNS_JSON:{"ok":false,"target":"","interfaceIndex":0,"dhcp":"","backup":"","dnsBackup":"","actions":[],"error":"no-result"}'
    exit 1
}

# ---- elevated part ---------------------------------------------------------

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$storeDir = Join-Path $env:ProgramData 'WindowsTool'
if (-not (Test-Path $storeDir)) {
    New-Item -ItemType Directory -Path $storeDir -Force | Out-Null
}

$backupPath = Join-Path $storeDir ('netconfig-' + $stamp + '.txt')
$dnsBackupPath = Join-Path $storeDir ('dns-' + $stamp + '.json')
$actions = @()
$errorCode = ''

try {
    ipconfig.exe /all > $backupPath 2>&1
    $actions += 'backup-ipconfig'
} catch {
    $errorCode = 'backup-failed'
}

try {
    $dnsSnapshot = Get-DnsClientServerAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Select-Object InterfaceAlias, InterfaceIndex, ServerAddresses
    ($dnsSnapshot | ConvertTo-Json -Compress -Depth 4) | Out-File -Path $dnsBackupPath -Encoding ASCII
    $actions += 'backup-dns'
} catch {
    if (-not $errorCode) {
        $errorCode = 'backup-failed'
    }
}

# Pick the physical adapter that carries the default route. Virtual adapters
# (VPN / VMware / Wintun) are skipped, so a running proxy is never touched.
$targetAdapter = $null
try {
    $routes = Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
        Sort-Object RouteMetric
    foreach ($route in $routes) {
        $adapter = Get-NetAdapter -InterfaceIndex $route.ifIndex -ErrorAction SilentlyContinue
        if ($adapter -and $adapter.HardwareInterface -and $adapter.Status -eq 'Up') {
            $targetAdapter = $adapter
            break
        }
    }
    if (-not $targetAdapter) {
        $targetAdapter = Get-NetAdapter -Physical -ErrorAction SilentlyContinue |
            Where-Object { $_.Status -eq 'Up' } |
            Select-Object -First 1
    }
} catch {
    $targetAdapter = $null
}

$dhcpState = 'Unknown'
if ($targetAdapter) {
    try {
        $ipInterface = Get-NetIPInterface -InterfaceIndex $targetAdapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue
        if ($ipInterface) {
            $dhcpState = [string]$ipInterface.Dhcp
        }
        if ($dhcpState -eq 'Enabled') {
            Set-NetIPInterface -InterfaceIndex $targetAdapter.ifIndex -Dhcp Enabled -ErrorAction SilentlyContinue
            $actions += 'ensure-dhcp'
        }
    } catch {
        if (-not $errorCode) {
            $errorCode = 'dhcp-failed'
        }
    }

    try {
        Set-DnsClientServerAddress -InterfaceIndex $targetAdapter.ifIndex -ResetServerAddresses -ErrorAction Stop
        $actions += 'reset-dns'
    } catch {
        $errorCode = 'dns-reset-failed'
    }
} else {
    $errorCode = 'no-physical-adapter'
}

try {
    Clear-DnsClientCache -ErrorAction Stop
    $actions += 'flush-dns'
} catch {
    if (-not $errorCode) {
        $errorCode = 'flush-failed'
    }
}

$targetLabel = ''
$interfaceIndex = 0
if ($targetAdapter) {
    $targetLabel = [string]$targetAdapter.Name
    $interfaceIndex = [int]$targetAdapter.ifIndex
}

$result = [pscustomobject]@{
    ok             = ($errorCode -eq '')
    target         = $targetLabel
    interfaceIndex = $interfaceIndex
    dhcp           = $dhcpState
    backup         = $backupPath
    dnsBackup      = $dnsBackupPath
    actions        = @($actions)
    error          = $errorCode
}

$json = $result | ConvertTo-Json -Compress -Depth 4
$json = [regex]::Replace($json, '[^\x00-\x7F]', { param($m) '\u{0:x4}' -f [int][char]$m.Value })

if ($ResultFile) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($ResultFile, $json, $utf8)
}

if (-not $Quiet) {
    Write-Output ('FIXDNS_JSON:' + $json)
}

if ($errorCode -eq '') {
    exit 0
}
exit 1
