@echo off
%1 start "" mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c pushd ""%~dp0"" && ""%~s0"" ::","","runas",1)(window.close)&&exit
netsh  interface ip set address "WLAN" source=dhcp
netsh  interface ip set dns "WLAN" source=dhcp
netsh  interface ip set address "��������" source=dhcp
netsh  interface ip set dns "��������" source=dhcp
netsh  interface ip set address "��̫��" source=dhcp
netsh  interface ip set dns "��̫��" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set address "�������� %%i" source=dhcp 
for /L %%i in (1,1,20) do netsh  interface ip set dns "�������� %%i" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set address "��̫�� %%i" source=dhcp
for /L %%i in (1,1,20) do netsh  interface ip set dns "��̫�� %%i" source=dhcp
ipconfig/flushdns
start www.baidu.com
echo ִ�����
taskkill /f /im cmd.exe
