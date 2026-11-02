param(
    [Parameter(Mandatory = $true)][string]$Path
)

$ErrorActionPreference = 'SilentlyContinue'
$ProgressPreference = 'SilentlyContinue'

# Read-only "who is locking this file" lookup via the Restart Manager API.
# Keep this file pure ASCII: Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, so non-ASCII comments can corrupt the script.

$target = $Path
try { $target = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath } catch { }

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WtRestartManager
{
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct RM_UNIQUE_PROCESS
    {
        public int dwProcessId;
        public long ProcessStartTime;   // FILETIME, 8 bytes
    }

    [StructLayout(LayoutKind.Sequential, Pack = 4, CharSet = CharSet.Unicode)]
    public struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string strAppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string strServiceShortName;
        public int ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)] public bool bRestartable;
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    public static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, string strSessionKey);

    [DllImport("rstrtmgr.dll")]
    public static extern int RmEndSession(uint pSessionHandle);

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    public static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames,
        uint nApplications, RM_UNIQUE_PROCESS[] rgApplications, uint nServices, string[] rgsServiceNames);

    [DllImport("rstrtmgr.dll")]
    public static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded, ref uint pnProcInfo,
        [In, Out] RM_PROCESS_INFO[] rgAffectedApps, out uint lpdwRebootReasons);

    public static RM_PROCESS_INFO[] WhoIsLocking(string path, out int status, out uint rebootReason)
    {
        status = 0;
        rebootReason = 0;
        uint handle = 0;
        string key = Guid.NewGuid().ToString();
        int res = RmStartSession(out handle, 0, key);
        if (res != 0) { status = res; return null; }
        try
        {
            string[] resources = new string[] { path };
            res = RmRegisterResources(handle, (uint)resources.Length, resources, 0, null, 0, null);
            if (res != 0) { status = res; return null; }

            uint needed = 0;
            uint count = 0;
            RM_PROCESS_INFO[] buffer = new RM_PROCESS_INFO[0];
            res = RmGetList(handle, out needed, ref count, buffer, out rebootReason);
            if (res == 234)   // ERROR_MORE_DATA
            {
                buffer = new RM_PROCESS_INFO[needed];
                count = needed;
                res = RmGetList(handle, out needed, ref count, buffer, out rebootReason);
                if (res != 0) { status = res; return null; }
                RM_PROCESS_INFO[] result = new RM_PROCESS_INFO[count];
                Array.Copy(buffer, result, count);
                return result;
            }
            if (res != 0) { status = res; return null; }
            return new RM_PROCESS_INFO[0];
        }
        finally
        {
            RmEndSession(handle);
        }
    }
}
"@ -ErrorAction Stop

$status = 0
$reason = 0
$procs = [WtRestartManager]::WhoIsLocking($target, [ref]$status, [ref]$reason)

$items = New-Object System.Collections.ArrayList
if ($procs -ne $null) {
    foreach ($p in $procs) {
        $procId = [int]$p.Process.dwProcessId
        $name = ''
        $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
        if ($proc -ne $null) { $name = [string]$proc.ProcessName }

        # RM_APP_TYPE -> short ASCII enum consumed by the UI
        $type = 'app'
        switch ([int]$p.ApplicationType) {
            1 { $type = 'window' }
            2 { $type = 'window' }
            3 { $type = 'service' }
            4 { $type = 'explorer' }
            5 { $type = 'console' }
            100 { $type = 'critical' }
            1000 { $type = 'critical' }
            default { }
        }

        $appName = [string]$p.strAppName
        if ($appName.Length -eq 0) { $appName = [string]$p.strServiceShortName }

        [void]$items.Add([pscustomobject]@{
            pid  = $procId
            name = $name
            type = $type
            app  = $appName
        })
    }
}

$payload = [pscustomobject]@{
    path      = $target
    status    = $status
    processes = @($items)
}

Write-Output ('FILELOCK_JSON:' + (ConvertTo-Json -InputObject $payload -Compress -Depth 4))
