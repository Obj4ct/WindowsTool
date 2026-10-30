@echo off
chcp 936 >nul 2>&1

rem 重启 explorer 以重建桌面图标缓存
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul 2>&1

if exist "%LOCALAPPDATA%\IconCache.db" del /f /q "%LOCALAPPDATA%\IconCache.db" >nul 2>&1
if exist "%LOCALAPPDATA%\Microsoft\Windows\Explorer" del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*.db" >nul 2>&1

start explorer.exe
echo 执行完成
