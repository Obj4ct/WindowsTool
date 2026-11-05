# WindowsTool 电脑工具箱

一个用 Qt 5 编写的 Windows 桌面小工具箱，把常见的电脑维护操作收拢到一个界面里：
网络检测与修复、系统清理、硬件信息、软件管理、文件工具，以及统一的操作记录与撤销。

所有实际改动系统的动作都交给 `bat/` 与 `ps1/` 下的脚本执行，Qt 端只负责编排流程、
展示结果和做并发保护，因此脚本可以单独运行、单独排查。

- 界面：Qt Widgets（C++17），自绘矢量图标，浅色卡片 + 深色侧栏
- 平台：Windows 10 / 11 x64
- 许可：MIT（见 [LICENSE](LICENSE)）

---

## 功能

界面共 7 个页面，左侧导航切换。

### 概览

| 功能     | 说明                   |
| ------ | -------------------- |
| 一键体检   | 自动切到系统维护页并立即执行电脑体检   |
| 网络检查   | 跳转到网络页，由用户选择具体网络工具   |
| 查端口占用  | 跳转到网络页并刷新端口占用列表      |
| 硬盘健康   | 读取磁盘 S.M.A.R.T. 健康状态 |
| 电脑健康评分 | 汇总磁盘、内存、系统等指标给出评分与建议 |

### 网络问题

| 功能       | 说明                                                                                 |
| -------- | ---------------------------------------------------------------------------------- |
| 一键检测修复   | 按链路 → 路由 → DNS → 代理 → 目标端口 → HTTPS 分层诊断，结果逐层显示在网络页；只报告首个出问题的环节，DNS 异常时尝试修复并请用户确认结果 |
| 看网络信息    | 输出完整的 `ipconfig /all`                                                              |
| 查端口占用    | 列出监听端口与对应进程                                                                        |
| Hosts 设置 | 读取 / 编辑 / 保存 / 恢复 Hosts，保存前自动备份                                                    |
| 换 DNS    | 切换网卡 DNS，支持常用公共 DNS 与手动填写                                                          |
| 刷新网络设置   | 重置网卡 DNS 配置并清理解析缓存（需管理员）                                                           |
| 重置网络组件   | 重新初始化 Winsock（需重启生效）                                                               |

### 系统维护

| 功能            | 说明                             |
| ------------- | ------------------------------ |
| 垃圾清理          | 清理临时文件、预取缓存、错误报告队列与浏览器缓存（需管理员） |
| 图标变白修复        | 重建桌面图标缓存                       |
| 电脑体检          | 检查系统文件、磁盘、内存、启动项等并输出结果卡片       |
| 修复系统文件        | 调用 SFC / DISM 修复系统镜像           |
| 修复 Windows 更新 | 重置更新相关服务与缓存                    |
| 清理更新组件        | 清理 WinSxS 旧组件（需管理员，耗时较长）       |
| 系统还原点         | 列出 / 创建系统还原点，并可打开系统还原界面        |

### 硬件信息

| 功能    | 说明                         |
| ----- | -------------------------- |
| 硬盘健康  | 磁盘型号、介质类型、容量、S.M.A.R.T. 状态 |
| 看电脑配置 | CPU、内存、主板、显卡、系统版本等概要       |
| 电池健康  | 设计容量 / 当前容量、损耗与充电状态        |
| 看实时占用 | CPU、内存、磁盘、网络的实时监控          |

### 软件管理

| 功能     | 说明                         |
| ------ | -------------------------- |
| 开机启动项  | 列出注册表 Run 与启动文件夹项，可禁用 / 启用 |
| 运行中的程序 | 进程列表与资源占用                  |
| 卸载软件   | 列出已安装程序并调用卸载程序             |
| 查看驱动   | 按设备类别列出驱动与版本信息             |

### 实用工具

| 功能     | 说明                           |
| ------ | ---------------------------- |
| 找大文件   | 递归扫描目录，列出体积最大的文件             |
| 看文件夹大小 | 统计一级子目录累计大小与占比               |
| 找重复文件  | 按大小分组 + MD5 校验，可删除多余副本       |
| 右键菜单   | 查看 / 禁用 / 启用资源管理器右键菜单项       |
| 开机用了多久 | 估算开机到进入桌面的耗时                 |
| 文件被谁占用 | 通过 Restart Manager 查出占用文件的进程 |
| C 盘空间图 | 矩形树图展示目录占用，可逐层下钻             |

### 操作记录

所有写操作成功后都会追加到 `%LOCALAPPDATA%\WindowsTool\operations.jsonl`：

- **可撤销**：开机启动项、右键菜单、Hosts 保存 / 恢复，记录行右侧提供「撤销」按钮；
- **不可恢复**：清理、网络修复、DNS 切换、卸载、删除重复文件、更新组件清理、
  SFC / DISM、创建还原点等，状态标注「不可恢复」，不提供撤销；
- 撤销成功只追加一条 `undo` 事件，历史记录不可改写。

---

## 安装

本项目为绿色版，无需安装。

