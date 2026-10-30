$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only process snapshot: name, pid, working set, cumulative CPU time and
# command line. Sorted by memory usage; only the top N rows are returned so the
# Qt list stays light (the full count is reported separately).

$limit = 60

# Command lines are not exposed by Get-Process; read them once from CIM.
$cimMap = @{}
try {
    foreach ($c in @(Get-CimInstance -ClassName Win32_Process)) {
        $cimMap[[int]$c.ProcessId] = $c
    }
} catch {
    $cimMap = @{}
}

$items = New-Object System.Collections.ArrayList
foreach ($p in @(Get-Process)) {
    $mem = [int64]$p.WorkingSet64
    if ($mem -le 0) { continue }

    $cpu = $null
    if ($null -ne $p.CPU) { $cpu = [math]::Round([double]$p.CPU, 1) }

    $cmd = ''
    $cim = $cimMap[[int]$p.Id]
    if ($cim -and $cim.CommandLine) { $cmd = ([string]$cim.CommandLine).Trim() }

    [void]$items.Add([pscustomobject]@{
        name = [string]$p.ProcessName
        pid  = [int]$p.Id
        mem  = $mem
        cpu  = $cpu
        cmd  = $cmd
    })
}

$sorted = @($items | Sort-Object -Property mem -Descending)
$top = @($sorted | Select-Object -First $limit)

$data = [pscustomobject]@{
    total = $sorted.Count
    limit = $limit
    items = @($top)
}

Write-Output ('PROCESSES_JSON:' + (ConvertTo-Json -InputObject $data -Compress -Depth 4))
