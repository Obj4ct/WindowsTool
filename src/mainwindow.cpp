#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processrunner.h"
#include "netcheck.h"
#include "uiicons.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace NetCheck;

namespace {

/** 主题配色（与 resources/style.qss 保持一致） */
namespace Theme {
constexpr auto Accent      = "#4A8CFF";
constexpr auto AccentDark  = "#2B6CD4";
constexpr auto BadgeBg     = "#E8F0FE";
constexpr auto NavInactive = "#93A0B8";
}

/** 主题配色 */
const QColor cAccent(Theme::Accent);
const QColor cAccentDark(Theme::AccentDark);
const QColor cBadgeBg(Theme::BadgeBg);

/** MB → "x.x GB" */
QString formatGb(qint64 mb)
{
    return QString::number(static_cast<double>(mb) / 1024.0, 'f', 1);
}

/** 运行时长：秒 → "x 天 y 小时" / "x 小时 y 分钟" */
QString formatUptime(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("不足 1 分钟");

    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;

    if (days > 0)
        return QStringLiteral("%1 天 %2 小时").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1 小时 %2 分钟").arg(hours).arg(minutes);
    return QStringLiteral("%1 分钟").arg(minutes);
}

/** 把样式表文件读入并应用到整个应用 */
bool applyStyleSheet(QApplication &app)
{
    QFile file(":/style.qss");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QString qss = QString::fromUtf8(file.readAll());
    app.setStyleSheet(qss);
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_runner(new ProcessRunner(this))
{
    ui->setupUi(this);

    setWindowIcon(QIcon(":/ico/toolBox.png"));

    setupTheme();
    setupAbout();
    setupNavigation();
    setupNetwork();
    setupSystem();

    connect(m_runner, &ProcessRunner::outputReceived,
            this, [this](const QString &text) {
                // 体检脚本只输出一行 JSON 标记，不把原始数据刷进控制台
                if (m_task == Task::HealthCheck)
                    return;
                appendLog(text);
            });
    connect(m_runner, &ProcessRunner::finished,
            this, &MainWindow::onTaskFinished);
    connect(m_runner, &ProcessRunner::failed,
            this, &MainWindow::onTaskFailed);
}

MainWindow::~MainWindow()
{
    // 顺序很重要：必须先停掉子进程，再销毁 UI。
    //
    // m_runner 是 this 的子对象，会在 ~QObject 时被删除；而 ~ProcessRunner
    // 里的 stop() 会 kill 子进程并 waitForFinished()，这会**同步**触发
    // finished → outputReceived → appendLog()。如果此时 ui 已被 delete，
    // ui->textBrowser 就是野指针 → 解引用 vtable → 读取 0xFFFFFFFFFFFFFFFF
    // → 访问冲突崩溃（0xC0000005）。
    //
    // 所以这里先断开信号并停掉进程，确保不会再有回调打到已销毁的控件上。
    if (m_runner) {
        disconnect(m_runner, nullptr, this, nullptr);
        m_runner->stop();
    }

    delete ui;
}

// ---------------------------------------------------------------- 主题

void MainWindow::setupTheme()
{
    applyStyleSheet(*qApp);

    // 顶栏品牌徽章
    ui->logoLabel->setPixmap(
        UiIcons::makeBadge(UiIcons::Icon::Logo, cBadgeBg, cAccent, 38, 10));

    // 侧边导航图标（选中/悬停时变亮，用 QIcon 的多态实现）
    auto navIcon = [](UiIcons::Icon icon) {
        QIcon ico;
        ico.addPixmap(UiIcons::render(icon, QColor(Theme::NavInactive), 18),
                      QIcon::Normal, QIcon::Off);
        ico.addPixmap(UiIcons::render(icon, QColor("#FFFFFF"), 18),
                      QIcon::Selected, QIcon::Off);
        ico.addPixmap(UiIcons::render(icon, QColor("#FFFFFF"), 18),
                      QIcon::Active, QIcon::Off);
        return ico;
    };
    ui->navNetworkBtn->setIcon(navIcon(UiIcons::Icon::Network));
    ui->navSystemBtn->setIcon(navIcon(UiIcons::Icon::Monitor));
    for (QPushButton *b : {ui->navNetworkBtn, ui->navSystemBtn}) {
        b->setIconSize(QSize(18, 18));
    }

    // 功能磁贴图标
    auto tileIcon = [](UiIcons::Icon icon) {
        QIcon ico;
        ico.addPixmap(UiIcons::render(icon, cAccent, 30), QIcon::Normal, QIcon::Off);
        ico.addPixmap(UiIcons::render(icon, cAccentDark, 30), QIcon::Selected, QIcon::Off);
        ico.addPixmap(UiIcons::render(icon, cAccentDark, 30), QIcon::Active, QIcon::Off);
        return ico;
    };
    ui->btn_autoCheck->setIcon(tileIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_showNetWork->setIcon(tileIcon(UiIcons::Icon::Info));
    ui->btn_netWorkTest->setIcon(tileIcon(UiIcons::Icon::Activity));
    ui->btn_cleaner->setIcon(tileIcon(UiIcons::Icon::Clean));
    ui->btn_fixWhite->setIcon(tileIcon(UiIcons::Icon::Picture));
    ui->btn_healthCheck->setIcon(tileIcon(UiIcons::Icon::Health));

    // 磁贴图标尺寸（QToolButton 不会自动跟随 QSS 的 icon-size）
    for (QToolButton *b : {ui->btn_autoCheck, ui->btn_showNetWork,
                            ui->btn_netWorkTest, ui->btn_cleaner,
                            ui->btn_fixWhite, ui->btn_healthCheck}) {
        b->setIconSize(QSize(30, 30));
    }

    // 次级按钮图标
    ui->btn_refreshDNS->setIcon(UiIcons::make(UiIcons::Icon::Refresh, QColor("#6B7A93"), 16));
    ui->btn_resetWinSock->setIcon(UiIcons::make(UiIcons::Icon::Reset, QColor("#6B7A93"), 16));

    // 控制台
    ui->textBrowser->setOpenExternalLinks(true);

    // 清空日志
    connect(ui->clearLogButton, &QPushButton::clicked,
            this, &MainWindow::clearLog);
}

// ---------------------------------------------------------------- 关于

void MainWindow::setupAbout()
{
    connect(ui->aboutButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle("关于");
        dialog.setWindowIcon(QIcon(":/ico/cool.png"));
        dialog.setMinimumSize(380, 260);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(26, 24, 26, 20);
        layout->setSpacing(6);

        // 顶部徽章 + 标题
        auto *header = new QHBoxLayout;
        header->setSpacing(12);
        auto *badge = new QLabel(&dialog);
        badge->setPixmap(UiIcons::makeBadge(UiIcons::Icon::Logo, cBadgeBg, cAccent, 44, 12));
        badge->setFixedSize(44, 44);
        header->addWidget(badge);

        auto *titleBox = new QVBoxLayout;
        titleBox->setSpacing(2);
        auto *title = new QLabel("WindowsTool 电脑工具箱", &dialog);
        title->setStyleSheet("color:#16202E;font-size:16px;font-weight:600;");
        auto *ver = new QLabel("版本 1.0 · 基于 Qt 5 构建", &dialog);
        ver->setStyleSheet("color:#93A0B4;font-size:12px;");
        titleBox->addWidget(title);
        titleBox->addWidget(ver);
        header->addLayout(titleBox);
        header->addStretch();
        layout->addLayout(header);

        layout->addSpacing(14);

        // 分隔线
        auto *line = new QFrame(&dialog);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color:#E6EAF1;");
        layout->addWidget(line);
        layout->addSpacing(10);

        const QStringList lines = {
            "开发人员: 我真的不会写代码",
            "邮箱: ztheng010726@gmail.com",
            "QQ: 2321852318",
            "技术栈: C++ / Qt Widgets / bat 脚本"
        };
        for (const QString &line : lines) {
            auto *label = new QLabel(line, &dialog);
            label->setStyleSheet("color:#4A576F;font-size:13px;");
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(label);
        }

        layout->addStretch();

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        buttons->button(QDialogButtonBox::Close)->setText("关闭");
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
        layout->addWidget(buttons);

        dialog.exec();
    });
}

// ---------------------------------------------------------------- 侧边导航

void MainWindow::setupNavigation()
{
    auto switchTo = [this](int index) {
        ui->stackedWidget->setCurrentIndex(index);
    };

    connect(ui->navNetworkBtn, &QAbstractButton::clicked, this, [switchTo]() { switchTo(0); });
    connect(ui->navSystemBtn, &QAbstractButton::clicked, this, [switchTo]() { switchTo(1); });

    // 默认停在第一页
    ui->navNetworkBtn->setChecked(true);
    ui->stackedWidget->setCurrentIndex(0);
}

// ---------------------------------------------------------------- 网络模块

void MainWindow::setupNetwork()
{
    // 自动检测并修复
    connect(ui->btn_autoCheck, &QAbstractButton::clicked,
            this, &MainWindow::startAutoCheck);

    // 查看网络信息
    connect(ui->btn_showNetWork, &QAbstractButton::clicked,
            this, &MainWindow::showNetworkInfo);

    // 刷新 DNS
    connect(ui->btn_refreshDNS, &QPushButton::clicked, this, [this]() {
        if (!confirmAction("刷新网络配置",
                           "此操作会把承载默认路由的物理网卡恢复为自动获取 DNS，"
                           "并清除本机 DNS 解析缓存；静态 IP 配置与 VPN/虚拟网卡不会被改动。"
                           "执行前会自动备份当前网络配置。\n\n执行期间网络可能短暂不可用，是否继续？"))
            return;

        ++m_flow;

        clearLog();
        appendLog("正在刷新网络配置，请稍候……");
        runFixDns(false);
    });

    // 刷新 winsock
    connect(ui->btn_resetWinSock, &QPushButton::clicked, this, [this]() {
        if (!confirmAction("重置 Winsock",
                           "此操作将重新初始化 Winsock 套接字目录，用于修复因软件冲突或"
                           "安全软件残留导致的网络协议异常。\n\n"
                           "注意：重置后部分网络性能可能下降，且需要重启计算机才能完全生效。\n\n"
                           "是否继续？"))
            return;

        if (!batExists("winsockReset.bat")) {
            showMessage("缺少脚本文件",
                        "未找到所需的脚本文件 winsockReset.bat。\n\n"
                        "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                        QMessageBox::Critical);
            return;
        }

        ++m_flow;

        clearLog();
        appendLog("正在重置 Winsock，请稍候……");
        m_task = Task::WinsockReset;
        m_runner->runBat("winsockReset.bat");
    });

    // 网络测试
    connect(ui->btn_netWorkTest, &QAbstractButton::clicked, this, [this]() {
        ++m_flow;

        clearLog();
        appendLog("正在测试网络连通性……");
        m_task = Task::PingTest;
        m_runner->runCommand(QStringLiteral("ping www.baidu.com"));
    });
}

/**
 * 异步任务完成回调：按当前任务类型分发。
 * 同一时刻只跑一个任务，ProcessRunner 会自动终止上一个进程。
 */
void MainWindow::onTaskFinished(int exitCode)
{
    Q_UNUSED(exitCode);

    switch (m_task) {
    case Task::PingAutoCheck:
        handleAutoCheckPing();
        break;
    case Task::PingTest:
        handlePingTest();
        break;
    case Task::DnsCheck:
        handleDnsCheck();
        break;
    case Task::FixDnsAuto:
        askNetworkRecovered();
        break;
    case Task::FixDnsManual:
        appendLog("-------------------");
        appendLog("网络配置刷新完成");
        break;
    case Task::Cleaner:
        appendLog("-------------------");
        appendLog("系统临时文件清理完成");
        break;
    case Task::WinsockReset:
        appendLog("-------------------");
        appendLog("Winsock 重置完成，请重启计算机后生效");
        break;
    case Task::FixWhite:
        appendLog("-------------------");
        appendLog("桌面图标缓存重建完成");
        break;
    case Task::HealthCheck:
        handleHealthCheck();
        break;
    case Task::NetInfo:
        appendLog("-------------------");
        appendLog("网络信息获取完成");
        break;
    case Task::None:
        break;
    }

    m_task = Task::None;
}

void MainWindow::onTaskFailed(const QString &reason)
{
    appendLog("-------------------");
    appendLog(QStringLiteral("执行失败：%1").arg(reason));
    showMessage("操作执行失败", reason, QMessageBox::Critical);
    m_task = Task::None;
}

/**
 * 自动检测流程（异步串联）：
 *   ping → DNS 检查 → 必要时修复 → 询问用户
 * 每一步用 QTimer 推进，避免阻塞主线程（原实现用 QTest::qWait 硬延时）。
 */
void MainWindow::startAutoCheck()
{
    ++m_flow;

    clearLog();
    appendLog("请等待，正在检测网络，电脑可能会出现短暂卡顿，此时请勿进行任何操作……");

    // 启动 ping 检测；结果在 handleAutoCheckPing() 中处理
    m_task = Task::PingAutoCheck;
    m_runner->runCommand(QStringLiteral("ping www.baidu.com"));
}

void MainWindow::handleAutoCheckPing()
{
    const QString output = m_runner->collectedOutput();
    const int flow = m_flow;

    if (isPingHostUnknown(output)) {
        appendLog("-------------------");
        appendLog("ping 错误：找不到主机，正在尝试修复网络配置……");

        // 稍作停顿让用户看清提示，再进入 DNS 检查。
        // 期间用户若点了别的功能，世代号变化，这里直接放弃。
        QTimer::singleShot(600, this, [this, flow]() {
            if (flow != m_flow)
                return;
            m_task = Task::DnsCheck;
            m_runner->runBat("dnsCheck.bat");
        });
        return;
    }

    if (isPingTimeout(output)) {
        appendLog("-------------------");
        appendLog("网站请求超时，请检查你的网络网络是否连接！并确保在电脑设置中关闭了代理");
        return;
    }

    // ping 正常，继续检测 DNS
    appendLog("-------------------");
    appendLog("网络连通性正常，正在检测 DNS 配置……");

    QTimer::singleShot(600, this, [this, flow]() {
        if (flow != m_flow)
            return;
        m_task = Task::DnsCheck;
        m_runner->runBat("dnsCheck.bat");
    });
}

void MainWindow::handlePingTest()
{
    const QString output = m_runner->collectedOutput();

    if (isPingTimeout(output)) {
        showMessage("连接超时",
                    "在规定时间内未收到目标主机的响应。\n\n"
                    "可能原因：网络尚未连接、目标站点不可达，或存在代理/防火墙拦截。\n\n"
                    "建议使用「自动检测并修复」定位具体故障环节。",
                    QMessageBox::Critical);
        return;
    }

    if (isPingHostUnknown(output)) {
        showMessage("域名解析失败",
                    "无法将目标域名解析为 IP 地址。\n\n"
                    "可能原因：DNS 服务器不可用，或 DNS 配置有误。\n\n"
                    "建议使用「刷新网络配置」重置 DNS 设置。",
                    QMessageBox::Critical);
        return;
    }

    showMessage("连通性正常", "目标主机响应正常，网络连通性测试已通过。");
}

void MainWindow::handleDnsCheck()
{
    const QString output = m_runner->collectedOutput();
    const int flow = m_flow;

    if (isDnsLookupFailed(output)) {
        appendLog("-------------------");
        appendLog("网络配置出现问题了（无法找到 DNS 服务器），正在尝试修复……");

        QTimer::singleShot(600, this, [this, flow]() {
            if (flow != m_flow)
                return;
            runFixDns(true);
        });
        return;
    }

    appendLog("-------------------");
    appendLog("网络连接正常，自动检测完成");
}

/**
 * 执行 DNS 修复脚本。
 *
 * fixDNS.bat 会自行申请管理员权限，并用 -Wait 等待提权后的实例执行完毕，
 * 因此这里的 finished 信号就代表修复真正结束——不再靠固定延时猜测。
 *
 * @param askAfter true 表示自动检测触发的修复，结束后询问用户是否恢复；
 *                 false 表示用户手动刷新，只记录日志。
 */
void MainWindow::runFixDns(bool askAfter)
{
    if (!batExists("fixDNS.bat")) {
        showMessage("缺少脚本文件",
                    "未找到所需的脚本文件 fixDNS.bat。\n\n"
                    "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    appendLog("-------------------");
    appendLog("正在刷新网络配置，请稍候……");
    appendLog("-------------------");

    m_task = askAfter ? Task::FixDnsAuto : Task::FixDnsManual;
    m_runner->runBat("fixDNS.bat");
}

void MainWindow::askNetworkRecovered()
{
    appendLog("-------------------");
    appendLog("网络配置已重置，请确认当前访问网页是否正常。");

    QMessageBox box(QMessageBox::Question, "验证修复结果",
                    "网络配置已重置。请问当前是否可以正常访问网页？",
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setWindowIcon(QIcon(":/ico/question.png"));
    box.setButtonText(QMessageBox::Yes, "可以正常访问");
    box.setButtonText(QMessageBox::No, "仍然无法访问");

    if (box.exec() == QMessageBox::Yes) {
        appendLog("验证通过，网络已恢复正常。");
    } else {
        showMessage("问题仍未解决",
                    "网络配置已重置，但访问仍不正常。\n\n"
                    "可能是运营商侧故障、代理配置异常，或被安全软件拦截。\n\n"
                    "可通过右上角「关于」获取联系方式以获取进一步支持。");
        appendLog("验证未通过，请检查代理设置或联系技术支持。");
    }
}

void MainWindow::showNetworkInfo()
{
    ++m_flow;

    clearLog();
    appendLog("正在获取网络信息……");
    m_task = Task::NetInfo;
    m_runner->runCommand(QStringLiteral("ipconfig /all"));
}

// ---------------------------------------------------------------- 系统优化

void MainWindow::setupSystem()
{
    // 清理系统垃圾
    connect(ui->btn_cleaner, &QAbstractButton::clicked, this, [this]() {
        if (!batExists("cleaner.bat")) {
            showMessage("缺少脚本文件",
                        "未找到所需的脚本文件 cleaner.bat。\n\n"
                        "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                        QMessageBox::Critical);
            return;
        }

        ++m_flow;

        clearLog();
        appendLog("-------------------");
        appendLog("即将清理系统临时文件。此操作不会影响个人文档。");
        appendLog("-------------------");
        appendLog("系统将弹出权限请求窗口，请选择「是」以继续。"
                  "清理时长取决于临时文件数量，请耐心等待。");

        m_task = Task::Cleaner;
        m_runner->runBat("cleaner.bat");
    });

    // 修复桌面图标变白
    connect(ui->btn_fixWhite, &QAbstractButton::clicked, this, [this]() {
        if (!batExists("fixWhite.bat")) {
            showMessage("缺少脚本文件",
                        "未找到所需的脚本文件 fixWhite.bat。\n\n"
                        "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                        QMessageBox::Critical);
            return;
        }

        ++m_flow;

        clearLog();
        appendLog("正在重建桌面图标缓存，期间屏幕会短暂黑屏，请稍候……");
        m_task = Task::FixWhite;
        m_runner->runBat("fixWhite.bat");
    });

    // 电脑体检
    connect(ui->btn_healthCheck, &QAbstractButton::clicked,
            this, &MainWindow::runHealthCheck);
}

void MainWindow::runHealthCheck()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/healthcheck.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少体检脚本",
                    "未找到 ps1\\healthcheck.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在采集系统信息，请稍候……");

    // 清空上一次的体检结果
    while (QLayoutItem *item = ui->healthRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->healthSummary->setText(QStringLiteral("正在体检……"));
    ui->healthSummary->setProperty("state", QString());
    ui->healthSummary->style()->unpolish(ui->healthSummary);
    ui->healthSummary->style()->polish(ui->healthSummary);
    ui->cardHealth->setVisible(true);

    // 直接用 PowerShell 的 exe + 参数列表启动，不再把整条命令行交给 cmd.exe。
    // 整串命令行里的双引号会先被 cmd.exe 解析、再被 QProcess 做参数转义，
    // PowerShell 最终拿到的 -File 值会带上字面引号，报"路径中具有非法字符"。
    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::HealthCheck;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleHealthCheck()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("HEALTHCHECK_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        appendLog("-------------------");
        appendLog("体检失败：未能读取系统信息。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        showMessage("体检失败",
                    "未能从 PowerShell 获取有效的系统信息。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const int build = data.value(QStringLiteral("os_build")).toInt();
    const QString osVersion = data.value(QStringLiteral("os_version")).toString();
    const qint64 uptime = static_cast<qint64>(
        data.value(QStringLiteral("uptime_seconds")).toDouble());
    const qint64 memTotal = static_cast<qint64>(
        data.value(QStringLiteral("mem_total_mb")).toDouble());
    const qint64 memFree = static_cast<qint64>(
        data.value(QStringLiteral("mem_free_mb")).toDouble());
    const QString diskLetter = data.value(QStringLiteral("disk_letter")).toString();
    const qint64 diskTotal = static_cast<qint64>(
        data.value(QStringLiteral("disk_total_mb")).toDouble());
    const qint64 diskFree = static_cast<qint64>(
        data.value(QStringLiteral("disk_free_mb")).toDouble());
    const int startupCount = data.value(QStringLiteral("startup_count")).toInt();
    const bool pendingReboot = data.value(QStringLiteral("pending_reboot")).toBool();
    const bool proxyEnabled = data.value(QStringLiteral("proxy_enabled")).toBool();

    int warnCount = 0;
    int riskCount = 0;
    int total = 0;
    auto add = [this, &warnCount, &riskCount, &total](const QString &state,
                                                     const QString &name,
                                                     const QString &detail) {
        ++total;
        if (state == QLatin1String("warn"))
            ++warnCount;
        else if (state == QLatin1String("risk"))
            ++riskCount;
        addHealthRow(state, name, detail);
    };

    // 操作系统
    if (build > 0) {
        QString name = build >= 22000 ? QStringLiteral("Windows 11")
                                       : QStringLiteral("Windows 10");
        QString detail = name;
        if (!osVersion.isEmpty())
            detail += QStringLiteral(" · ") + osVersion;
        detail += QStringLiteral(" · Build %1").arg(build);
        add(QStringLiteral("ok"), QStringLiteral("操作系统"), detail);
    } else {
        add(QStringLiteral("warn"), QStringLiteral("操作系统"),
            QStringLiteral("无法读取系统版本"));
    }

    // 运行时长
    {
        const QString state = uptime > 30 * 86400 ? QStringLiteral("risk")
                            : uptime > 7 * 86400  ? QStringLiteral("warn")
                                                  : QStringLiteral("ok");
        QString detail = QStringLiteral("已连续运行 ") + formatUptime(uptime);
        if (state != QLatin1String("ok"))
            detail += QStringLiteral("，建议重启");
        add(state, QStringLiteral("运行时长"), detail);
    }

    // 内存
    if (memTotal > 0) {
        const int usedPercent = static_cast<int>((memTotal - memFree) * 100 / memTotal);
        const QString state = usedPercent >= 95 ? QStringLiteral("risk")
                            : usedPercent >= 85 ? QStringLiteral("warn")
                                                : QStringLiteral("ok");
        QString detail = QStringLiteral("已用 %1%（共 %2 GB，剩余 %3 GB）")
                             .arg(usedPercent)
                             .arg(formatGb(memTotal))
                             .arg(formatGb(memFree));
        if (state != QLatin1String("ok"))
            detail += QStringLiteral("，建议关闭占用较高的程序");
        add(state, QStringLiteral("内存占用"), detail);
    } else {
        add(QStringLiteral("warn"), QStringLiteral("内存占用"),
            QStringLiteral("无法读取内存信息"));
    }

    // 系统盘
    if (diskTotal > 0) {
        const int usedPercent = static_cast<int>((diskTotal - diskFree) * 100 / diskTotal);
        const QString state = usedPercent >= 95 ? QStringLiteral("risk")
                            : usedPercent >= 85 ? QStringLiteral("warn")
                                                : QStringLiteral("ok");
        QString detail = QStringLiteral("系统盘 %1 已用 %2%（剩余 %3 GB / 共 %4 GB）")
                             .arg(diskLetter.isEmpty() ? QStringLiteral("C:") : diskLetter)
                             .arg(usedPercent)
                             .arg(formatGb(diskFree))
                             .arg(formatGb(diskTotal));
        if (state != QLatin1String("ok"))
            detail += QStringLiteral("，建议清理磁盘");
        add(state, QStringLiteral("磁盘空间"), detail);
    } else {
        add(QStringLiteral("warn"), QStringLiteral("磁盘空间"),
            QStringLiteral("无法读取系统盘信息"));
    }

    // 开机启动项
    {
        const QString state = startupCount >= 20 ? QStringLiteral("risk")
                            : startupCount >= 10 ? QStringLiteral("warn")
                                                 : QStringLiteral("ok");
        QString detail = QStringLiteral("共 %1 项开机启动").arg(startupCount);
        if (state != QLatin1String("ok"))
            detail += QStringLiteral("，建议精简");
        add(state, QStringLiteral("开机启动项"), detail);
    }

    // 待重启
    add(pendingReboot ? QStringLiteral("warn") : QStringLiteral("ok"),
        QStringLiteral("待重启操作"),
        pendingReboot ? QStringLiteral("有更新或安装操作等待重启")
                      : QStringLiteral("没有等待重启的操作"));

    // 系统代理
    add(proxyEnabled ? QStringLiteral("warn") : QStringLiteral("ok"),
        QStringLiteral("系统代理"),
        proxyEnabled ? QStringLiteral("系统代理已开启，可能影响网络诊断结论")
                     : QStringLiteral("未开启系统代理"));

    QString summary;
    if (riskCount > 0) {
        summary = QStringLiteral("体检完成：发现 %1 项注意、%2 项建议")
                      .arg(riskCount)
                      .arg(warnCount);
    } else if (warnCount > 0) {
        summary = QStringLiteral("体检完成：%1 项建议关注").arg(warnCount);
    } else {
        summary = QStringLiteral("体检完成：%1 项检查全部正常").arg(total);
    }

    const QString summaryState = riskCount > 0 ? QStringLiteral("risk")
                               : warnCount > 0 ? QStringLiteral("warn")
                                               : QStringLiteral("ok");
    ui->healthSummary->setText(summary);
    ui->healthSummary->setProperty("state", summaryState);
    ui->healthSummary->style()->unpolish(ui->healthSummary);
    ui->healthSummary->style()->polish(ui->healthSummary);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addHealthRow(const QString &state, const QString &name,
                              const QString &detail)
{
    QWidget *row = new QWidget(ui->healthRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    const QString badgeText = state == QLatin1String("risk")
                                  ? QStringLiteral("注意")
                              : state == QLatin1String("warn")
                                  ? QStringLiteral("建议")
                                  : QStringLiteral("通过");

    QLabel *badge = new QLabel(badgeText, row);
    badge->setObjectName(QStringLiteral("healthBadge"));
    badge->setProperty("state", state);
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(46);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("healthName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("healthDetail"));
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    layout->addWidget(badge);
    layout->addWidget(title);
    layout->addWidget(desc, 1);

    ui->healthRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 通用工具

void MainWindow::appendLog(const QString &text)
{
    // 必须用 QTextCursor::insertText() 而不是 QTextBrowser::append()。
    //
    // QTextBrowser::append() 会先判断文本是否"像 HTML"（Qt::mightBeRichText），
    // 若像则交给 HTML 解析器处理。命令输出里出现 < 就可能触发：
    //   ping 输出 "时间<1ms TTL=64"  →  <1ms 被当成非法标签
    //   → HTML 解析器取到无效索引 → 访问 0xFFFFFFFFFFFFFFFF → 访问冲突崩溃
    //
    // insertText() 走纯文本路径，不做任何标记解析，日志内容原样显示。
    QTextCursor cursor(ui->textBrowser->document());
    cursor.movePosition(QTextCursor::End);
    if (!ui->textBrowser->document()->isEmpty()) {
        cursor.insertBlock();
    }
    cursor.insertText(text);
    ui->textBrowser->setTextCursor(cursor);
    ui->textBrowser->ensureCursorVisible();
}

void MainWindow::clearLog()
{
    ui->textBrowser->clear();
}

void MainWindow::showMessage(const QString &title, const QString &text,
                             QMessageBox::Icon icon)
{
    QMessageBox box(icon, title, text, QMessageBox::Ok, this);
    box.setWindowIcon(windowIcon());
    box.exec();
}

bool MainWindow::confirmAction(const QString &title, const QString &text,
                               QMessageBox::Icon icon)
{
    QMessageBox box(icon, title, text, QMessageBox::Yes | QMessageBox::No, this);
    box.setWindowIcon(windowIcon());
    box.setButtonText(QMessageBox::Yes, "确定");
    box.setButtonText(QMessageBox::No, "取消");
    return box.exec() == QMessageBox::Yes;
}

bool MainWindow::batExists(const QString &fileName) const
{
    return QFileInfo::exists(
        QCoreApplication::applicationDirPath() + "/bat/" + fileName);
}
