param([string]$Root)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only large file scan: list the top 50 files by size under $Root.
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.
$items = New-Object System.Collections.ArrayList

if ($Root -and (Test-Path -LiteralPath $Root)) {
    $files = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force)
    $top = @($files | Sort-Object -Property Length -Descending | Select-Object -First 50)
    foreach ($f in $top) {
        [void]$items.Add([pscustomobject]@{
            name = [string]$f.Name
            path = [string]$f.FullName
            size = [long]$f.Length
        })
    }
}

Write-Output ('LARGEFILES_JSON:' + (ConvertTo-Json -InputObject @($items) -Compress -Depth 3))
