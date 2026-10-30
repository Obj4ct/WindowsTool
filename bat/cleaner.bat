@echo off
chcp 936 >nul 2>&1
setlocal

rem 以管理员权限重新运行自身（用 PowerShell 提权，兼容 Win11 24H2+ 已弃用的 VBScript/mshta）
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList 'elevated' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

echo 正在清理系统垃圾文件，请稍等......

rem 只清理明确的临时目录与缓存目录，不做整盘递归删除
if exist "%TEMP%" del /f /q "%TEMP%\*.*" >nul 2>&1
if exist "%LOCALAPPDATA%\Temp" del /f /q "%LOCALAPPDATA%\Temp\*.*" >nul 2>&1
if exist "%SYSTEMROOT%\Temp" del /f /q "%SYSTEMROOT%\Temp\*.*" >nul 2>&1
if exist "%SYSTEMROOT%\Prefetch" del /f /q "%SYSTEMROOT%\Prefetch\*.*" >nul 2>&1

rem 清理 Windows 错误报告队列
if exist "%PROGRAMDATA%\Microsoft\Windows\WER\ReportQueue" del /f /s /q "%PROGRAMDATA%\Microsoft\Windows\WER\ReportQueue\*" >nul 2>&1

rem 清理 IE/Edge 缓存（Win10+ 的实际路径，取代已失效的 "Local Settings"）
if exist "%LOCALAPPDATA%\Microsoft\Windows\INetCache" del /f /q "%LOCALAPPDATA%\Microsoft\Windows\INetCache\IE\*" >nul 2>&1
if exist "%LOCALAPPDATA%\Microsoft\Windows\WebCache" del /f /q "%LOCALAPPDATA%\Microsoft\Windows\WebCache\*" >nul 2>&1

echo 清理系统垃圾完成！
echo.
echo 注意：出于安全考虑，本次清理只处理临时文件与缓存，不会删除您的个人文档。
pause
