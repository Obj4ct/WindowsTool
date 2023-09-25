@echo off
%1 start "" mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c pushd ""%~dp0"" && ""%~s0"" ::","","runas",1)(window.close)&&exit
netsh  interface ip set address "WLAN" source=dhcp
netsh  interface ip set dns "WLAN" source=dhcp
netsh  interface ip set address "本地连接" source=dhcp
netsh  interface ip set dns "本地连接" source=dhcp
netsh  interface ip set address "以太网" source=dhcp
netsh  interface ip set dns "以太网" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set address "本地连接 %%i" source=dhcp 
for /L %%i in (1,1,20) do netsh  interface ip set dns "本地连接 %%i" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set address "以太网 %%i" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set dns "以太网 %%i" source=dhcp
ipconfig/flushdns
start www.baidu.com
echo 执行完成
taskkill /f /im cmd.exe
