param([string]$Root)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only one-level space map for the folder map UI.
# Every immediate child folder is summed recursively; loose files in the
# scanned folder are merged into one entry so the map stays readable.
# Junction / symlink folders are skipped to avoid double counting and loops.
# Output is a single JSON line prefixed with TREEMAP_JSON: for the Qt side.
# Keep this file pure ASCII (Windows PowerShell 5.1 reads .ps1 as ANSI).

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

$limit = 40

if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output 'TREEMAP_JSON:{"error":"path"}'
    exit 1
}

$items = New-Object System.Collections.ArrayList

# Loose files directly inside the scanned folder
$looseSize = [int64]0
$looseFiles = 0
try {
    foreach ($file in [System.IO.Directory]::EnumerateFiles($Root)) {
        try {
            $looseSize += [System.IO.FileInfo]::new($file).Length
            $looseFiles++
        } catch { }
    }
} catch { }

foreach ($dir in @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue)) {
    try {
        $attributes = [System.IO.FileInfo]::new($dir.FullName).Attributes
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
    } catch { }

    $stats = Get-FolderStats $dir.FullName
    if ($stats.size -le 0 -and $stats.files -eq 0) { continue }

    [void]$items.Add([pscustomobject]@{
        name  = [string]$dir.Name
        path  = [string]$dir.FullName
        size  = [int64]$stats.size
        files = [int]$stats.files
        isDir = $true
    })
}

if ($looseSize -gt 0) {
    [void]$items.Add([pscustomobject]@{
        name  = '(root files)'
        path  = ''
        size  = [int64]$looseSize
        files = [int]$looseFiles
        isDir = $false
    })
}

$sorted = @($items | Sort-Object -Property size -Descending)
$total = [int64]0
foreach ($entry in $sorted) { $total += [int64]$entry.size }
$top = @($sorted | Select-Object -First $limit)

$data = [pscustomobject]@{
    root    = $Root
    total   = $total
    folders = @($sorted | Where-Object { $_.isDir }).Count
    items   = @($top)
}

Write-Output ('TREEMAP_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
