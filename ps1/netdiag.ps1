# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only snapshot of the local network stack.
# The body stays pure ASCII; every non-ASCII character is escaped to \uXXXX
# before output so the caller can decode it as plain ASCII JSON.

function Test-VirtualAdapter([string]$description) {
    if (-not $description) { return $false }
    return ($description -match '(?i)(VMware|VirtualBox|Hyper-V|Virtual Ethernet|TAP[- ]|TUN|VPN|WireGuard|Tailscale|ZeroTier|v2ray|xray|Wintun|Loopback|Npcap)')
}

$adapters = New-Object System.Collections.ArrayList

function Add-Adapter($nic, $ipv4, $gateway, $dnsList) {
    [void]$adapters.Add([pscustomobject]@{
        name        = [string]$nic.Name
        description = [string]$nic.InterfaceDescription
        status      = [string]$nic.Status
        ifIndex     = [int]$nic.ifIndex
        ipv4        = [string]$ipv4
        gateway     = [string]$gateway
        dns         = @($dnsList)
        virtual     = (Test-VirtualAdapter([string]$nic.InterfaceDescription))
        defaultRoute = $false
    })
}

$ipConfigs = @(Get-NetIPConfiguration -ErrorAction SilentlyContinue)
if ($ipConfigs.Count -gt 0) {
    foreach ($cfg in $ipConfigs) {
        $nic = $cfg.NetAdapter
        if (-not $nic) { continue }

        $ipv4 = ''
        $addresses = @($cfg.IPv4Address)
        if ($addresses.Count -gt 0) { $ipv4 = [string]$addresses[0].IPAddress }

        $gateway = ''
        if ($cfg.IPv4DefaultGateway) { $gateway = [string]$cfg.IPv4DefaultGateway.NextHop }

        $dnsList = New-Object System.Collections.ArrayList
        foreach ($srv in @($cfg.DNSServer)) {
            foreach ($addr in @($srv.ServerAddresses)) {
                # Only IPv4 servers: the probe speaks A records over IPv4.
                if ($addr -and ($addr -match '^\d{1,3}(\.\d{1,3}){3}$')) {
                    [void]$dnsList.Add([string]$addr)
                }
            }
        }

        Add-Adapter $nic $ipv4 $gateway $dnsList
    }
} else {
    # Fallback for old PowerShell builds without Get-NetIPConfiguration.
    foreach ($nic in @(Get-NetAdapter -ErrorAction SilentlyContinue)) {
        $addr = @(Get-NetIPAddress -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Select-Object -First 1)
        $ipv4 = ''
        if ($addr.Count -gt 0) { $ipv4 = [string]$addr[0].IPAddress }

        $route = @(Get-NetRoute -InterfaceIndex $nic.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Select-Object -First 1)
        $gateway = ''
        if ($route.Count -gt 0) { $gateway = [string]$route[0].NextHop }

        $dnsList = New-Object System.Collections.ArrayList
        foreach ($dns in @(Get-DnsClientServerAddress -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue)) {
            foreach ($addr2 in @($dns.ServerAddresses)) {
                if ($addr2 -and ($addr2 -match '^\d{1,3}(\.\d{1,3}){3}$')) {
                    [void]$dnsList.Add([string]$addr2)
                }
            }
        }

        Add-Adapter $nic $ipv4 $gateway $dnsList
    }
}

# Adapter that carries the default route (lowest metric, then lowest ifIndex).
$defaultIf = 0
$defaultRoutes = @(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
    Sort-Object -Property RouteMetric, ifIndex)
if ($defaultRoutes.Count -gt 0) { $defaultIf = [int]$defaultRoutes[0].ifIndex }

$defaultName = ''
foreach ($adapter in $adapters) {
    if ($adapter.ifIndex -eq $defaultIf) {
        $adapter.defaultRoute = $true
        $defaultName = $adapter.name
    }
}

# System proxy (per-user WinINET settings).
$proxy = [pscustomobject]@{
    enabled = $false
    server  = ''
    pac     = ''
}
$internetSettings = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -ErrorAction SilentlyContinue
if ($internetSettings) {
    $proxy.enabled = ([int]$internetSettings.ProxyEnable -eq 1)
    $proxy.server = [string]$internetSettings.ProxyServer
    $proxy.pac = [string]$internetSettings.AutoConfigURL
}

$payload = [pscustomobject]@{
    adapters       = @($adapters)
    defaultAdapter = $defaultName
    proxy          = $proxy
}

$json = ConvertTo-Json -InputObject $payload -Depth 5 -Compress
$json = [regex]::Replace($json, '[^\x00-\x7F]', { param($m) '\u{0:x4}' -f [int][char]$m.Value })

Write-Output ('NETDIAG_JSON:' + $json)
