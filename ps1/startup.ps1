$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only startup item collection.
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.
# Source codes (user / machine / machine32 / startup / startupCommon) are
# mapped to Chinese labels on the Qt side.
$items = New-Object System.Collections.ArrayList

$runKeys = @(
    @{ Path = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'; Source = 'user' },
    @{ Path = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run'; Source = 'machine' },
    @{ Path = 'HKLM:\Software\Wow6432Node\Microsoft\Windows\CurrentVersion\Run'; Source = 'machine32' }
)

foreach ($key in $runKeys) {
    if (-not (Test-Path -LiteralPath $key.Path)) { continue }
    $item = Get-Item -LiteralPath $key.Path
    if (-not $item -or -not $item.Property) { continue }
    foreach ($name in @($item.Property)) {
        $command = ''
        try { $command = [string]$item.GetValue($name) } catch { $command = '' }
        if ([string]::IsNullOrWhiteSpace($command)) { continue }
        [void]$items.Add([ordered]@{
            name    = [string]$name
            source  = [string]$key.Source
            command = $command
        })
    }
}

$startupFolders = @(
    @{ Path = [Environment]::GetFolderPath('Startup'); Source = 'startup' },
    @{ Path = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\StartUp"; Source = 'startupCommon' }
)

foreach ($folder in $startupFolders) {
    if (-not $folder.Path) { continue }
    if (-not (Test-Path -LiteralPath $folder.Path)) { continue }
    foreach ($file in @(Get-ChildItem -LiteralPath $folder.Path -File)) {
        [void]$items.Add([ordered]@{
            name    = [string]$file.BaseName
            source  = [string]$folder.Source
            command = [string]$file.FullName
        })
    }
}

Write-Output ('STARTUP_JSON:' + (ConvertTo-Json -InputObject @($items) -Compress -Depth 3))
