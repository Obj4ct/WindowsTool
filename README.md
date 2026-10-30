# 用QT实现简易Windows小工具

2026年开始此项目转为AI协助开发

在此之前为古法编程

当时本人担任大学生技术中心技术部部长，很多同学过来解决的电脑问题都差不多，所以整合起来，写了这个程序
<br>

<h1>项目结构</h1>

```
WindowsTool/
├── CMakeLists.txt        # CMake 构建配置（生成 Visual Studio 工程）
├── WindowsTool.pro       # qmake 构建配置（供 Qt Creator 使用）
├── build.ps1             # 一键构建脚本：生成 VS 工程 + 编译 + 部署
├── include/              # 头文件
│   ├── mainwindow.h
│   ├── netcheck.h        # 网络检测判断逻辑（可独立测试）
│   ├── processrunner.h   # 异步进程执行器
│   └── uiicons.h         # QPainter 矢量图标工厂
├── src/                  # 源文件
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── mainwindow.ui
│   ├── processrunner.cpp
│   └── uiicons.cpp
├── tests/                # 单元测试
│   ├── netchecktest.cpp  # 网络判断逻辑 + cmd 参数
│   ├── logappendtest.cpp # 日志纯文本追加
│   └── uitest.py         # 界面回归测试（Python，无依赖）
├── resources/            # 资源
│   ├── ico.qrc           # 图标资源表（运行时路径 :/ico/xxx.png、:/style.qss）
│   ├── style.qss         # 界面主题样式表
│   ├── tool.ico          # 程序图标
│   └── ico/              # 表情包式图标 PNG
└── bat/                  # 功能脚本（运行时需与 exe 同级）
```

<h1>界面</h1>

采用「顶栏 + 深色侧边导航 + 浅色卡片内容区 + 深色控制台」的四区结构：

- **顶栏**：品牌徽章（矢量绘制）、标题、关于按钮
- **侧边栏**：网络 / 系统 / 其他工具三个页面切换，选中项带高亮与左侧蓝色指示条
- **内容区**：白色圆角卡片分组，功能入口为大图标磁贴
- **控制台**：深色等宽字体日志区，带清空按钮

主题全部定义在 `resources/style.qss`（编译进资源，运行时以 `:/style.qss` 加载）。
图标由 `UiIcons` 用 QPainter 在 24×24 逻辑坐标系绘制，可任意换色缩放，
不依赖位图资源，因此深浅色主题共用一套代码。

<h1>构建</h1>

一键脚本（自动探测 Qt5 与 Visual Studio，生成 `.sln` / `.vcxproj` 并编译）：

```powershell
# Release x64
.\build.ps1 -Config Release

# Debug x64
.\build.ps1 -Config Debug
```

> 构建后会自动把 Qt 运行库部署到 `build/<Config>/`，该目录可直接双击运行，
> 无需再手工执行 windeployqt。Debug 会部署 debug 版 Qt（`Qt5Widgetsd.dll` 等），
> Release 部署 release 版，两者互不混用。

常用参数：`-QtPath`（Qt 路径）、`-BuildDir`（构建目录）、`-Config`（Release/Debug）、
`-Arch`（x64/Win32）、`-Generator`、`-NoDeploy`（跳过运行库部署）、`-Clean`（重建缓存）、
`-Test`（额外构建并运行单元测试）。

运行单元测试：

```powershell
.\build.ps1 -Config Debug -Test
```

会依次构建并运行 `NetCheckTest`（网络判断逻辑 + cmd 参数）和
`LogAppendTest`（日志纯文本追加）。

界面层面的回归测试用 Python 脚本（无需调试器，直接跑 exe）：

```powershell
python tests\uitest.py build\Release\WindowsTool.exe
```

覆盖：命令执行与日志输出、**运行中关闭窗口不崩溃**（析构顺序）、
**快速切换任务不误报错误**。

生成后可用 Visual Studio 直接打开 `build/WindowsTool.sln`，或继续沿用 qmake：

```powershell
D:\Qt\5.15.2\msvc2019_64\bin\qmake.exe WindowsTool.pro
```

> 注意：源文件为 UTF-8 无 BOM，MSVC 需加 `/utf-8` 选项（构建配置中已包含），
> 否则中文字符串字面量会因代码页 936(GBK) 解码而断裂（error C2001）。

> 注意：`QTest::qWait` 由 QtCore 导出，因此未链接 Qt5Test 模块，
> 输出目录不会有 `Qt5Test.dll`（单元测试目标除外）。

> 注意：`bat/*.bat` 为 **GBK 编码**（cmd 按代码页 936 读取），
> 修改时不要用 UTF-8 工具改写，否则中文会乱码。

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
