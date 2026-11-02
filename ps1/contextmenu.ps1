param(
    [ValidateSet('list', 'disable', 'enable')][string]$Action = 'list',
    [string]$Root = 'HKCU',
    [string]$Scope = '*',
    [string]$Entry = ''
)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Enumerate / toggle Explorer right-click menu verbs stored in the registry.
#
# Only classic "shell" verbs are handled (not shellex CLSID handlers):
#   <root>\Software\Classes\<scope>\shell\<entry>
# Disabling writes LegacyDisable (plus ProgrammaticAccessOnly), which makes
# Explorer skip the verb without deleting anything.
#
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.

$scopeList = @(
    '*',
    'Directory',
    'Directory\Background',
    'Drive',
    'Folder',
    'AllFilesystemObjects',
    'DesktopBackground'
)

$rootList = @(
    @{ id = 'HKCU'; path = 'HKCU:\Software\Classes'; admin = $false },
    @{ id = 'HKLM'; path = 'HKLM:\Software\Classes'; admin = $true }
)

function Read-RegString {
    param([string]$KeyPath, [string]$Name)
    $value = ''
    try {
        $item = Get-ItemProperty -LiteralPath $KeyPath -ErrorAction Stop
        $value = [string]$item.$Name
    } catch { }
    return $value
}

function Test-RegValue {
    param([string]$KeyPath, [string]$Name)
    $item = Get-ItemProperty -LiteralPath $KeyPath -Name $Name -ErrorAction SilentlyContinue
    return ($item -ne $null)
}

if ($Action -eq 'list') {
    $items = New-Object System.Collections.ArrayList

    foreach ($r in $rootList) {
        foreach ($s in $scopeList) {
            $shellKey = "$($r.path)\$s\shell"
            if (-not (Test-Path -LiteralPath $shellKey)) { continue }

            foreach ($k in @(Get-ChildItem -LiteralPath $shellKey -ErrorAction SilentlyContinue)) {
                if ($k -eq $null) { continue }
                $entryName = [string]$k.PSChildName
                $keyPath = [string]$k.PSPath

                $display = Read-RegString -KeyPath $keyPath -Name 'MUIVerb'
                if ([string]::IsNullOrWhiteSpace($display)) {
                    $display = Read-RegString -KeyPath $keyPath -Name '(default)'
                }
                # "@shell32.dll,-8506" style resource references cannot be
                # resolved cheaply; fall back to the registry key name.
                if ($display.StartsWith('@')) { $display = '' }
                if ([string]::IsNullOrWhiteSpace($display)) { $display = $entryName }

                $command = Read-RegString -KeyPath "$keyPath\command" -Name '(default)'

                $disabled = ((Test-RegValue -KeyPath $keyPath -Name 'LegacyDisable') -or
                             (Test-RegValue -KeyPath $keyPath -Name 'ProgrammaticAccessOnly'))

                [void]$items.Add([pscustomobject]@{
                    root     = $r.id
                    scope    = $s
                    entry    = $entryName
                    display  = $display
                    command  = $command
                    disabled = [bool]$disabled
                    admin    = [bool]$r.admin
                })
            }
        }
    }

    $sorted = @($items | Sort-Object -Property root, scope, display)
    Write-Output ('CONTEXTMENU_JSON:' + (ConvertTo-Json -InputObject $sorted -Compress -Depth 3))
    return
}

# ---- disable / enable ----
$base = ''
foreach ($r in $rootList) {
    if ($r.id -eq $Root) { $base = "$($r.path)\$Scope\shell" }
}

if ([string]::IsNullOrWhiteSpace($base) -or [string]::IsNullOrWhiteSpace($Entry)) {
    $payload = [pscustomobject]@{ ok = $false; message = 'invalid parameters' }
    Write-Output ('CONTEXTMENU_ACTION_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 3))
    return
}

$target = "$base\$Entry"
$payload = $null
try {
    if (-not (Test-Path -LiteralPath $target)) {
        throw 'registry key not found'
    }
    if ($Action -eq 'disable') {
        New-ItemProperty -LiteralPath $target -Name 'LegacyDisable' -PropertyType String -Value '' -Force -ErrorAction Stop | Out-Null
        $payload = [pscustomobject]@{ ok = $true; message = 'disabled' }
    } else {
        Remove-ItemProperty -LiteralPath $target -Name 'LegacyDisable' -ErrorAction Stop
        Remove-ItemProperty -LiteralPath $target -Name 'ProgrammaticAccessOnly' -ErrorAction SilentlyContinue
        $payload = [pscustomobject]@{ ok = $true; message = 'enabled' }
    }
} catch {
    $payload = [pscustomobject]@{ ok = $false; message = [string]$_.Exception.Message }
}

Write-Output ('CONTEXTMENU_ACTION_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 3))
