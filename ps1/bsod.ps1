# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only blue screen history collector.
#
# Sources:
#   - System log event 1001 (WER-SystemErrorReporting) = bugcheck code
#   - System log events 41 / 6008 = unexpected shutdown, no bugcheck code
#   - C:\Windows\Minidump\*.dmp and C:\Windows\MEMORY.DMP listings
#
# The event log is readable without administrator rights. The dump folder may
# not be, so a denied read is reported as minidumpAccessible = false instead
# of failing the whole scan.
#
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.

function Format-BugCheckCode([string]$raw) {
    if ($raw -match '0x[0-9a-fA-F]+') {
        $hex = $Matches[0].Substring(2).ToUpper()
        if ($hex.Length -lt 8) { $hex = $hex.PadLeft(8, '0') }
        if ($hex.Length -gt 8) { $hex = $hex.Substring($hex.Length - 8, 8) }
        return ('0x' + $hex)
    }
    if ($raw -match '[0-9a-fA-F]{8}') {
        return ('0x' + $Matches[0].ToUpper())
    }
    return ''
}

$items = New-Object System.Collections.ArrayList
$bugTimes = New-Object System.Collections.ArrayList

# ---- bugcheck events (1001) ----
$bug = @()
try {
    $bug = @(Get-WinEvent -FilterHashtable @{
        LogName = 'System'
        ProviderName = 'Microsoft-Windows-WER-SystemErrorReporting'
        Id = 1001
    } -MaxEvents 10 -ErrorAction Stop)
} catch { }

foreach ($e in $bug) {
    $raw = ''
    if ($e.Properties -ne $null -and $e.Properties.Count -gt 0) {
        $v = $e.Properties[0].Value
        if ($v -ne $null) { $raw = [string]$v }
    }

    $code = Format-BugCheckCode $raw
    if ($code -eq '') { $code = Format-BugCheckCode ([string]$e.Message) }

    $params = New-Object System.Collections.ArrayList
    if ($e.Properties -ne $null -and $e.Properties.Count -gt 1) {
        $max = [Math]::Min(4, $e.Properties.Count - 1)
        for ($i = 1; $i -le $max; $i++) {
            $pv = $e.Properties[$i].Value
            if ($pv -ne $null) { [void]$params.Add([string]$pv) }
        }
    }

    $dump = ''
    if ($e.Properties -ne $null) {
        foreach ($p in $e.Properties) {
            $s = [string]$p.Value
            if ($s -match '\.dmp') { $dump = $s; break }
        }
    }

    [void]$bugTimes.Add($e.TimeCreated)
    [void]$items.Add([pscustomobject]@{
        kind   = 'bugcheck'
        time   = $e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')
        code   = $code
        params = @($params)
        dump   = $dump
    })
}

# ---- unexpected shutdown events (41 / 6008) ----
# A bugcheck reboot also logs event 41 (and 6008); the same shutdown can log
# several power events within seconds, so skip any power event that falls
# within three minutes of a time already collected.
$power = @()
try {
    $power = @(Get-WinEvent -FilterHashtable @{
        LogName = 'System'
        Id = 41, 6008
    } -MaxEvents 10 -ErrorAction Stop)
} catch { }

foreach ($e in $power) {
    $duplicate = $false
    foreach ($t in $bugTimes) {
        $diff = [Math]::Abs(($e.TimeCreated - $t).TotalSeconds)
        if ($diff -le 180) { $duplicate = $true; break }
    }
    if ($duplicate) { continue }

    [void]$bugTimes.Add($e.TimeCreated)
    [void]$items.Add([pscustomobject]@{
        kind   = 'power'
        time   = $e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')
        code   = ''
        params = @()
        dump   = ''
    })
}

$events = @($items | Sort-Object -Property time -Descending)

# ---- dump files ----
$minidumps = New-Object System.Collections.ArrayList
$minidumpAccessible = $false
try {
    $files = @(Get-ChildItem -LiteralPath 'C:\Windows\Minidump' -Filter *.dmp -File -ErrorAction Stop)
    $minidumpAccessible = $true
    $files = @($files | Sort-Object -Property LastWriteTime -Descending | Select-Object -First 10)
    foreach ($f in $files) {
        [void]$minidumps.Add([pscustomobject]@{
            name = $f.Name
            time = $f.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
            size = [long]$f.Length
        })
    }
} catch {
    $minidumpAccessible = $false
}

$memoryDump = [pscustomobject]@{
    exists = $false
    time   = ''
    size   = 0
}
$memFile = Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue
if ($memFile -ne $null) {
    $memoryDump = [pscustomobject]@{
        exists = $true
        time   = $memFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
        size   = [long]$memFile.Length
    }
}

$payload = [pscustomobject]@{
    events            = @($events)
    minidumps         = @($minidumps)
    minidumpAccessible = $minidumpAccessible
    memoryDump        = $memoryDump
}

Write-Output ('BSOD_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 4))
