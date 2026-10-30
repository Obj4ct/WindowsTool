#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class ProcessRunner;
class QToolButton;
class SysMonitor;

/**
 * 主窗口：GUI 与流程编排。
 *
 * 所有外部命令通过 ProcessRunner 异步执行，界面在脚本运行期间保持响应；
 * 流程推进使用 QTimer 链，而非阻塞主线程的 QTest::qWait。
 *
 * 界面结构（见 resources/style.qss）：
 *   顶栏(标题/关于) + 深色侧边导航 + 浅色卡片内容区 + 深色控制台
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    /** 当前正在执行的任务，用于把异步回调分发到正确的处理逻辑 */
    enum class Task {
        None,
        PingAutoCheck,   ///< 自动检测流程中的 ping
        PingTest,        ///< 用户点击"网络测试"的 ping
        DnsCheck,        ///< DNS 解析检查
        FixDnsAuto,      ///< 自动检测触发的修复，完成后询问用户
        FixDnsManual,    ///< 用户手动刷新网络配置，完成后仅记录日志
        NetInfo,         ///< ipconfig
        Cleaner,         ///< 清理系统临时文件
        WinsockReset,    ///< 重置 Winsock
        FixWhite,        ///< 重建桌面图标缓存
        HealthCheck,     ///< 系统体检（healthcheck.ps1）
        StartupList,     ///< 开机启动项读取（startup.ps1）
        PortsList,       ///< 端口占用读取（ports.ps1）
        LargeFiles,      ///< 大文件扫描（largefiles.ps1）
        DuplicateScan,   ///< 重复文件扫描（duplicates.ps1 scan）
        DuplicateDelete, ///< 删除重复副本（duplicates.ps1 delete）
        DiskHealth,      ///< 磁盘健康检测（diskhealth.ps1）
        HwInfo,          ///< 硬件信息快照（hwinfo.ps1）
        Battery,         ///< 电池健康（battery.ps1）
        ProcessList,     ///< 进程列表（processes.ps1）
        DiskUsage,       ///< 目录占用分析（diskusage.ps1）
        UninstallList,   ///< 已安装程序清单（uninstall.ps1）
        UninstallAction, ///< 执行卸载（uninstall.ps1 -Action uninstall）
        DriverList,      ///< 驱动清单（drivers.ps1）
        HostsRead,       ///< 读取 Hosts（hosts.ps1 -Action read）
        HostsWrite,      ///< 写入/恢复 Hosts（hosts.ps1 -Action save/restore）
        DnsRead,         ///< 读取当前网卡 DNS（dns.ps1 -Action read）
        DnsWrite         ///< 切换 DNS（dns.ps1 -Action apply）
    };

    void setupTheme();
    void setupAbout();
    void setupNavigation();
    void setupOverview();
    void setupPlaceholders();
    void setupNetwork();
    void setupSystem();

    /** 切换功能页（0 概览 / 1 网络 / 2 系统维护 / 3 硬件监控 / 4 系统管理 / 5 实用工具） */
    void showPage(int index);

    /** 把规划中的磁贴置灰，点击只提示"功能开发中" */
    void wirePlaceholderTile(QToolButton *tile, const QString &title,
                             const QString &detail);

    // 异步任务分发
    void onTaskFinished(int exitCode);
    void onTaskFailed(const QString &reason);

    // 网络检测流程（异步串联）
    void startAutoCheck();
    void handleAutoCheckPing();
    void handlePingTest();
    void handleDnsCheck();
    void handleNetworkInfo();
    void runFixDns(bool askAfter);
    void askNetworkRecovered();

    // 系统体检
    void runHealthCheck();
    void handleHealthCheck();
    void addHealthRow(const QString &state, const QString &name,
                      const QString &detail);

    // 开机启动项
    void runStartupList();
    void handleStartupList();
    void addStartupRow(const QString &source, const QString &name,
                       const QString &command);

    // 端口占用
    void runPortsList();
    void handlePortsList();
    void addPortRow(const QString &protocol, const QString &address,
                    const QString &port, const QString &pid,
                    const QString &process, const QString &state);

    // 大文件扫描
    void runLargeFileScan();
    void handleLargeFileScan();
    void addLargeFileRow(const QString &size, const QString &name,
                         const QString &path);

    // 重复文件扫描与清理
    void runDuplicateScan();
    void runDuplicateScanFor(const QString &root);
    void handleDuplicateScan();
    void runDuplicateDelete();
    void handleDuplicateDelete();
    void addDupRow(const QString &badge, const QString &name,
                   const QString &detail);

    // 磁盘健康检测
    void runDiskHealth();
    void handleDiskHealth();
    void addDiskRow(const QString &health, const QString &model,
                    const QString &detail);

    // 硬件信息快照
    void runHwInfo();
    void handleHwInfo();
    void addHwRow(const QString &category, const QString &title,
                  const QString &detail);

    // 电池健康（只读）
    void runBattery();
    void handleBattery();

    // 进程列表（只读）
    void runProcessList();
    void handleProcessList();
    void addProcessRow(const QString &pid, const QString &name,
                       const QString &detail);

    // 目录占用分析（只读）
    void runDiskUsage();
    void handleDiskUsage();
    void addUsageRow(const QString &percent, const QString &name,
                     const QString &detail);

    // 已安装程序清单 / 卸载
    void runUninstallList();
    void handleUninstallList();
    void runUninstall(const QString &key, const QString &name);
    void handleUninstallAction();
    void addAppRow(const QString &size, const QString &name,
                   const QString &detail, const QString &key,
                   bool uninstallable);

    // 驱动清单（只读）
    void runDriverList();
    void handleDriverList();
    void addDriverRow(const QString &name, const QString &detail);

    // Hosts 查看与编辑
    void runHostsRead();
    void handleHostsRead();
    void saveHosts();
    void restoreHosts();
    void runHostsWrite(const QString &action, const QString &source);
    void handleHostsWrite();

    // DNS 切换
    void runDnsRead();
    void handleDnsRead();
    void applyDns(const QString &servers, const QString &label);
    void runDnsWrite(const QString &servers);
    void handleDnsWrite();

    // 实时监控
    void runMonitor();
    void stopMonitor();

    /** 系统页同时只显示一个结果卡片，并把该卡片滚入视野 */
    void showSystemResultCard(QWidget *card);

    /** 恢复备份后用于提示文案 */
    bool m_hostsRestore = false;

    /** 当前默认路由网卡的显示名，DNS 切换时用于提示 */
    QString m_dnsAlias;

    /** 重复文件扫描：扫描目录与已展示的分组数，删除时按同样的分组顺序处理 */
    QString m_dupRoot;
    int m_dupShown = 0;
    int m_dupFiles = 0;
    qint64 m_dupBytes = 0;

    /** CPU/内存/磁盘/网络实时采样器 */
    SysMonitor *m_monitor = nullptr;

    // 工具方法
    void appendLog(const QString &text);
    void clearLog();
    void showNetworkInfo();
    void showMessage(const QString &title, const QString &text,
                     QMessageBox::Icon icon = QMessageBox::Information);
    bool confirmAction(const QString &title, const QString &text,
                       QMessageBox::Icon icon = QMessageBox::Warning);
    bool batExists(const QString &fileName) const;

    Ui::MainWindow *ui;
    ProcessRunner *m_runner;
    Task m_task = Task::None;

    /**
     * 流程世代号。
     *
     * 自动检测用 QTimer 串联多步，期间用户可能点了别的功能。每次新操作
     * 都会让它自增，尚未触发的定时器发现世代号不一致就直接放弃，
     * 避免旧流程覆盖新任务或弹出无关弹窗。
     */
    int m_flow = 0;
};

#endif // MAINWINDOW_H
