# 用QT实现简易Windows小工具

2026年开始此项目转为AI协助开发

在此之前为古法编程

当时本人担任大学生技术中心技术部部长，很多同学过来解决的电脑问题都差不多，所以整合起来，写了这个程序

> **在线阅读提示**：本文档描述的是**当前实现**。
> 早期版本的一些做法（同步 `QProcess`、逐块 `fromLocal8Bit` 解码、
> `mshta` 提权、整盘 `del` 清理）已被重构替换，理由见「实现要点」。

**一句话概括**：一个用 Qt 写的 Windows 小工具箱，专注解决「网络连不上」
和「桌面图标变白」这两类最高频的电脑问题。

---

## 功能

| 模块     | 功能         | 说明                               |
| ------ | ---------- | -------------------------------- |
| **网络** | 自动检测并修复    | 依次检测连通性与 DNS，异常时尝试修复并请用户确认结果     |
|        | 查看网络信息     | 输出完整的 `ipconfig /all`            |
|        | 网络测试       | 单次 ping 连通性测试                    |
|        | 刷新网络配置     | 重置网卡 DNS 配置并清理解析缓存（需管理员权限）       |
|        | 重置 WinSock | 重新初始化套接字目录，修复协议栈异常（需重启生效）        |
| **系统** | 垃圾清理       | 清理临时文件、预取缓存、错误报告队列与浏览器缓存（需管理员权限） |
|        | 修复桌面图标变白   | 重建图标缓存，解决桌面图标不显示的问题              |

所有命令的实际执行都由 `bat/` 下的脚本完成，程序只负责编排流程与展示结果。

### 已裁剪的功能

> 原则：功能不是越多越好。一个说不清「什么时候用得上」的入口，
> 只会稀释主功能的注意力，还平白增加维护面。

| 曾有的功能                | 处置        | 理由                                            |
| --------------------- | --------- | --------------------------------------------- |
| 快捷键速查（表格 + 控制台按钮）   | **已删除**   | 与「修复常见电脑问题」的定位无关，系统里随时能查到           |
| 截图工具（磁贴，点了弹一段教学提示）    | **已删除**   | 只是个 Win+Shift+S 的教学贴纸，系统自带功能          |
| 「其他工具」整页               | **已删除**   | 只包含上述两项，内容清空后留个空页更糟                   |
| `bat/help.bat`             | **已删除**   | 全文无任何引用，且行为原始（裸开两个窗口）                    |

裁剪后侧边栏精简为「网络 / 系统」两项。完整决策记录见
[重构方案 5.2](docs/重构方案.md)。

### 弹窗文案规范

弹窗标题一律用**动作或结果**（如「连接超时」「验证修复结果」），
不用「疑问」「别开玩笑」这类情绪词；正文统一为
**发生了什么 → 可能原因 → 建议动作** 三段式；
涉及系统改动的前置警示必须说明影响范围与是否需要重启。

---

## 界面

采用「顶栏 + 深色侧边导航 + 浅色卡片内容区 + 深色控制台」的四区结构：

- **顶栏**：品牌徽章（矢量绘制）、标题、关于按钮
- **侧边栏**：网络 / 系统两个页面切换，选中项带高亮与左侧蓝色指示条
- **内容区**：白色圆角卡片分组，功能入口为大图标磁贴
- **控制台**：深色等宽字体日志区，带清空按钮

**主题**全部定义在 `resources/style.qss`，编译进资源、运行时以 `:/style.qss` 加载——
改配色、圆角、悬停态只需要动这一个文件。

**图标**由 `UiIcons` 用 QPainter 在 24×24 逻辑坐标系绘制，可任意换色缩放，
不依赖位图资源，因此深浅色主题共用一套代码。

---

