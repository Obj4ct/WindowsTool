@echo off
chcp 936 >nul 2>&1
setlocal

rem 以管理员权限重新运行自身
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList 'elevated' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

netsh winsock reset
echo 完成！
echo 请重启电脑以使更改生效。
pause
