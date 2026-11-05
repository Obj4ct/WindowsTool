# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

param(
    [ValidateSet('file', 'scan')][string]$Action = 'file',
    [string]$Path = '',
    [string]$Result = ''
)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Heuristics for disguised software: signature, real file header, naming and
# location patterns. Keep this file pure ASCII: localized text is assembled in
# Qt from the emitted "reasons" codes, and Windows PowerShell 5.1 reads a
# BOM-less .ps1 as ANSI.

$PeExt = @('.exe', '.dll', '.sys', '.scr', '.cpl', '.ocx', '.com')
$MsiExt = @('.msi')
$ScriptExt = @('.bat', '.cmd', '.ps1', '.vbs', '.js', '.hta')
$ScanExt = $PeExt + $MsiExt + $ScriptExt
$DocExt = @('.pdf', '.jpg', '.jpeg', '.png', '.gif', '.bmp', '.txt', '.doc',
    '.docx', '.xls', '.xlsx', '.ppt', '.pptx', '.csv', '.rtf', '.htm',
    '.html', '.zip', '.rar', '.7z', '.iso', '.mp3', '.mp4', '.avi', '.mkv',
    '.wav', '.apk')
$SystemNames = @('svchost.exe', 'lsass.exe', 'services.exe', 'winlogon.exe',
    'wininit.exe', 'csrss.exe', 'smss.exe', 'explorer.exe', 'spoolsv.exe',
    'taskhostw.exe', 'conhost.exe', 'rundll32.exe')