## 项目结构

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
├── tests/                # 测试
│   ├── netchecktest.cpp  # 网络判断逻辑 + cmd 参数
│   ├── logappendtest.cpp # 日志纯文本追加
│   └── uitest.py         # 界面回归测试（Python，无第三方依赖）
├── resources/            # 资源
│   ├── ico.qrc           # 图标资源表（运行时路径 :/ico/xxx.png、:/style.qss）
│   ├── style.qss         # 界面主题样式表
│   ├── tool.ico          # 程序图标
│   └── ico/              # 表情包式图标 PNG
├── docs/                 # 设计文档
│   └── 重构方案.md        # 网络检测逻辑重构方案（含功能裁剪决策记录）
└── bat/                  # 功能脚本（运行时需与 exe 同级）
    ├── dnsCheck.bat      # DNS 解析检测
    ├── fixDNS.bat        # 刷新网络配置（重置物理网卡 DNS）
    ├── winsockReset.bat  # 重置 Winsock 协议栈
    ├── cleaner.bat       # 清理系统临时文件
    └── fixWhite.bat      # 重建桌面图标缓存
```

---

## 构建

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

### 测试

```powershell
# 单元测试（NetCheckTest 12 项 + LogAppendTest 6 项）
.\build.ps1 -Config Debug -Test

