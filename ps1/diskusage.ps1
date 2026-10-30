param([string]$Root)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only top-level folder size analysis for the chosen directory.
# Junction / symlink folders are skipped: they would be counted twice and can
# form infinite loops (e.g. "AppData\Local\Application Data").
# Output is a single JSON line prefixed with DISKUSAGE_JSON: for the Qt side.

function Get-FolderStats([string]$Path) {
    $size = [int64]0
    $files = 0
    $stack = New-Object System.Collections.Stack
    $stack.Push($Path)

    while ($stack.Count -gt 0) {
        $current = [string]$stack.Pop()

        try {
            foreach ($file in [System.IO.Directory]::EnumerateFiles($current)) {
                try {
                    $size += [System.IO.FileInfo]::new($file).Length
                    $files++
                } catch { }
            }
        } catch { }

        try {
            foreach ($child in [System.IO.Directory]::EnumerateDirectories($current)) {
                try {
                    $attributes = [System.IO.FileInfo]::new($child).Attributes
                    if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
                    $stack.Push($child)
                } catch { }
            }
        } catch { }
    }

    return [pscustomobject]@{ size = $size; files = $files }
}

$limit = 20

if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output 'DISKUSAGE_JSON:{"error":"path"}'
    exit 1
}

$items = New-Object System.Collections.ArrayList
foreach ($dir in @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue)) {
    $stats = Get-FolderStats $dir.FullName
    if ($stats.size -le 0 -and $stats.files -eq 0) { continue }

    [void]$items.Add([pscustomobject]@{
        name  = [string]$dir.Name
        size  = [int64]$stats.size
        files = [int]$stats.files
    })
}

$sorted = @($items | Sort-Object -Property size -Descending)
$total = [int64]0
foreach ($entry in $sorted) { $total += [int64]$entry.size }
$top = @($sorted | Select-Object -First $limit)

$data = [pscustomobject]@{
    root    = $Root
    total   = $total
    folders = $sorted.Count
    shown   = $top.Count
    items   = @($top)
}

Write-Output ('DISKUSAGE_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
