@echo off
chcp 936 >nul 2>&1
setlocal

rem 以管理员权限重新运行自身
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList 'elevated' -Verb RunAs -WorkingDirectory '%~dp0'"
    exit /b
)

rem 逐个适配器恢复为 DHCP 自动获取 IP 与 DNS
rem 用 netsh 枚举实际存在的适配器，避免硬编码适配器名称
for /f "tokens=3" %%i in ('netsh interface show interface ^| findstr /c:"Admin State" /v /c:"Interface"') do (
    netsh interface ip set address name="%%i" source=dhcp >nul 2>&1
    netsh interface ip set dns name="%%i" source=dhcp >nul 2>&1
)

ipconfig /flushdns >nul 2>&1
echo 执行完成
