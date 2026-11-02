@echo off
chcp 936 >nul 2>&1
setlocal

rem 用法：sfcRepair.bat <sfc|dism> <日志文件路径>
set "MODE=%~1"
if "%MODE%"=="" set "MODE=sfc"
set "LOG=%~2"
if "%LOG%"=="" set "LOG=%TEMP%\wt_sfc.log"

rem 以管理员权限重新运行自身，透传模式与日志路径，并等待提权实例结束
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process -FilePath '%~f0' -ArgumentList @('%MODE%','%LOG%') -Verb RunAs -WorkingDirectory '%~dp0' -Wait -PassThru; exit $p.ExitCode"
    exit /b
)

if /i "%MODE%"=="dism" (
    echo 正在修复系统映像，可能需要较长时间……
    dism /Online /Cleanup-Image /RestoreHealth > "%LOG%" 2>&1
) else (
    echo 正在扫描并修复系统文件，可能需要较长时间……
    sfc /scannow > "%LOG%" 2>&1
)

set "RC=%errorlevel%"
echo 修复命令执行完毕，退出码 %RC%
exit /b %RC%