# 界面回归测试（直接跑 exe，不需要调试器）
python tests\uitest.py build\Release\WindowsTool.exe
```

界面回归覆盖三件事：**命令执行与日志输出**、**运行中关闭窗口不崩溃**（析构顺序）、
**快速切换任务不误报错误**。

生成后可用 Visual Studio 直接打开 `build/WindowsTool.sln`，或继续沿用 qmake：

```powershell
D:\Qt\5.15.2\msvc2019_64\bin\qmake.exe WindowsTool.pro
```

### 环境注意事项

> 源文件为 UTF-8 无 BOM，MSVC 需加 `/utf-8` 选项（构建配置中已包含），
> 否则中文字符串字面量会因代码页 936(GBK) 解码而断裂（error C2001）。

> `QTest::qWait` 由 QtCore 导出，因此未链接 Qt5Test 模块，
> 输出目录不会有 `Qt5Test.dll`（单元测试目标除外）。

> `bat/*.bat` 为 **GBK 编码**（cmd 按代码页 936 读取），
> 修改时不要用 UTF-8 工具改写，否则中文会乱码。

---

## 实现要点

这一节记录**为什么这么写**，而不是罗列代码。

### 异步执行，而不是同步等待

早期实现用 `QProcess::waitForFinished()` 轮询 + `repaint()` 伪异步，
配合 `QTest::qWait` 硬延时，界面最长会卡死 20 秒以上。

现在由 `ProcessRunner` 统一管理：事件驱动、输出实时回传，
流程推进用 QTimer 链，界面全程可响应。

### 输出解码要跨 chunk 保持状态

早期实现对 `readAllStandardOutput()` 的每一块独立用 `QString::fromLocal8Bit` 解码。
GBK 是双字节编码，**一个汉字被切在两块之间就会解出乱码**——
这正是原作者笔记里「输出内容明明与 if 条件一致，却走进 else 分支」的真正原因。

现在改用 `QTextDecoder`，保留跨块解码状态，每次运行重置解码器。

### 跑命令行必须带 `/c`

```cpp
m_runner->runCommand(QStringLiteral("ping www.baidu.com"));
// 内部展开为： cmd.exe /c ping www.baidu.com
```

`cmd.exe` 缺少 `/c` 会进入**交互式 shell 模式**：只打印版本横幅和提示符，
然后等待 stdin——被请求的命令根本不会执行，进程还会以异常状态退出。
表现为「控制台只输出一句话」（那其实是 cmd 的横幅）。

所有命令行一律走 `runCommand()`，不要直接调 `run(cmd(), {...})`。

### 析构顺序不能反

```cpp
MainWindow::~MainWindow()
{
    if (m_runner) {
        disconnect(m_runner, nullptr, this, nullptr);
        m_runner->stop();      // 必须先停子进程
    }
    delete ui;                // 再销毁界面
}
```

`m_runner` 是 `this` 的子对象，会在 `~QObject` 时被删除；而 `~ProcessRunner`
里的 `stop()` 会 kill 子进程并 `waitForFinished()`，这会**同步**触发 `finished`
→ `outputReceived` → `appendLog()`。如果此时 `ui` 已被 delete，
`ui->textBrowser` 就是野指针 → 解引用 vtable → 读 `0xFFFFFFFFFFFFFFFF`
→ 访问冲突（0xC0000005）。

### 流程串联要用「世代号」防竞态

「自动检测并修复」是用 `QTimer` 串起来的多步流程：
ping → （等 600ms）→ nslookup → （等 600ms）→ 修复 → 询问用户。

问题在于**这些等待期间用户完全可以去点别的功能**。旧流程的定时器醒来后
不知道"世界已经变了"，会继续往下走——把新任务的界面覆盖掉，
或者弹出一个与当前操作毫不相干的对话框。

解法是给每个操作分配一个**世代号**，等待前记下当前世代，
醒来时发现世代变了就直接放弃：

```cpp
int m_flow = 0;   // 每次新操作 ++m_flow

const int flow = m_flow;              // 记下当前世代
QTimer::singleShot(600, this, [this, flow]() {
    if (flow != m_flow)               // 世代变了 → 用户已开始了别的操作
        return;
    m_task = Task::DnsCheck;
    m_runner->runBat("dnsCheck.bat");
});
```

比"取消定时器"更可靠——因为定时器可能已经进了事件队列，
`QTimer::stop()` 未必拦得住。

### Task 枚举要按「谁在跑」细分

早期只有 `PingAutoCheck / PingTest / DnsCheck / NetInfo` 四个，
所有 bat 脚本共用一个 `Task`，结果只能靠 `exitCode` 猜。

现在每个会产生输出的脚本都有独立的 Task
（`FixDnsAuto / FixDnsManual / Cleaner / WinsockReset / FixWhite`），
`onTaskFinished` 里按 Task 精确分发，不再猜。

顺带解决的另一个问题：**修复脚本什么时候算真正结束？**

早期用 `QTimer::singleShot(5000, ...)` 猜一个固定延时，
网络慢的时候脚本还没跑完就弹询问框。现在 `fixDNS.bat` 提权时用
`Start-Process -Wait -PassThru` 把退出码传回父进程，
父脚本的退出码即代表子脚本的退出码——`finished` 信号这才真正可靠。

### 主动中止不该报「进程异常终止」

`stop()` 会 `kill()` 掉上一个还没跑完的进程，
而 `kill()` 必然让 Qt 把退出状态报成 `CrashExit`。
用户主动切换功能时这不是错误，所以加了 `m_aborted` 标志加以区分：

```cpp
m_aborted = true;
m_process->kill();
m_process->waitForFinished(1000);
```

`onFinished` / `onError` 里遇到 `m_aborted` 就静默返回，
不再弹「进程异常终止」。

### 日志必须走纯文本追加

```cpp
QTextCursor cursor(ui->textBrowser->document());
cursor.movePosition(QTextCursor::End);
cursor.insertText(text);
```

不能用 `QTextBrowser::append()`——它会先判断文本是否「像 HTML」
（`Qt::mightBeRichText`），若像则交给 HTML 解析器，
命令输出里的尖括号会被当成标签吃掉。

### DNS 失败的判断特征

**不能用** `服务器:  UnKnown` 作为失败特征——该行在 DNS 完全正常时也会出现
（nslookup 无法反向解析 DNS 服务器自身的主机名时会打印它）。
早期实现正因此把正常网络误判为故障，触发无谓的修复。

现在只匹配真正的失败特征（Non-existent domain / can't find /
No response from server / timed out / SERVFAIL），并有单元测试覆盖。

### bat 脚本的加固

早期脚本有几处需要特别注意的地方，现已全部重写：

| 原写法                                   | 问题                             | 现状                                         |
| -------------------------------------- | ------------------------------ | ------------------------------------------ |
| `mshta vbscript:...` 提权                | Win11 24H2+ 已弃用 VBScript       | 改用 PowerShell `Start-Process -Verb RunAs`   |
| `del /f /s /q %systemdrive%\*.tmp`     | **整盘递归删除**，会误删其他软件与用户文件   | 只清 `%TEMP%` 等明确临时目录                        |
| `taskkill /f /im cmd.exe`              | 会强杀**用户所有** cmd 窗口         | 移除                                          |
| 硬编码「本地连接1..20」循环                       | 适配器命名规则不可靠                 | 用 `Get-NetAdapter` 枚举实际适配器（详见下节）             |

### 刷新网络配置：只动真正在用的那张网卡

这一项最容易写坏，写坏了会**把用户的网络配置搞乱**。三个坑：

**坑一：枚举适配器要避开系统语言。**
`netsh interface show interface` 的**表头随系统语言变化**——中文系统是
「管理员状态 / 状态 / 类型 / 接口名称」，英文系统才是 `Admin State`。
按英文过滤在中文系统上一条都匹配不到，表现为**点了「刷新网络配置」没有任何反应**。
现在改用 PowerShell 的 `Get-NetAdapter`，字段名与语言无关。

**坑二：不能把所有网卡都重置成 DHCP。**
本机实测同时挂着 5 个 Up 的适配器：

```
[物理] 以太网          ← 物理网卡
[物理] WLAN            ← 物理网卡
[跳过] xray_tun        Status=Up  HW=False   ← Wintun VPN 隧道
[跳过] VMware VMnet1   Status=Up  HW=False
[跳过] VMware VMnet8   Status=Up  HW=False
```

一刀切地重置全部网卡会：抹掉用户手工设的**静态 IP**；
把 VPN 虚拟网卡也去设 DHCP（这类适配器压根不适用）。

正确做法是**顺着默认路由找到真正在用的那张物理网卡**，只动它：

```powershell
$routes = Get-NetRoute -DestinationPrefix '0.0.0.0/0' | Sort-Object RouteMetric
foreach ($r in $routes) {
    $a = Get-NetAdapter -InterfaceIndex $r.ifIndex
    if ($a -and $a.HardwareInterface -and $a.Status -eq 'Up') { $target = $a; break }
}
```

本机实测（默认路由有三条，按 Metric 排序）：

```
ifIndex=3  Metric=0    NextHop=192.168.3.1   →  WLAN      ★ 命中
ifIndex=13 Metric=0    NextHop=0.0.0.0       →  xray_tun    (HW=False，跳过)
ifIndex=17 Metric=256  NextHop=192.168.9.254 →  以太网      (排在后面)
```

命中 `WLAN`，`xray_tun` 与 `以太网` 正确跳过。

**坑三：改动之前必须先备份。**
`ipconfig /all` 存一份带时间戳的快照到 `%ProgramData%\WindowsTool\`，
用户网络出问题时有据可查。

另外，脚本用 `Set-DnsClientServerAddress -ResetServerAddresses`
而不是 `netsh interface ip set ...`，同样是為了绕开 `netsh` 的本地化输出；
并且先判断网卡是否为 DHCP 模式，**静态 IP 的网卡只重置 DNS、不动 IP**。

---

## 已知问题

- **DNS 检测依赖 `nslookup` 的输出文案**，各语言/版本存在差异。
  计划在网络诊断重构（阶段 1）里改为自实现 UDP DNS 查询，彻底摆脱文本解析。
- **提权脚本会弹 UAC 并另开进程**，程序侧拿不到其 stdout
  （修复脚本的完成判定已通过 `-Wait -PassThru` 传递退出码解决，
  但过程输出仍看不到）。
- **修复后目前是弹窗请用户确认**结果，不是自动复检。
  自动复检属于网络诊断重构方案（阶段 2）的内容。
- **「刷新网络配置」只重置默认路由那张物理网卡的 DNS**，
  静态 IP 的网卡只动 DNS 不动 IP；但如果用户同时用多个网卡分流，
  非默认路由的那张不会被处理。
- **分层诊断尚未实现**，当前仍是「ping + nslookup」的二值判断，
  无法区分「DNS 坏了」和「代理拦了」这类不同成因。

---

## 开发计划

重构方案（分层诊断、精准修复、公共 DNS 一键切换等）见：

**[`docs/重构方案.md`](docs/重构方案.md)**

该文档包含现状调研、分层诊断模型、功能清单（P0–P2）、分阶段实施路线，
以及「功能裁剪」决策记录。

当前进度：

| 阶段   | 内容                          | 状态        |
| ---- | --------------------------- | --------- |
| 阶段 0 | 功能裁剪、弹窗文案规范化            | ✅ 已完成     |
| 阶段 1 | DNS 探测器 + 诊断引擎 + 本机状态 JSON   | ⬜ 未开始     |
| 阶段 2 | 接入 UI，分层诊断面板                 | ⬜ 未开始     |
| 阶段 3 | 精准修复（改写 `fixDNS`）              | ⬜ 未开始     |
| 阶段 4 | DNS 测速、多目标、公共 DNS 切换          | ⬜ 未开始     |
