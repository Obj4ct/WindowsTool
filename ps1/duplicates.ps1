param(
    [ValidateSet('scan', 'delete')][string]$Action = 'scan',
    [string]$Root = '',
    [int]$Top = 20,
    [int]$MinSize = 1024
)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Duplicate file scanner. Action scan only reads: it groups files by size first
# and hashes (MD5) only the size groups that contain more than one file, then
# reports the groups that waste the most space. Action delete removes the extra
# copies of the first "Top" groups (keeping the earliest created file in each
# group). No elevation is required; files that cannot be deleted are counted.
#
# NOTE: this script is deliberately ASCII-only. PowerShell 5.1 reads BOM-less
# .ps1 files as ANSI, so Chinese text would be mangled. User-facing messages are
# built in Qt from the fields emitted here.

function Get-DupData([string]$Folder, [int]$MinimumSize) {
    $files = @(Get-ChildItem -LiteralPath $Folder -Recurse -File -ErrorAction SilentlyContinue |
               Where-Object { $_.Length -ge $MinimumSize })

    $data = [pscustomobject]@{
        scanned = $files.Count
        groups  = @()
    }

    if ($files.Count -lt 2) { return $data }

    $groups = New-Object System.Collections.ArrayList

    # Hashing every file is expensive, so first group by exact byte length and
    # only hash the groups with at least two candidates.
    $bySize = @($files | Group-Object -Property Length | Where-Object { $_.Count -gt 1 })

    foreach ($sizeGroup in $bySize) {
        $hashed = @()
        foreach ($file in $sizeGroup.Group) {
            $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm MD5 -ErrorAction SilentlyContinue
            if ($hash) {
                $hashed += [pscustomobject]@{ File = $file; Hash = $hash.Hash }
            }
        }

        $byHash = @($hashed | Group-Object -Property Hash | Where-Object { $_.Count -gt 1 })

        foreach ($hashGroup in $byHash) {
            $sorted = @($hashGroup.Group | ForEach-Object { $_.File } |
                        Sort-Object -Property CreationTime, FullName)
            if ($sorted.Count -lt 2) { continue }

            $size = [int64]$sorted[0].Length
            [void]$groups.Add([pscustomobject]@{
                Name   = $sorted[0].Name
                Size   = $size
                Count  = $sorted.Count
                Wasted = $size * ($sorted.Count - 1)
                Files  = $sorted
            })
        }
    }

    $data.groups = @($groups | Sort-Object -Property Wasted -Descending)
    return $data
}

if ($Root -eq '' -or -not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output 'DUPLICATES_JSON:{"error":"no-root"}'
    exit 0
}

if ($Action -eq 'scan') {
    $data = Get-DupData $Root $MinSize
    $shown = @($data.groups | Select-Object -First $Top)

    $dupFiles = 0
    $wasted = [int64]0
    foreach ($group in $data.groups) {
        $dupFiles += ($group.Count - 1)
        $wasted += $group.Wasted
    }

    $items = @()
    foreach ($group in $shown) {
        $paths = @($group.Files | Select-Object -First 4 |
                   ForEach-Object { $_.FullName })
        $items += [pscustomobject]@{
            name   = $group.Name
            size   = $group.Size
            count  = $group.Count
            wasted = $group.Wasted
            paths  = @($paths)
        }
    }

    $out = [pscustomobject]@{
        root     = $Root
        scanned  = $data.scanned
        groups   = $data.groups.Count
        shown    = $items.Count
        dupFiles = $dupFiles
        wasted   = $wasted
        items    = @($items)
    }

    Write-Output ('DUPLICATES_JSON:' + (ConvertTo-Json -InputObject $out -Compress -Depth 5))
    exit 0
}

# ---------------------------------------------------------------- delete

$data = Get-DupData $Root $MinSize

$deleted = 0
$freed = [int64]0
$failed = 0
$index = 0

foreach ($group in $data.groups) {
    $index++
    if ($index -gt $Top) { break }

    $files = @($group.Files)
    for ($i = 1; $i -lt $files.Count; $i++) {
        try {
            $length = [int64]$files[$i].Length
            Remove-Item -LiteralPath $files[$i].FullName -Force -ErrorAction Stop
            $deleted++
            $freed += $length
        } catch {
            $failed++
        }
    }
}

$out = [pscustomobject]@{
    code    = 'done'
    deleted = $deleted
    freed   = $freed
    failed  = $failed
}

Write-Output ('DUPLICATES_ACTION_JSON:' + (ConvertTo-Json -InputObject $out -Compress))
exit 0
