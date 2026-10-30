param(
    [ValidateSet('read', 'uninstall')][string]$Action = 'read',
    [string]$Key = '',
    [string]$Result = ''
)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# NOTE: this script is deliberately ASCII-only. PowerShell 5.1 reads BOM-less
# .ps1 files as ANSI, so Chinese text would be mangled. User-facing messages are
# built in Qt from the short "code" values emitted here.
#
# Action read: inventory of installed programs from the three uninstall registry
# roots (64-bit, 32-bit and per-user). Output is one JSON line prefixed with
# UNINSTALL_JSON:.
#
# Action uninstall: run the UninstallString of the registry entry identified by
# -Key. Requires administrator rights, so the script relaunches itself elevated
# and reports the outcome through -Result (a temporary JSON file) as well.

$roots = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)

function Get-Items {
    $list = New-Object System.Collections.ArrayList

    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }

        foreach ($key in @(Get-ChildItem -LiteralPath $root -ErrorAction SilentlyContinue)) {
            $props = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
            if (-not $props) { continue }

            $name = ([string]$props.DisplayName).Trim()
            if ($name -eq '') { continue }
            if ([int]$props.SystemComponent -eq 1) { continue }
            if ($props.ParentKeyName) { continue }
            if ($props.ReleaseType -eq 'Security Update' -or $props.ReleaseType -eq 'Update') { continue }

            $size = [int64]0
            if ($props.EstimatedSize) { $size = [int64]$props.EstimatedSize * 1024 }
            # Some installers store a bogus EstimatedSize (e.g. bytes treated as KB);
            # anything above 50 GB is treated as unknown instead of showing nonsense.
            if ($size -gt 53687091200) { $size = 0 }

            # InstallDate is stored as yyyyMMdd
            $date = ''
            $rawDate = [string]$props.InstallDate
            if ($rawDate.Length -eq 8) {
                $date = $rawDate.Substring(0, 4) + '-' + $rawDate.Substring(4, 2) + '-' + $rawDate.Substring(6, 2)
            }

            $command = ([string]$props.UninstallString).Trim()
            if ($command -eq '') { $command = ([string]$props.QuietUninstallString).Trim() }

            [void]$list.Add([pscustomobject]@{
                key       = $key.PSPath
                name      = $name
                version   = ([string]$props.DisplayVersion).Trim()
                publisher = ([string]$props.Publisher).Trim()
                date      = $date
                size      = $size
                uninstall = ($command -ne '')
            })
        }
    }

    return @($list | Sort-Object -Property size -Descending)
}

function Write-Outcome([bool]$Ok, [string]$Code, [string]$Name, [string]$Detail) {
    $json = [pscustomobject]@{
        ok     = $Ok
        code   = $Code
        name   = $Name
        detail = $Detail
    } | ConvertTo-Json -Compress

    if ($Result) {
        [System.IO.File]::WriteAllText($Result, $json, (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output ('UNINSTALL_ACTION_JSON:' + $json)
}

if ($Action -eq 'read') {
    $limit = 40
    $sorted = Get-Items
    $top = @($sorted | Select-Object -First $limit)

    $data = [pscustomobject]@{
        total = $sorted.Count
        shown = $top.Count
        items = @($top)
    }

    Write-Output ('UNINSTALL_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
    exit 0
}

# ---------------------------------------------------------------- uninstall

if ($Key -eq '') {
    Write-Outcome $false 'no-entry' '' ''
    exit 0
}

$isAdmin = $false
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} catch {
    $isAdmin = $false
}

if (-not $isAdmin) {
    $tempResult = Join-Path $env:TEMP ('wt-uninstall-' + [guid]::NewGuid().ToString('N') + '.json')
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action uninstall -Key `"$Key`" -Result `"$tempResult`""

    $started = $true
    try {
        Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait | Out-Null
    } catch {
        $started = $false
    }

    if (-not $started) {
        Write-Outcome $false 'canceled' '' ''
        exit 0
    }

    if (Test-Path -LiteralPath $tempResult) {
        $raw = [System.IO.File]::ReadAllText($tempResult)
        Remove-Item -LiteralPath $tempResult -Force -ErrorAction SilentlyContinue
        $raw = $raw.Trim().TrimStart([char]0xFEFF)
        Write-Output ('UNINSTALL_ACTION_JSON:' + $raw)
    } else {
        Write-Outcome $false 'no-result' '' ''
    }
    exit 0
}

$props = Get-ItemProperty -LiteralPath $Key -ErrorAction SilentlyContinue
if (-not $props) {
    Write-Outcome $false 'no-entry' '' ''
    exit 0
}

$name = ([string]$props.DisplayName).Trim()
$command = ([string]$props.UninstallString).Trim()
if ($command -eq '') { $command = ([string]$props.QuietUninstallString).Trim() }
if ($command -eq '') {
    Write-Outcome $false 'no-command' $name ''
    exit 0
}

# Run the vendor uninstaller through cmd.exe so the stored command line keeps
# its own quoting. StartInfo.Arguments is passed verbatim to CreateProcess,
# which avoids the quoting pitfalls of Start-Process -ArgumentList.
try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/c ' + $command
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
} catch {
    Write-Outcome $false 'error' $name $_.Exception.Message
    exit 0
}

# Wait up to 5 minutes for the user to finish the uninstaller wizard.
$finished = $proc.WaitForExit(300000)

if (-not $finished) {
    Write-Outcome $false 'pending' $name ''
    exit 0
}

if (Test-Path -LiteralPath $Key) {
    Write-Outcome $false 'still-present' $name ''
} else {
    Write-Outcome $true 'removed' $name ''
}
exit 0
