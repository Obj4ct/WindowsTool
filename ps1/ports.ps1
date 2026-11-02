# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only listening port collection.
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.
$items = New-Object System.Collections.ArrayList

$processNames = @{}
foreach ($p in @(Get-Process)) {
    $processNames[[int]$p.Id] = [string]$p.ProcessName
}

foreach ($c in @(Get-NetTCPConnection -State Listen)) {
    $procId = [int]$c.OwningProcess
    $name = ''
    if ($processNames.ContainsKey($procId)) { $name = $processNames[$procId] }
    [void]$items.Add([pscustomobject]@{
        port     = [int]$c.LocalPort
        address  = [string]$c.LocalAddress
        protocol = 'TCP'
        state    = 'LISTEN'
        pid      = $procId
        process  = $name
    })
}

foreach ($e in @(Get-NetUDPEndpoint)) {
    $procId = [int]$e.OwningProcess
    $name = ''
    if ($processNames.ContainsKey($procId)) { $name = $processNames[$procId] }
    [void]$items.Add([pscustomobject]@{
        port     = [int]$e.LocalPort
        address  = [string]$e.LocalAddress
        protocol = 'UDP'
        state    = ''
        pid      = $procId
        process  = $name
    })
}

$sorted = @($items | Sort-Object -Property port, protocol)

Write-Output ('PORTS_JSON:' + (ConvertTo-Json -InputObject $sorted -Compress -Depth 3))
