# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.
#
# ASCII-only on purpose (PowerShell 5.1 reads BOM-less .ps1 as ANSI).
# User-facing text is assembled in Qt from the emitted "code" value.

param(
    [ValidateSet('read', 'apply', 'off', 'restore')][string]$Action = 'read',
    [string]$Server = '',
    [string]$Result = ''
)

$ErrorActionPreference = 'Stop'

$regPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
$backupDir = Join-Path $env:LOCALAPPDATA 'WindowsTool'
$backupFile = Join-Path $backupDir 'proxy-backup.json'

$state = [pscustomobject]@{
    enabled      = $false
    server       = ''
    pac          = ''
    hasBackup    = $false
    backupServer = ''
    backupTime   = ''
}

function Read-Current {
    $reg = Get-ItemProperty -LiteralPath $regPath -ErrorAction SilentlyContinue
    if ($reg) {
        $state.enabled = ([int]$reg.ProxyEnable -eq 1)
        $state.server = [string]$reg.ProxyServer
        $state.pac = [string]$reg.AutoConfigURL
    }
}

function Read-Backup {
    $state.hasBackup = Test-Path -LiteralPath $backupFile
    if (-not $state.hasBackup) { return }
    try {
        $data = ConvertFrom-Json ([System.IO.File]::ReadAllText($backupFile))
        $state.backupServer = [string]$data.server
        $state.backupTime = [string]$data.time
    } catch {
        $state.hasBackup = $false
    }
}

function Write-Result([bool]$Ok, [string]$Code) {
    $json = [pscustomobject]@{
        ok           = $Ok
        code         = $Code
        enabled      = $state.enabled
        server       = $state.server
        pac          = $state.pac
        hasBackup    = $state.hasBackup
        backupServer = $state.backupServer
        backupTime   = $state.backupTime
    } | ConvertTo-Json -Compress
    if ($Result) {
        [System.IO.File]::WriteAllText($Result, $json,
            (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output ('PROXY_JSON:' + $json)
}

function Save-Backup {
    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $data = [pscustomobject]@{
        enabled = $state.enabled
        server  = $state.server
        pac     = $state.pac
        time    = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    }
    $json = $data | ConvertTo-Json -Compress
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($backupFile, $json, $utf8)
    $history = Join-Path $backupDir ('proxy-backup-' + $stamp + '.json')
    [System.IO.File]::WriteAllText($history, $json, $utf8)
}

function Clear-SystemProxy {
    Remove-ItemProperty -LiteralPath $regPath -Name 'ProxyServer' -ErrorAction SilentlyContinue
    Remove-ItemProperty -LiteralPath $regPath -Name 'AutoConfigURL' -ErrorAction SilentlyContinue
    Set-ItemProperty -LiteralPath $regPath -Name 'ProxyEnable' -Value 0 -Type DWord
}

function Set-SystemProxy([string]$ProxyServer, [string]$PacUrl) {
    if ($ProxyServer) {
        Set-ItemProperty -LiteralPath $regPath -Name 'ProxyServer' -Value $ProxyServer -Type String
    } else {
        Remove-ItemProperty -LiteralPath $regPath -Name 'ProxyServer' -ErrorAction SilentlyContinue
    }
    if ($PacUrl) {
        Set-ItemProperty -LiteralPath $regPath -Name 'AutoConfigURL' -Value $PacUrl -Type String
    } else {
        Remove-ItemProperty -LiteralPath $regPath -Name 'AutoConfigURL' -ErrorAction SilentlyContinue
    }
    Set-ItemProperty -LiteralPath $regPath -Name 'ProxyEnable' -Value 1 -Type DWord
    Set-ItemProperty -LiteralPath $regPath -Name 'ProxyOverride' -Value '<local>' -Type String
}

function Refresh-WinInet {
    try {
        if (-not ('WinINetRefresh' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class WinINetRefresh {
    [DllImport("wininet.dll", SetLastError = true)]
    public static extern bool InternetSetOption(IntPtr h, int option, IntPtr buffer, int length);
}
'@
        }
        [WinINetRefresh]::InternetSetOption([IntPtr]::Zero, 39, [IntPtr]::Zero, 0) | Out-Null
        [WinINetRefresh]::InternetSetOption([IntPtr]::Zero, 37, [IntPtr]::Zero, 0) | Out-Null
    } catch {
        # Settings are still written; Explorer will pick them up eventually.
    }
}

Read-Current
Read-Backup

switch ($Action) {
    'read' {
        Write-Result $true 'read'
        exit 0
    }

    'off' {
        try {
            Save-Backup
        } catch {
            Write-Result $false 'backup-failed'
            exit 0
        }
        try {
            Clear-SystemProxy
            Refresh-WinInet
        } catch {
            Write-Result $false 'write-failed'
            exit 0
        }
        Read-Current
        Read-Backup
        Write-Result $true 'off'
        exit 0
    }

    'apply' {
        $value = $Server.Trim()
        if ($value -notmatch '^[A-Za-z0-9\.\-]+:[0-9]{1,5}$') {
            Write-Result $false 'invalid-server'
            exit 0
        }
        $port = [int]($value.Split(':')[-1])
        if ($port -lt 1 -or $port -gt 65535) {
            Write-Result $false 'invalid-server'
            exit 0
        }
        try {
            Save-Backup
        } catch {
            Write-Result $false 'backup-failed'
            exit 0
        }
        try {
            Set-SystemProxy $value ''
            Refresh-WinInet
        } catch {
            Write-Result $false 'write-failed'
            exit 0
        }
        Read-Current
        Read-Backup
        Write-Result $true 'applied'
        exit 0
    }

    'restore' {
        if (-not $state.hasBackup) {
            Write-Result $false 'no-backup'
            exit 0
        }
        try {
            $data = ConvertFrom-Json ([System.IO.File]::ReadAllText($backupFile))
            if ([bool]$data.enabled) {
                Set-SystemProxy ([string]$data.server) ([string]$data.pac)
            } else {
                Clear-SystemProxy
            }
            Refresh-WinInet
        } catch {
            Write-Result $false 'restore-error'
            exit 0
        }
        Read-Current
        Write-Result $true 'restored'
        exit 0
    }
}
