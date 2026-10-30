@echo off
chcp 936 >nul 2>&1
setlocal

rem 以管理员权限重新运行自身，并等待提权后的实例执行完毕再返回
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process -FilePath '%~f0' -ArgumentList 'elevated' -Verb RunAs -WorkingDirectory '%~dp0' -Wait -PassThru; exit $p.ExitCode"
    exit /b
)

echo 正在备份当前网络配置……
set "BACKUP=%ProgramData%\WindowsTool"
if not exist "%BACKUP%" md "%BACKUP%" >nul 2>&1
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%t"
if not defined STAMP set "STAMP=manual"
ipconfig /all > "%BACKUP%\网络配置备份-%STAMP%.txt" 2>&1
echo 备份已保存：%BACKUP%\网络配置备份-%STAMP%.txt

echo 正在重置承载默认路由的物理网卡……
rem 只处理已连接且承载默认路由的物理网卡；VPN / VMware / Wintun 等虚拟网卡不参与，
rem 静态 IP 配置也不会被改写成 DHCP。判断全部走 PowerShell，不依赖系统语言。
powershell -NoProfile -ExecutionPolicy Bypass -Command "$routes = Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Sort-Object RouteMetric; $target = $null; foreach ($r in $routes) { $a = Get-NetAdapter -InterfaceIndex $r.ifIndex -ErrorAction SilentlyContinue; if ($a -and $a.HardwareInterface -and $a.Status -eq 'Up') { $target = $a; break } }; if (-not $target) { $target = Get-NetAdapter -Physical -ErrorAction SilentlyContinue | Where-Object { $_.Status -eq 'Up' } | Select-Object -First 1 }; if ($target) { Write-Host ('目标网卡：' + $target.Name); $ip = Get-NetIPInterface -InterfaceIndex $target.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue; if ($ip -and $ip.Dhcp -eq 'Enabled') { Set-NetIPInterface -InterfaceIndex $target.ifIndex -Dhcp Enabled -ErrorAction SilentlyContinue }; Set-DnsClientServerAddress -InterfaceIndex $target.ifIndex -ResetServerAddresses -ErrorAction SilentlyContinue; Write-Host '已重置该网卡的 DNS 配置' } else { Write-Host '未找到已连接的物理网卡' }"

ipconfig /flushdns >nul 2>&1
echo 执行完成
