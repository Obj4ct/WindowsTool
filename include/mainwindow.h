#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class ProcessRunner;

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
        StartupList      ///< 开机启动项读取（startup.ps1）
    };

    void setupTheme();
    void setupAbout();
    void setupNavigation();
    void setupNetwork();
    void setupSystem();

    // 异步任务分发
    void onTaskFinished(int exitCode);
    void onTaskFailed(const QString &reason);

    // 网络检测流程（异步串联）
    void startAutoCheck();
    void handleAutoCheckPing();
    void handlePingTest();
    void handleDnsCheck();
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
