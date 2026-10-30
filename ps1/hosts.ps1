param(
    [ValidateSet('read', 'save', 'restore')][string]$Action = 'read',
    [string]$Source = '',
    [string]$Result = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# NOTE: this script is deliberately ASCII-only. PowerShell 5.1 reads BOM-less
# .ps1 files as ANSI, so Chinese text would be mangled. User-facing messages are
# built in Qt from the short "code" values emitted here.

$hostsPath = Join-Path $env:SystemRoot 'System32\drivers\etc\hosts'
$backupDir = Join-Path $env:ProgramData 'WindowsTool\hosts-backup'

function Write-Result([bool]$Ok, [string]$Code, [string]$Backup, [string]$Detail) {
    $json = [pscustomobject]@{
        ok     = $Ok
        code   = $Code
        backup = $Backup
        detail = $Detail
    } | ConvertTo-Json -Compress

    if ($Result) {
        [System.IO.File]::WriteAllText($Result, $json, (New-Object System.Text.UTF8Encoding($false)))
    }
    Write-Output ('HOSTS_WRITE_JSON:' + $json)
}

function Get-LatestBackup {
    if (-not (Test-Path -LiteralPath $backupDir)) { return $null }
    $files = @(Get-ChildItem -LiteralPath $backupDir -Filter 'hosts-*.bak' -File -ErrorAction SilentlyContinue |
        Sort-Object -Property LastWriteTime -Descending)
    if ($files.Count -eq 0) { return $null }
    return $files[0]
}

if ($Action -eq 'read') {
    $exists = Test-Path -LiteralPath $hostsPath

    $content = ''
    if ($exists) {
        $content = [System.IO.File]::ReadAllText($hostsPath)
    }

    $latest = Get-LatestBackup
    $backupName = ''
    $backupTime = ''
    if ($latest) {
        $backupName = $latest.Name
        $backupTime = $latest.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
    }

    $count = 0
    if (Test-Path -LiteralPath $backupDir) {
        $count = @(Get-ChildItem -LiteralPath $backupDir -Filter 'hosts-*.bak' -File -ErrorAction SilentlyContinue).Count
    }

    $data = [pscustomobject]@{
        path       = $hostsPath
        exists     = $exists
        content    = $content
        backup     = $backupName
        backupTime = $backupTime
        backups    = $count
    }

    Write-Output ('HOSTS_JSON:' + (ConvertTo-Json -InputObject $data -Compress))
    exit 0
}

# Writing to Hosts requires administrator rights; relaunch ourselves elevated.
$isAdmin = $false
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} catch {
    $isAdmin = $false
}

if (-not $isAdmin) {
    $tempResult = Join-Path $env:TEMP ('wt-hosts-' + [guid]::NewGuid().ToString('N') + '.json')
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action $Action -Result `"$tempResult`""
    if ($Source) { $argList += " -Source `"$Source`"" }

    $started = $true
    try {
        Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -Verb RunAs -Wait | Out-Null
    } catch {
        $started = $false
    }

    if (-not $started) {
        Write-Result $false 'canceled' '' ''
        exit 0
    }

    if (Test-Path -LiteralPath $tempResult) {
        $raw = [System.IO.File]::ReadAllText($tempResult)
        Remove-Item -LiteralPath $tempResult -Force -ErrorAction SilentlyContinue
        $raw = $raw.Trim().TrimStart([char]0xFEFF)
        Write-Output ('HOSTS_WRITE_JSON:' + $raw)
    } else {
        Write-Result $false 'no-result' '' ''
    }
    exit 0
}

try {
    if ($Action -eq 'save') {
        if (-not (Test-Path -LiteralPath $Source)) {
            Write-Result $false 'source-missing' '' ''
            exit 0
        }

        if (-not (Test-Path -LiteralPath $backupDir)) {
            New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
        }

        # Back up the current file before touching it
        $backupName = ''
        if (Test-Path -LiteralPath $hostsPath) {
            $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
            $backupName = "hosts-$stamp.bak"
            Copy-Item -LiteralPath $hostsPath -Destination (Join-Path $backupDir $backupName) -Force
        }

        $text = [System.IO.File]::ReadAllText($Source)
        [System.IO.File]::WriteAllText($hostsPath, $text, (New-Object System.Text.UTF8Encoding($false)))

        # Keep only the newest 10 backups
        $old = @(Get-ChildItem -LiteralPath $backupDir -Filter 'hosts-*.bak' -File -ErrorAction SilentlyContinue |
            Sort-Object -Property LastWriteTime -Descending | Select-Object -Skip 10)
        foreach ($file in $old) {
            Remove-Item -LiteralPath $file.FullName -Force -ErrorAction SilentlyContinue
        }

        Write-Result $true 'saved' $backupName ''
    } elseif ($Action -eq 'restore') {
        $latest = Get-LatestBackup
        if (-not $latest) {
            Write-Result $false 'no-backup' '' ''
            exit 0
        }

        Copy-Item -LiteralPath $latest.FullName -Destination $hostsPath -Force
        Write-Result $true 'restored' $latest.Name ''
    }
} catch {
    Write-Result $false 'error' '' $_.Exception.Message
    exit 0
}