1. 下载或自行构建 `WindowsTool.exe`；
2. 保证 `bat/`、`ps1/` 与 exe 位于同一目录（构建脚本会自动复制）；
3. 双击 `WindowsTool.exe` 即可运行。

涉及系统目录的操作会通过 UAC 弹窗请求管理员权限；拒绝提权时对应功能会提示取消，
不会静默失败。

### 运行环境

| 项目    | 要求                                                      |
| ----- | ------------------------------------------------------- |
| 系统    | Windows 10 / 11 x64                                     |
| 脚本运行时 | PowerShell 5.1（系统自带）                                    |
| 构建依赖  | Qt 5.15.2 (msvc2019_64)、Visual Studio 2022、CMake ≥ 3.16 |
| 界面测试  | Python 3.8+（仅标准库，无第三方依赖）                                |

---

## 构建

一键脚本会自动探测 Qt 与 Visual Studio，生成工程、编译并部署运行库：

```powershell
# Release x64（推荐）
.\build.ps1 -Config Release

# Debug x64
.\build.ps1 -Config Debug
```

产物位于 `build\Release\WindowsTool.exe`（Debug 为 `build\Debug`），Qt 运行库由
`windeployqt` 自动部署，目录可直接运行。

常用参数：

| 参数           | 说明                  |
| ------------ | ------------------- |
| `-QtPath`    | 指定 Qt 安装路径          |
| `-BuildDir`  | 指定构建目录              |
| `-Config`    | `Release` / `Debug` |
| `-Arch`      | `x64` / `Win32`     |
| `-Generator` | 指定 CMake 生成器        |
| `-NoDeploy`  | 跳过运行库部署             |
| `-Clean`     | 清理构建缓存后重建           |
| `-Test`      | 额外构建并运行单元测试         |

也可直接用 Qt Creator / qmake：

```powershell
D:\Qt\5.15.2\msvc2019_64\bin\qmake.exe WindowsTool.pro
```

### 测试

```powershell
# 单元测试（NetCheckTest + LogAppendTest）
.\build.ps1 -Config Debug -Test

# 界面回归测试（直接跑 exe，不需要调试器）
python tests\uitest.py build\Release\WindowsTool.exe
```

界面回归覆盖：命令执行与日志输出、运行中关窗不崩溃、快速切换任务、任务并发
（终止 / 后台运行 / 取消）、后台任务结果回收、操作记录页。

---

## 目录结构

```
WindowsTool/
├── CMakeLists.txt        # CMake 构建配置
├── WindowsTool.pro       # qmake 构建配置（Qt Creator）
├── build.ps1             # 一键构建脚本
├── LICENSE               # MIT 许可全文
├── NOTICE                # 资源与第三方组件说明
├── include/              # 头文件
│   ├── mainwindow.h
│   ├── netcheck.h        # 网络判断逻辑（可独立测试）
│   ├── dnsprobe.h        # 极简 DNS 探测器（A 记录）
│   ├── netdiag.h         # 分层网络诊断引擎
│   ├── netprobe.h        # L4/L6 TCP 与代理端口探测
│   ├── processrunner.h   # 异步进程执行器
│   ├── sysmonitor.h      # 系统实时监控
│   ├── treemapwidget.h   # 矩形树图控件
│   └── uiicons.h         # QPainter 矢量图标
├── src/
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── mainwindow.ui
│   ├── dnsprobe.cpp
│   ├── netdiag.cpp
│   ├── processrunner.cpp
│   ├── sysmonitor.cpp
│   ├── treemapwidget.cpp
│   └── uiicons.cpp
├── bat/                  # 需要管理员权限的系统操作脚本（GBK）
├── ps1/                  # 只读查询与可逆操作脚本（UTF-8 with BOM）
├── resources/
│   ├── ico.qrc           # 图标资源表
│   ├── style.qss         # 全局主题样式
│   ├── tool.ico          # 程序图标
│   └── ico/              # 界面 PNG 图标
│   └── donation/         # 捐赠二维码         
├── tests/
│   ├── netchecktest.cpp
│   ├── logappendtest.cpp
│   └── uitest.py         # 界面回归测试
└── docs/                 # 设计与决策记录
```

`build/`、本机功能清单等本地资料不会进入版本库，见 `.gitignore`。

---

## 贡献指南

欢迎提交 Issue 与 Pull Request。

如果对你有用，请点一个Star！

---

## License

本项目采用 **MIT 协议**，版权归 Copyright (c) 2023 素曜 所有。
完整许可文本见根目录 [LICENSE](LICENSE)。

无法在文件内部附加版权注释的资源（`resources/tool.ico`、`resources/ico/*.png`）
以及第三方组件（Qt 5.15.2 按 LGPLv3 / GPLv3 授权）的归属说明，
统一记录在 [NOTICE](NOTICE) 中。

## 给作者加鸡腿

| 微信                                                                   | 支付宝                                                                       |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| <img src="resources/donation/reward-wx.png" alt="微信赞赏码" width="216"> | <img src="resources/donation/reward-alipay.jpg" alt="支付宝赞赏码" width="196"> |
