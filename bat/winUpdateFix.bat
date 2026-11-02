@echo off
chcp 936 >nul 2>&1
setlocal

rem 用法：winUpdateFix.bat <日志文件路径>
set "LOG=%~1"
if "%LOG%"=="" set "LOG=%TEMP%\wt_wupdate.log"

rem 以管理员权限重新运行自身，透传日志路径，并等待提权实例结束
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process -FilePath '%~f0' -ArgumentList @('%LOG%') -Verb RunAs -WorkingDirectory '%~dp0' -Wait -PassThru; exit $p.ExitCode"
    exit /b
)

echo Windows 更新修复开始> "%LOG%"
echo.>> "%LOG%"

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "TS=%%i"

echo 正在停止更新相关服务……>> "%LOG%" 2>&1
for %%s in (wuauserv bits cryptsvc msiserver) do (
    net stop %%s>> "%LOG%" 2>&1
)

echo.>> "%LOG%"
echo 正在备份并重置更新缓存目录……>> "%LOG%"
set "RC=0"
if exist "%SystemRoot%\SoftwareDistribution" (
    move /y "%SystemRoot%\SoftwareDistribution" "%SystemRoot%\SoftwareDistribution.%TS%.bak">> "%LOG%" 2>&1
    if errorlevel 1 set "RC=1"
) else (
    echo 未找到 SoftwareDistribution，跳过。>> "%LOG%"
)
if exist "%SystemRoot%\System32\catroot2" (
    move /y "%SystemRoot%\System32\catroot2" "%SystemRoot%\System32\catroot2.%TS%.bak">> "%LOG%" 2>&1
    if errorlevel 1 set "RC=1"
) else (
    echo 未找到 catroot2，跳过。>> "%LOG%"
)

echo.>> "%LOG%"
echo 正在重新启动更新相关服务……>> "%LOG%"
for %%s in (wuauserv bits cryptsvc msiserver) do (
    net start %%s>> "%LOG%" 2>&1
)

echo.>> "%LOG%"
if "%RC%"=="0" (
    echo 更新组件已重置，旧缓存已重命名为 .%TS%.bak，需要时可手动恢复。>> "%LOG%"
    echo 更新组件已重置，旧缓存已备份为 .%TS%.bak
) else (
    echo 部分缓存目录重置失败，请查看上方日志。>> "%LOG%"
    echo 部分缓存目录重置失败，请查看上方日志。
)
exit /b %RC%