function Get-FileHead([string]$FilePath) {
    try {
        $stream = [System.IO.File]::Open($FilePath,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
    } catch {
        return ''
    }
    try {
        $buffer = New-Object byte[] 8
        $read = $stream.Read($buffer, 0, 8)
        if ($read -lt 4) { return '' }
        $hex = ([System.BitConverter]::ToString($buffer, 0, 4)).Replace('-', '')
        if ($hex.StartsWith('4D5A')) { return 'PE' }
        if ($hex.StartsWith('504B')) { return 'ZIP' }
        if ($hex.StartsWith('25504446')) { return 'PDF' }
        if ($hex.StartsWith('FFD8FF')) { return 'JPEG' }
        if ($hex.StartsWith('89504E47')) { return 'PNG' }
        if ($hex.StartsWith('47494638')) { return 'GIF' }
        if ($hex.StartsWith('52494646')) { return 'RIFF' }
        if ($hex.StartsWith('1F8B')) { return 'GZIP' }
        if ($hex.StartsWith('377ABCAF')) { return '7Z' }
        if ($hex.StartsWith('52617221')) { return 'RAR' }
        if ($hex.StartsWith('4D534346')) { return 'CAB' }
        if ($hex.StartsWith('D0CF11E0')) { return 'OLE' }
        if ($hex.StartsWith('38425053')) { return 'PSD' }
        return 'BIN'
    } finally {
        $stream.Dispose()
    }
}

function Get-FileSignature([string]$FilePath, [string]$Ext) {
    $result = [pscustomobject]@{ status = 'NotSupported'; signer = ''; issuer = '' }
    if (($PeExt -notcontains $Ext) -and ($MsiExt -notcontains $Ext) -and
        ($ScriptExt -notcontains $Ext)) {
        return $result
    }
    try {
        $sig = Get-AuthenticodeSignature -LiteralPath $FilePath -ErrorAction Stop
        $result.status = [string]$sig.Status
        if ($sig.SignerCertificate) {
            $result.signer = [string]$sig.SignerCertificate.Subject
            $result.issuer = [string]$sig.SignerCertificate.Issuer
        }
    } catch {
        $result.status = 'UnknownError'
    }
    return $result
}

function New-Finding([string]$FilePath, [bool]$WithHash) {
    $item = Get-Item -LiteralPath $FilePath -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $null }

    $ext = [System.IO.Path]::GetExtension($FilePath).ToLowerInvariant()
    $name = [string]$item.Name
    $magic = Get-FileHead $FilePath
    $sig = Get-FileSignature $FilePath $ext
    $isPe = $PeExt -contains $ext
    $isMsi = $MsiExt -contains $ext
    $isScript = $ScriptExt -contains $ext
    $isExec = $isPe -or $isMsi

    # Double extension such as "invoice.pdf.exe".
    $doubleExt = $false
    $parts = $name.Split('.')
    if ($parts.Count -ge 3) {
        $last = '.' + $parts[$parts.Count - 1].ToLowerInvariant()
        $prev = '.' + $parts[$parts.Count - 2].ToLowerInvariant()
        if (($ScanExt -contains $last) -and ($DocExt -contains $prev)) {
            $doubleExt = $true
        }
    }

    # System process name outside the system directories.
    $systemName = $false
    if ($SystemNames -contains $name.ToLowerInvariant()) {
        $sys32 = Join-Path $env:SystemRoot 'System32'
        $sysWow = Join-Path $env:SystemRoot 'SysWOW64'
        $sysRootExe = Join-Path $env:SystemRoot 'explorer.exe'
        $inSystem = $FilePath.StartsWith($sys32, [System.StringComparison]::OrdinalIgnoreCase) -or
            $FilePath.StartsWith($sysWow, [System.StringComparison]::OrdinalIgnoreCase) -or
            $FilePath.Equals($sysRootExe, [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $inSystem) { $systemName = $true }
    }

    $inTemp = $false
    if ($env:TEMP) {
        $tempRoot = [System.IO.Path]::GetFullPath($env:TEMP)
        if ($FilePath.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            $inTemp = $true
        }
    }

    $score = 0
    $reasons = New-Object System.Collections.ArrayList

    if ($isExec -or $isScript) {
        switch ($sig.status) {
            'HashMismatch' {
                $score += 6
                [void]$reasons.Add('tampered')
            }
            'NotTrusted' {
                $score += 4
                [void]$reasons.Add('untrusted')
            }
            'UnknownError' {
                # Unsigned scripts often report UnknownError; only flag real
                # executables to avoid noise on ordinary .bat/.ps1 files.
                if ($isExec) {
                    $score += 3
                    [void]$reasons.Add('sig-error')
                }
            }
            'NotSigned' {
                if ($isPe) {
                    $score += 2
                    [void]$reasons.Add('unsigned')
                } elseif ($isMsi) {
                    $score += 1
                    [void]$reasons.Add('unsigned')
                }
            }
        }
    }

    if ($isPe -and $magic -ne 'PE' -and $magic -ne '') {
        $score += 6
        [void]$reasons.Add('header-mismatch')
    }
    if ($isMsi -and $magic -ne 'OLE' -and $magic -ne '') {
        $score += 4
        [void]$reasons.Add('header-mismatch')
    }
    if ($doubleExt) {
        $score += 4
        [void]$reasons.Add('double-ext')
    }
    if ($systemName) {
        $score += 6
        [void]$reasons.Add('system-name')
    }
    if ($inTemp -and $isExec -and $sig.status -ne 'Valid') {
        $score += 3
        [void]$reasons.Add('temp-dir')
    }

    $risk = 'normal'
    if ($score -ge 6) { $risk = 'high' }
    elseif ($score -ge 2) { $risk = 'suspicious' }

    $hash = ''
    if ($WithHash) {
        try {
            $hash = (Get-FileHash -LiteralPath $FilePath -Algorithm 'SHA256' -ErrorAction Stop).Hash
        } catch {
            $hash = ''
        }
    }

    return [pscustomobject]@{
        path       = $FilePath
        name       = $name
        size       = [long]$item.Length
        magic      = $magic
        sigStatus  = $sig.status
        signer     = $sig.signer
        issuer     = $sig.issuer
        risk       = $risk
        reasons    = @($reasons)
        hash       = $hash
    }
}

$report = [pscustomobject]@{
    ok    = $false
    code  = ''
    mode  = $Action
    files = @()
}

if ($Action -eq 'file') {
    if (-not $Path) {
        $report.code = 'no-path'
    } elseif (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $report.code = 'not-found'
    } else {
        $finding = New-Finding $Path $true
        if ($null -eq $finding) {
            $report.code = 'read-failed'
        } else {
            $report.ok = $true
            $report.code = 'ok'
            $report.files = @($finding)
        }
    }
} else {
    $roots = New-Object System.Collections.ArrayList
    $candidates = @(
        [Environment]::GetFolderPath('Desktop'),
        (Join-Path $env:USERPROFILE 'Downloads'),
        [Environment]::GetFolderPath('Startup'),
        [Environment]::GetFolderPath('CommonStartup')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            if (-not $roots.Contains($candidate)) { [void]$roots.Add($candidate) }
        }
    }

    $seen = @{}
    $entries = New-Object System.Collections.ArrayList
    foreach ($root in $roots) {
        Get-ChildItem -LiteralPath $root -File -ErrorAction SilentlyContinue |
            Where-Object { $ScanExt -contains $_.Extension.ToLowerInvariant() } |
            ForEach-Object {
                $full = [string]$_.FullName
                if (-not $seen.ContainsKey($full)) {
                    $seen[$full] = $true
                    [void]$entries.Add($_)
                }
            }
    }

    $ordered = @($entries | Sort-Object -Property LastWriteTime -Descending)
    if ($ordered.Count -gt 50) { $ordered = $ordered[0..49] }

    $findings = New-Object System.Collections.ArrayList
    foreach ($entry in $ordered) {
        $finding = New-Finding $entry.FullName $false
        if ($null -ne $finding) { [void]$findings.Add($finding) }
    }

    $report.ok = $true
    $report.code = 'ok'
    $report.files = @($findings)
}

$payload = ConvertTo-Json -InputObject $report -Compress -Depth 6
if ($Result) {
    [System.IO.File]::WriteAllText($Result, $payload,
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Output 'FAKECHECK_DONE'
} else {
    Write-Output ('FAKECHECK_JSON:' + $payload)
}
