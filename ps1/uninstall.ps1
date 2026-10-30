$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only inventory of installed programs, collected from the three uninstall
# registry roots (64-bit, 32-bit and per-user). Entries without a display name,
# system components and updates are skipped. Output is one JSON line prefixed
# with UNINSTALL_JSON: for the Qt side.

$roots = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
)

$limit = 40
$items = New-Object System.Collections.ArrayList

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

        [void]$items.Add([pscustomobject]@{
            name      = $name
            version   = ([string]$props.DisplayVersion).Trim()
            publisher = ([string]$props.Publisher).Trim()
            date      = $date
            size      = $size
        })
    }
}

$sorted = @($items | Sort-Object -Property size -Descending)
$top = @($sorted | Select-Object -First $limit)

$data = [pscustomobject]@{
    total = $sorted.Count
    shown = $top.Count
    items = @($top)
}

Write-Output ('UNINSTALL_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
