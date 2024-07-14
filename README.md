# 用QT实现简易Windows小工具  
当时本人担任大学生技术中心技术部部长（说白了技术部就是个修电脑的部门🥲），很多同学过来解决的电脑问题都差不多，什么网络出错了，C盘红了，问题都是这些。
<br>
然后本人突发奇想，为了给技术部做贡献（其实主要是为了提升自己的编程能力，因为那时候刚刚接触Qt不久，正好来练练手🤓）
<h1>实现方法</h1>

头文件:
```cpp
#include <QLabel>
#include <QFont>  //字体
#include <QDialog>
#include <QPushButton>  //按钮
#include <QProcess> //QT进程
#include <QMessageBox>
#include <QtTest/QTest>//时间
#include <Windows.h>
#include <QString>
#include <filesystem> //文件系统
```
对百度网站进行检测，以便检查是否可以连接到网络：
```cpp
QProcess myProcess(this)
QString program = "C:/Windows/System32/cmd.exe"
QStringList arguments
arguments <<"/c" <<"ping www.baidu.com"
myProcess.start(program,arguments)
```
读取DeBug的所有输出，以便进行后续的判断(这种写法可能是错的，因为有的时候即使输出的文字内容与if的内容一致，但是却不会执行if语句，而是执行else)：
```cpp
QByteArray qByteRead = myProcess.readAllStandardOutput();//读取所有输出
QString str=QString::fromLocal8Bit(qByteRead);//转换为QString类型，便于if判断
```
连接不到网络的判断代码如下：
```cpp
str.contains("找不到主机",Qt::CaseSensitive)  //判断是否连接到主机
```
创建一个自定义对话框:
```cpp
QPushButton* okBtn = new QPushButton("可以");
QPushButton* cancelBtn = new QPushButton("不可以");
QMessageBox* myBox = new QMessageBox;
QString str = "是否可以上网？";
myBox->setIcon(QMessageBox::Question);
myBox->setWindowTitle("疑问");
myBox->setText(str);
//设置图标
myBox->setWindowIcon(QIcon(":/ico/question.png"));
myBox->addButton(okBtn, QMessageBox::AcceptRole);
myBox->addButton(cancelBtn, QMessageBox::RejectRole);
myBox->show();
myBox->exec();//阻塞等待用户输入
```
部分脚本文件如下：
```bat
%1 start "" mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c pushd ""%~dp0"" && ""%~s0"" ::","","runas",1)(window.close)&&exit
```
上面这个是获取系统管理员权限。

垃圾清理脚本文件:
```bat
@echo off
%1 start "" mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c pushd ""%~dp0"" && ""%~s0"" ::","","runas",1)(window.close)&&exit
echo 正在清理系统垃圾文件，请稍等......

del /f /s /q %systemdrive%\*.tmp

del /f /s /q %systemdrive%\*._mp

del /f /s /q %systemdrive%\*.log

del /f /s /q %systemdrive%\*.gid

del /f /s /q %systemdrive%\*.chk

del /f /s /q %systemdrive%\*.old

del /f /s /q %systemdrive%\recycled\*.*

del /f /s /q %windir%\*.bak

del /f /s /q %windir%\prefetch\*.*

rd /s /q %windir%\temp & md %windir%\temp

del /f /q %userprofile%\cookies\*.*

del /f /q %userprofile%\recent\*.*

del /f /s /q "%userprofile%\Local Settings\Temporary Internet Files\*.*"

del /f /s /q "%userprofile%\Local Settings\Temp\*.*"

del /f /s /q "%userprofile%\recent\*.*"

echo 清理系统垃圾完成！
pause
```
网络配置脚本文件(因为Windows系统里面网络适配器的命名规则是“本地连接1”，“本地连接2”......,“以太网1”，“以太网2”......,所以我就使用了一个循环，后面的数字范围是1-20，我觉得基本上一个Windows电脑的网络适配器后面的数字不会超过20，其实这里也是投机取巧了😳)：
```bat
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
```
