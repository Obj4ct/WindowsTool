# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

param(
    [Parameter(Mandatory = $true)][ValidateSet('disable', 'enable')][string]$Action,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Command = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Reversible startup item toggle.
# Registry items: the value is renamed to "WindowsTool_disabled::<name>" in the
# same key; enabling renames it back. Startup folder items: the file is moved
# into a "Disabled" subfolder and moved back when enabled.
# Keep this file pure ASCII (Windows PowerShell 5.1 reads .ps1 as ANSI).
$prefix = 'WindowsTool_disabled::'
$ok = $false
$message = ''

function Get-RunKeyPath([string]$code) {
    switch ($code) {
        'user'      { return 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' }
        'machine'   { return 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run' }
        'machine32' { return 'HKLM:\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run' }
    }
    return ''
}

function Get-StartupFolder([string]$code) {
    if ($code -eq 'startup') { return [Environment]::GetFolderPath('Startup') }
    if ($code -eq 'startupCommon') {
        return "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\StartUp"
    }
    return ''
}

try {
    $keyPath = Get-RunKeyPath $Source
    if ($keyPath) {
        if (-not (Test-Path -LiteralPath $keyPath)) {
            $message = 'key-missing'
        } else {
            $key = Get-Item -LiteralPath $keyPath
            $properties = @($key.Property)
            if ($Action -eq 'disable') {
                if ($properties -notcontains $Name) {
                    $message = 'item-missing'
                } else {
                    $kind = $key.GetValueKind($Name)
                    $data = $key.GetValue($Name)
                    Set-ItemProperty -LiteralPath $keyPath -Name ($prefix + $Name) -Value $data -Type $kind
                    Remove-ItemProperty -LiteralPath $keyPath -Name $Name
                    $ok = $true
                }
            } else {
                $disabledName = $prefix + $Name
                if ($properties -notcontains $disabledName) {
                    $message = 'record-missing'
                } else {
                    $kind = $key.GetValueKind($disabledName)
                    $data = $key.GetValue($disabledName)
                    Set-ItemProperty -LiteralPath $keyPath -Name $Name -Value $data -Type $kind
                    Remove-ItemProperty -LiteralPath $keyPath -Name $disabledName
                    $ok = $true
                }
            }
        }
    } else {
        $folder = Get-StartupFolder $Source
        if (-not $folder) {
            $message = 'bad-source'
        } elseif ($Action -eq 'disable') {
            $sourcePath = $Command
            if (-not $sourcePath) { $sourcePath = Join-Path $folder ($Name + '.lnk') }
            if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
                $message = 'item-missing'
            } else {
                $disabledDir = Join-Path $folder 'Disabled'
                if (-not (Test-Path -LiteralPath $disabledDir)) {
                    New-Item -ItemType Directory -Path $disabledDir | Out-Null
                }
                Move-Item -LiteralPath $sourcePath -Destination $disabledDir -Force
                $ok = $true
            }
        } else {
            if ($Command) { $fileName = Split-Path -Leaf $Command }
            else { $fileName = $Name + '.lnk' }
            $disabledPath = Join-Path (Join-Path $folder 'Disabled') $fileName
            if (-not (Test-Path -LiteralPath $disabledPath -PathType Leaf)) {
                $message = 'record-missing'
            } else {
                Move-Item -LiteralPath $disabledPath -Destination $folder -Force
                $ok = $true
            }
        }
    }
} catch {
    $ok = $false
    if ($message -eq '') {
        if ($_.Exception -is [System.UnauthorizedAccessException] -or
            $_.Exception -is [System.Security.SecurityException]) {
            $message = 'access-denied'
        } else {
            $message = 'failed'
        }
    }
}

$payload = [pscustomobject]@{
    ok      = $ok
    action  = $Action
    message = $message
}

Write-Output ('STARTUP_TOGGLE_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 3))
