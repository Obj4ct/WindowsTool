#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processrunner.h"
#include "netcheck.h"
#include "sysmonitor.h"
#include "uiicons.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QScrollArea>
#include <QScrollBar>
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

/** 字节数 → "1.2 GB" / "345 MB" / "12 KB" */
QString formatFileSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(
            QString::number(static_cast<double>(bytes) / (1024.0 * 1024 * 1024), 'f', 2));
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(
            QString::number(static_cast<double>(bytes) / (1024.0 * 1024), 'f', 1));
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(
            QString::number(static_cast<double>(bytes) / 1024.0, 'f', 0));
    return QStringLiteral("%1 B").arg(bytes);
}

/** 启动项来源代码（ps1 输出英文，避免脚本编码问题）→ 中文标签 */
QString startupSourceLabel(const QString &code)
{
    if (code == QLatin1String("machine") || code == QLatin1String("machine32"))
        return QStringLiteral("所有用户");
    if (code == QLatin1String("startup"))
        return QStringLiteral("启动文件夹");
    if (code == QLatin1String("startupCommon"))
        return QStringLiteral("公共启动项");
    return QStringLiteral("当前用户");
}

/** 磁盘介质类型（ps1 输出英文枚举）→ 中文标签 */
QString diskMediaLabel(const QString &code)
{
    if (code == QLatin1String("HDD"))
        return QStringLiteral("机械硬盘");
    if (code == QLatin1String("SSD"))
        return QStringLiteral("固态硬盘");
    if (code == QLatin1String("SCM"))
        return QStringLiteral("存储级内存");
    return QStringLiteral("未知介质");
}

/** 磁盘健康状态（ps1 输出英文枚举）→ 中文标签 + 徽章配色代号 */
QString diskHealthLabel(const QString &code, QString *state)
{
    if (code == QLatin1String("Healthy")) {
        *state = QStringLiteral("ok");
        return QStringLiteral("健康");
    }
    if (code == QLatin1String("Warning")) {
        *state = QStringLiteral("warn");
        return QStringLiteral("警告");
    }
    if (code == QLatin1String("Unhealthy")) {
        *state = QStringLiteral("bad");
        return QStringLiteral("异常");
    }
    *state = QStringLiteral("warn");
    return QStringLiteral("未知");
}

/** 设备类别（WMI DeviceClass 全大写英文枚举）→ 中文标签，未收录的保留原值 */
QString driverClassLabel(const QString &code)
{
    const QString key = code.toUpper();
    if (key == QLatin1String("DISPLAY")) return QStringLiteral("显示适配器");
    if (key == QLatin1String("MONITOR")) return QStringLiteral("显示器");
    if (key == QLatin1String("KEYBOARD")) return QStringLiteral("键盘");
    if (key == QLatin1String("MOUSE")) return QStringLiteral("鼠标");
    if (key == QLatin1String("HIDCLASS")) return QStringLiteral("人体学输入设备");
    if (key == QLatin1String("NET")) return QStringLiteral("网络适配器");
    if (key == QLatin1String("USB")) return QStringLiteral("USB 控制器");
    if (key == QLatin1String("HDC")) return QStringLiteral("磁盘控制器");
    if (key == QLatin1String("DISKDRIVE")) return QStringLiteral("磁盘驱动器");
    if (key == QLatin1String("CDROM")) return QStringLiteral("光驱");
    if (key == QLatin1String("VOLUME")) return QStringLiteral("存储卷");
    if (key == QLatin1String("MEDIA")) return QStringLiteral("音频设备");
    if (key == QLatin1String("AUDIOENDPOINT")) return QStringLiteral("音频端点");
    if (key == QLatin1String("AUDIOPROCESSINGOBJECT"))
        return QStringLiteral("音频处理对象");
    if (key == QLatin1String("BLUETOOTH")) return QStringLiteral("蓝牙");
    if (key == QLatin1String("BATTERY")) return QStringLiteral("电池");
    if (key == QLatin1String("CAMERA")) return QStringLiteral("摄像头");
    if (key == QLatin1String("IMAGE")) return QStringLiteral("成像设备");
    if (key == QLatin1String("PRINTER")) return QStringLiteral("打印机");
    if (key == QLatin1String("SMARTCARDREADER"))
        return QStringLiteral("智能卡读卡器");
    if (key == QLatin1String("PROCESSOR")) return QStringLiteral("处理器");
    if (key == QLatin1String("SYSTEM")) return QStringLiteral("系统设备");
    if (key == QLatin1String("SENSOR")) return QStringLiteral("传感器");
    if (key == QLatin1String("FIRMWARE")) return QStringLiteral("固件");
    if (key == QLatin1String("SOFTWAREINFRASTRUCTURE"))
        return QStringLiteral("软件设备");
    if (key.isEmpty() || key == QLatin1String("UNKNOWN"))
        return QStringLiteral("其他设备");
    return code;
}

/** 每秒字节数 → "12.3 KB"（后面由调用方补 "/s"） */
QString formatRate(double bytesPerSec)
{
    if (bytesPerSec >= 1024.0 * 1024.0)
        return QString::number(bytesPerSec / (1024.0 * 1024.0), 'f', 1)
               + QStringLiteral(" MB");
    if (bytesPerSec >= 1024.0)
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(qMax(0.0, bytesPerSec), 'f', 0) + QStringLiteral(" B");
}

/** 电池容量（mWh）→ "56.0 Wh"，0 表示系统未提供 */
QString formatCapacity(qint64 milliwattHours)
{
    if (milliwattHours <= 0)
        return QStringLiteral("—");
    return QString::number(static_cast<double>(milliwattHours) / 1000.0, 'f', 1)
           + QStringLiteral(" Wh");
}

/** Win32_Battery.BatteryStatus → 中文状态 */
QString batteryStatusLabel(int status)
{
    switch (status) {
    case 1:  return QStringLiteral("放电中");
    case 2:  return QStringLiteral("已接通电源（未充电）");
    case 3:  return QStringLiteral("已充满");
    case 4:  return QStringLiteral("电量低");
    case 5:  return QStringLiteral("电量严重不足");
    case 6:  return QStringLiteral("充电中");
    case 7:  return QStringLiteral("充电中（电量高）");
    case 8:  return QStringLiteral("充电中（电量低）");
    case 9:  return QStringLiteral("充电中（电量严重不足）");
    case 11: return QStringLiteral("部分充电");
    default: return QStringLiteral("未知");
    }
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
    setupOverview();
    setupPlaceholders();
    setupNetwork();
    setupSystem();

    connect(m_runner, &ProcessRunner::outputReceived,
            this, [this](const QString &text) {
                // 体检/启动项/端口/大文件/磁盘/硬件脚本只输出一行 JSON 标记，不把原始数据刷进控制台
                if (m_task == Task::HealthCheck || m_task == Task::StartupList
                    || m_task == Task::PortsList || m_task == Task::LargeFiles
                    || m_task == Task::DuplicateScan || m_task == Task::DuplicateDelete
                    || m_task == Task::DiskHealth || m_task == Task::HwInfo
                    || m_task == Task::Battery
                    || m_task == Task::ProcessList || m_task == Task::DiskUsage
                    || m_task == Task::UninstallList || m_task == Task::UninstallAction
                    || m_task == Task::DriverList
                    || m_task == Task::HostsRead || m_task == Task::HostsWrite
                    || m_task == Task::DnsRead || m_task == Task::DnsWrite)
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
    ui->navOverviewBtn->setIcon(navIcon(UiIcons::Icon::Health));
    ui->navNetworkBtn->setIcon(navIcon(UiIcons::Icon::Network));
    ui->navSystemBtn->setIcon(navIcon(UiIcons::Icon::Monitor));
    ui->navHardwareBtn->setIcon(navIcon(UiIcons::Icon::DiskPulse));
    ui->navManageBtn->setIcon(navIcon(UiIcons::Icon::Rocket));
    ui->navToolsBtn->setIcon(navIcon(UiIcons::Icon::Grid));
    for (QPushButton *b : {ui->navOverviewBtn, ui->navNetworkBtn, ui->navSystemBtn,
                           ui->navHardwareBtn, ui->navManageBtn, ui->navToolsBtn}) {
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
    // 规划中功能的磁贴用灰色图标，与可用功能区分
    auto grayIcon = [](UiIcons::Icon icon) {
        return UiIcons::make(icon, QColor("#AEB8C8"), 30);
    };

    // 概览页快捷入口
    ui->btn_quickHealth->setIcon(tileIcon(UiIcons::Icon::Health));
    ui->btn_quickNetwork->setIcon(tileIcon(UiIcons::Icon::Network));
    ui->btn_quickPorts->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_quickDisk->setIcon(tileIcon(UiIcons::Icon::DiskPulse));

    // 网络
    ui->btn_autoCheck->setIcon(tileIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_showNetWork->setIcon(tileIcon(UiIcons::Icon::Info));
    ui->btn_netWorkTest->setIcon(tileIcon(UiIcons::Icon::Activity));
    ui->btn_ports->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_hosts->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_dns->setIcon(tileIcon(UiIcons::Icon::Network));

    // 系统维护
    ui->btn_cleaner->setIcon(tileIcon(UiIcons::Icon::Clean));
    ui->btn_fixWhite->setIcon(tileIcon(UiIcons::Icon::Picture));
    ui->btn_healthCheck->setIcon(tileIcon(UiIcons::Icon::Health));
    ui->btn_sfc->setIcon(grayIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_wupdate->setIcon(grayIcon(UiIcons::Icon::Refresh));
    ui->btn_bsod->setIcon(grayIcon(UiIcons::Icon::Info));

    // 硬件监控
    ui->btn_diskHealth->setIcon(tileIcon(UiIcons::Icon::DiskPulse));
    ui->btn_hwinfo->setIcon(tileIcon(UiIcons::Icon::Monitor));
    ui->btn_battery->setIcon(tileIcon(UiIcons::Icon::Activity));
    ui->btn_monitor->setIcon(tileIcon(UiIcons::Icon::Activity));

    // 系统管理
    ui->btn_startup->setIcon(tileIcon(UiIcons::Icon::Rocket));
    ui->btn_process->setIcon(tileIcon(UiIcons::Icon::Dots));
    ui->btn_uninstall->setIcon(tileIcon(UiIcons::Icon::Grid));
    ui->btn_drivers->setIcon(tileIcon(UiIcons::Icon::Keyboard));

    // 实用工具
    ui->btn_largeFiles->setIcon(tileIcon(UiIcons::Icon::Disk));
    ui->btn_treedisk->setIcon(tileIcon(UiIcons::Icon::Picture));
    ui->btn_dup->setIcon(tileIcon(UiIcons::Icon::Grid));
    ui->btn_hash->setIcon(grayIcon(UiIcons::Icon::Link));

    // 磁贴图标尺寸（QToolButton 不会自动跟随 QSS 的 icon-size）
    for (QToolButton *b : {ui->btn_quickHealth, ui->btn_quickNetwork,
                            ui->btn_quickPorts, ui->btn_quickDisk,
                            ui->btn_autoCheck, ui->btn_showNetWork,
                            ui->btn_netWorkTest, ui->btn_ports,
                            ui->btn_hosts, ui->btn_dns,
                            ui->btn_cleaner, ui->btn_fixWhite,
                            ui->btn_healthCheck, ui->btn_sfc,
                            ui->btn_wupdate, ui->btn_bsod,
                            ui->btn_diskHealth, ui->btn_hwinfo,
                            ui->btn_battery, ui->btn_monitor,
                            ui->btn_startup, ui->btn_process,
                            ui->btn_uninstall, ui->btn_drivers,
                            ui->btn_largeFiles, ui->btn_treedisk,
                            ui->btn_dup, ui->btn_hash}) {
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
    const struct {
        QPushButton *button;
        int page;
    } entries[] = {
        {ui->navOverviewBtn, 0},
        {ui->navNetworkBtn, 1},
        {ui->navSystemBtn, 2},
        {ui->navHardwareBtn, 3},
        {ui->navManageBtn, 4},
        {ui->navToolsBtn, 5},
    };

    for (const auto &entry : entries) {
        connect(entry.button, &QAbstractButton::clicked, this,
                [this, page = entry.page]() { showPage(page); });
    }

    // 默认停在概览页
    showPage(0);
}

void MainWindow::showPage(int index)
{
    ui->stackedWidget->setCurrentIndex(index);

    QPushButton *buttons[] = {
        ui->navOverviewBtn, ui->navNetworkBtn, ui->navSystemBtn,
        ui->navHardwareBtn, ui->navManageBtn, ui->navToolsBtn,
    };
    const int count = int(sizeof(buttons) / sizeof(buttons[0]));
    if (index >= 0 && index < count)
        buttons[index]->setChecked(true);
}

// ---------------------------------------------------------------- 概览

void MainWindow::setupOverview()
{
    // 一键体检：跳到系统维护页再执行，结果卡片就在该页
    connect(ui->btn_quickHealth, &QAbstractButton::clicked, this, [this]() {
        showPage(2);
        runHealthCheck();
    });

    // 网络诊断：网络页集中了全部网络功能
    connect(ui->btn_quickNetwork, &QAbstractButton::clicked, this, [this]() {
        showPage(1);
    });

    // 端口占用：切到网络页后立即查询
    connect(ui->btn_quickPorts, &QAbstractButton::clicked, this, [this]() {
        showPage(1);
        runPortsList();
    });

    // 磁盘健康：切到硬件监控页后立即检测
    connect(ui->btn_quickDisk, &QAbstractButton::clicked, this, [this]() {
        showPage(3);
        runDiskHealth();
    });
}

// ---------------------------------------------------------------- 规划中功能

void MainWindow::wirePlaceholderTile(QToolButton *tile, const QString &title,
                                     const QString &detail)
{
    tile->setProperty("placeholder", true);

    connect(tile, &QAbstractButton::clicked, this, [this, title, detail]() {
        showMessage("功能开发中",
                    title + "\n\n" + detail
                        + "\n\n该功能仍在规划中，上线后会在这里提供入口。");
    });
}

void MainWindow::setupPlaceholders()
{
    wirePlaceholderTile(ui->btn_sfc,
                        "系统文件修复（SFC / DISM）",
                        "将调用系统自带的 SFC 与 DISM 修复受损的系统组件，"
                        "用于解决系统文件缺失、开始菜单异常等问题。");
    wirePlaceholderTile(ui->btn_wupdate,
                        "Windows 更新修复",
                        "将重置更新组件与缓存，用于解决更新失败、"
                        "一直停在检查更新、错误代码 0x80070002 等情况。");
    wirePlaceholderTile(ui->btn_bsod,
                        "蓝屏分析",
                        "将读取最近一次崩溃的转储文件，定位可能引发蓝屏的"
                        "驱动或硬件，并给出处理建议。");

    wirePlaceholderTile(ui->btn_hash,
                        "文件哈希",
                        "将计算文件的 MD5 / SHA1 / SHA256，"
                        "用于校验下载文件是否完整、是否被篡改。");
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

    // 端口占用查询（只读）
    connect(ui->btn_ports, &QAbstractButton::clicked,
            this, &MainWindow::runPortsList);

    // Hosts 查看与编辑
    connect(ui->btn_hosts, &QAbstractButton::clicked,
            this, &MainWindow::runHostsRead);
    connect(ui->hostsSaveBtn, &QAbstractButton::clicked,
            this, &MainWindow::saveHosts);
    connect(ui->hostsRestoreBtn, &QAbstractButton::clicked,
            this, &MainWindow::restoreHosts);

    // DNS 切换
    connect(ui->btn_dns, &QAbstractButton::clicked,
            this, &MainWindow::runDnsRead);

    const QList<QPair<QPushButton *, QPair<QString, QString>>> dnsPresets = {
        {ui->dnsAutoBtn,    {QString(),                        QStringLiteral("自动获取")}},
        {ui->dnsAliBtn,     {QStringLiteral("223.5.5.5,223.6.6.6"),
                             QStringLiteral("阿里 DNS")}},
        {ui->dnsTencentBtn, {QStringLiteral("119.29.29.29,182.254.116.116"),
                             QStringLiteral("腾讯 DNS")}},
        {ui->dns114Btn,     {QStringLiteral("114.114.114.114,114.114.115.115"),
                             QStringLiteral("114 DNS")}},
        {ui->dnsGoogleBtn,  {QStringLiteral("8.8.8.8,8.8.4.4"),
                             QStringLiteral("Google DNS")}},
        {ui->dnsBaiduBtn,   {QStringLiteral("180.76.76.76"),
                             QStringLiteral("百度 DNS")}}
    };
    for (const auto &preset : dnsPresets) {
        const QString servers = preset.second.first;
        const QString label = preset.second.second;
        connect(preset.first, &QAbstractButton::clicked, this,
                [this, servers, label] { applyDns(servers, label); });
    }
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
    case Task::StartupList:
        handleStartupList();
        break;
    case Task::PortsList:
        handlePortsList();
        break;
    case Task::LargeFiles:
        handleLargeFileScan();
        break;
    case Task::DuplicateScan:
        handleDuplicateScan();
        break;
    case Task::DuplicateDelete:
        handleDuplicateDelete();
        break;
    case Task::DiskHealth:
        handleDiskHealth();
        break;
    case Task::HwInfo:
        handleHwInfo();
        break;
    case Task::Battery:
        handleBattery();
        break;
    case Task::ProcessList:
        handleProcessList();
        break;
    case Task::DiskUsage:
        handleDiskUsage();
        break;
    case Task::UninstallList:
        handleUninstallList();
        break;
    case Task::UninstallAction:
        handleUninstallAction();
        break;
    case Task::DriverList:
        handleDriverList();
        break;
    case Task::HostsRead:
        handleHostsRead();
        break;
    case Task::HostsWrite:
        handleHostsWrite();
        break;
    case Task::DnsRead:
        handleDnsRead();
        break;
    case Task::DnsWrite:
        handleDnsWrite();
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

    // 开机启动项（只读）
    connect(ui->btn_startup, &QAbstractButton::clicked,
            this, &MainWindow::runStartupList);

    // 大文件扫描（只读）
    connect(ui->btn_largeFiles, &QAbstractButton::clicked,
            this, &MainWindow::runLargeFileScan);

    // 重复文件扫描与清理
    connect(ui->btn_dup, &QAbstractButton::clicked,
            this, &MainWindow::runDuplicateScan);
    connect(ui->dupDeleteBtn, &QAbstractButton::clicked,
            this, &MainWindow::runDuplicateDelete);

    // 磁盘健康检测（只读）
    connect(ui->btn_diskHealth, &QAbstractButton::clicked,
            this, &MainWindow::runDiskHealth);

    // 硬件信息快照（只读）
    connect(ui->btn_hwinfo, &QAbstractButton::clicked,
            this, &MainWindow::runHwInfo);

    // 电池健康（只读）
    connect(ui->btn_battery, &QAbstractButton::clicked,
            this, &MainWindow::runBattery);

    // 实时监控：1 秒采样一次，直接走 Win32 API
    m_monitor = new SysMonitor(this);
    connect(ui->btn_monitor, &QAbstractButton::clicked,
            this, &MainWindow::runMonitor);
    connect(ui->monitorStopBtn, &QAbstractButton::clicked,
            this, &MainWindow::stopMonitor);
    connect(m_monitor, &SysMonitor::updated, this,
            [this](double cpu, double memUsedMb, double memTotalMb,
                   double diskPercent, const QString &diskName,
                   double netIn, double netOut) {
                ui->monCpuBar->setValue(cpu < 0 ? 0 : qRound(cpu));
                ui->monCpuVal->setText(cpu < 0
                                           ? QStringLiteral("计算中…")
                                           : QStringLiteral("%1%").arg(cpu, 0, 'f', 1));

                const double memPercent = memTotalMb > 0 ? memUsedMb * 100.0 / memTotalMb
                                                         : 0.0;
                ui->monMemBar->setValue(static_cast<int>(qRound(memPercent)));
                ui->monMemVal->setText(QStringLiteral("%1 / %2 GB（%3%）")
                                           .arg(memUsedMb / 1024.0, 0, 'f', 1)
                                           .arg(memTotalMb / 1024.0, 0, 'f', 1)
                                           .arg(memPercent, 0, 'f', 0));

                ui->monDiskName->setText(diskName);
                ui->monDiskBar->setValue(static_cast<int>(qRound(diskPercent)));
                ui->monDiskVal->setText(QStringLiteral("已用 %1%")
                                            .arg(diskPercent, 0, 'f', 1));

                ui->monNetVal->setText(QStringLiteral("↓ %1/s　↑ %2/s")
                                           .arg(formatRate(netIn), formatRate(netOut)));
            });

    // 进程列表（只读）
    connect(ui->btn_process, &QAbstractButton::clicked,
            this, &MainWindow::runProcessList);

    // 目录占用分析（只读）
    connect(ui->btn_treedisk, &QAbstractButton::clicked,
            this, &MainWindow::runDiskUsage);

    // 已安装程序清单（只读）
    connect(ui->btn_uninstall, &QAbstractButton::clicked,
            this, &MainWindow::runUninstallList);

    // 驱动清单（只读）
    connect(ui->btn_drivers, &QAbstractButton::clicked,
            this, &MainWindow::runDriverList);
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
    showSystemResultCard(ui->cardHealth);

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

// ---------------------------------------------------------------- 开机启动项

void MainWindow::runStartupList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/startup.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少启动项脚本",
                    "未找到 ps1\\startup.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取开机启动项，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->startupRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->startupSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardStartup);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::StartupList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleStartupList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("STARTUP_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError) {
        appendLog("-------------------");
        appendLog("读取启动项失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->startupSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取启动项失败",
                    "未能从 PowerShell 获取有效的启动项列表。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    // 只有一项时 PowerShell 可能输出单个对象而不是数组
    QJsonArray items;
    if (document.isArray())
        items = document.array();
    else if (document.isObject())
        items.append(document.object());

    int total = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString command = object.value(QStringLiteral("command")).toString();
        if (name.isEmpty() && command.isEmpty())
            continue;

        addStartupRow(startupSourceLabel(object.value(QStringLiteral("source")).toString()),
                      name, command);
        ++total;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现开机启动项");
    else
        summary = QStringLiteral("共 %1 项开机启动 · 仅展示，暂不支持禁用").arg(total);

    ui->startupSummary->setText(summary);
    ui->startupScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addStartupRow(const QString &source, const QString &name,
                               const QString &command)
{
    QWidget *row = new QWidget(ui->startupRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(source, row);
    badge->setObjectName(QStringLiteral("startupSource"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(72);

    QLabel *title = new QLabel(name.isEmpty() ? QStringLiteral("(未命名)") : name, row);
    title->setObjectName(QStringLiteral("startupName"));

    QLabel *desc = new QLabel(command, row);
    desc->setObjectName(QStringLiteral("startupCommand"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->startupRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 端口占用

void MainWindow::runPortsList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/ports.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少端口脚本",
                    "未找到 ps1\\ports.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取端口占用，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->portsRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->portsSummary->setText(QStringLiteral("正在读取……"));
    ui->cardPorts->setVisible(true);

    // 网络页是可滚动的，结果显示后把卡片滚入视野
    QTimer::singleShot(0, this, [this] {
        ui->pageNetwork->ensureWidgetVisible(ui->cardPorts);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::PortsList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handlePortsList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("PORTS_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError) {
        appendLog("-------------------");
        appendLog("读取端口失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->portsSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取端口失败",
                    "未能从 PowerShell 获取有效的端口列表。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    // 只有一项时 PowerShell 可能输出单个对象而不是数组
    QJsonArray items;
    if (document.isArray())
        items = document.array();
    else if (document.isObject())
        items.append(document.object());

    int total = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const int port = object.value(QStringLiteral("port")).toInt();
        if (port <= 0)
            continue;

        addPortRow(object.value(QStringLiteral("protocol")).toString(),
                   object.value(QStringLiteral("address")).toString(),
                   QString::number(port),
                   QString::number(object.value(QStringLiteral("pid")).toInt()),
                   object.value(QStringLiteral("process")).toString(),
                   object.value(QStringLiteral("state")).toString());
        ++total;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现监听端口");
    else
        summary = QStringLiteral("共 %1 个监听端口 · 仅展示，暂不支持结束进程").arg(total);

    ui->portsSummary->setText(summary);
    ui->portsScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addPortRow(const QString &protocol, const QString &address,
                            const QString &port, const QString &pid,
                            const QString &process, const QString &state)
{
    QWidget *row = new QWidget(ui->portsRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(protocol.isEmpty() ? QStringLiteral("—") : protocol, row);
    badge->setObjectName(QStringLiteral("portBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(52);

    QString titleText = QStringLiteral("端口 %1").arg(port);
    if (!process.isEmpty())
        titleText += QStringLiteral(" · %1").arg(process);

    QLabel *title = new QLabel(titleText, row);
    title->setObjectName(QStringLiteral("portName"));

    QStringList detail;
    if (!address.isEmpty())
        detail << address;
    detail << QStringLiteral("PID %1").arg(pid);
    if (!state.isEmpty())
        detail << state;

    QLabel *desc = new QLabel(detail.join(QStringLiteral(" · ")), row);
    desc->setObjectName(QStringLiteral("portDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->portsRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 大文件扫描

void MainWindow::showSystemResultCard(QWidget *card)
{
    ui->cardHealth->setVisible(card == ui->cardHealth);
    ui->cardStartup->setVisible(card == ui->cardStartup);
    ui->cardLargeFiles->setVisible(card == ui->cardLargeFiles);
    ui->cardDiskHealth->setVisible(card == ui->cardDiskHealth);
    ui->cardHwinfo->setVisible(card == ui->cardHwinfo);
    ui->cardProcesses->setVisible(card == ui->cardProcesses);
    ui->cardDiskUsage->setVisible(card == ui->cardDiskUsage);
    ui->cardUninstall->setVisible(card == ui->cardUninstall);
    ui->cardDrivers->setVisible(card == ui->cardDrivers);
    ui->cardBattery->setVisible(card == ui->cardBattery);
    ui->cardDup->setVisible(card == ui->cardDup);

    // 结果卡片分散在不同功能页，取卡片所在页面并滚入视野
    QScrollArea *page = ui->pageSystem;
    if (card == ui->cardStartup || card == ui->cardProcesses
        || card == ui->cardUninstall || card == ui->cardDrivers)
        page = ui->pageManage;
    else if (card == ui->cardLargeFiles || card == ui->cardDiskUsage
             || card == ui->cardDup)
        page = ui->pageTools;
    else if (card == ui->cardDiskHealth || card == ui->cardHwinfo
             || card == ui->cardBattery)
        page = ui->pageHardware;

    QTimer::singleShot(0, this, [page, card] {
        page->ensureWidgetVisible(card);
    });
}

void MainWindow::runLargeFileScan()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/largefiles.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少扫描脚本",
                    "未找到 ps1\\largefiles.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    // 让用户选择扫描范围；取消则不执行
    const QString root = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要扫描的目录"), QDir::homePath());
    if (root.isEmpty())
        return;

    ++m_flow;

    clearLog();
    appendLog(QStringLiteral("正在扫描 %1，目录越大耗时越久，请稍候……")
                  .arg(QDir::toNativeSeparators(root)));

    // 清空上一次的结果
    while (QLayoutItem *item = ui->largeFilesRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->largeFilesSummary->setText(QStringLiteral("正在扫描……"));
    showSystemResultCard(ui->cardLargeFiles);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Root"), QDir::toNativeSeparators(root)
    };

    m_task = Task::LargeFiles;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleLargeFileScan()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("LARGEFILES_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError) {
        appendLog("-------------------");
        appendLog("扫描大文件失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->largeFilesSummary->setText(QStringLiteral("扫描失败"));
        showMessage("扫描大文件失败",
                    "未能从 PowerShell 获取有效的扫描结果。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或更换目录后重试。",
                    QMessageBox::Warning);
        return;
    }

    // 只有一项时 PowerShell 可能输出单个对象而不是数组
    QJsonArray items;
    if (document.isArray())
        items = document.array();
    else if (document.isObject())
        items.append(document.object());

    int total = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString path = object.value(QStringLiteral("path")).toString();
        if (name.isEmpty() && path.isEmpty())
            continue;

        const qint64 size = static_cast<qint64>(
            object.value(QStringLiteral("size")).toDouble());
        addLargeFileRow(formatFileSize(size), name, path);
        ++total;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现大文件");
    else
        summary = QStringLiteral("共 %1 个大文件（体积 Top 50） · 仅展示，暂不支持删除").arg(total);

    ui->largeFilesSummary->setText(summary);
    ui->largeFilesScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addLargeFileRow(const QString &size, const QString &name,
                                 const QString &path)
{
    QWidget *row = new QWidget(ui->largeFilesRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(size, row);
    badge->setObjectName(QStringLiteral("largeFileSize"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(72);

    QLabel *title = new QLabel(name.isEmpty() ? QStringLiteral("(未命名)") : name, row);
    title->setObjectName(QStringLiteral("largeFileName"));

    QLabel *desc = new QLabel(path, row);
    desc->setObjectName(QStringLiteral("largeFilePath"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->largeFilesRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 重复文件

void MainWindow::runDuplicateScan()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/duplicates.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少扫描脚本",
                    "未找到 ps1\\duplicates.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    const QString root = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要扫描的目录"), QDir::homePath());
    if (root.isEmpty())
        return;

    runDuplicateScanFor(root);
}

void MainWindow::runDuplicateScanFor(const QString &root)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/duplicates.ps1"));

    ++m_flow;

    clearLog();
    appendLog(QStringLiteral("正在扫描 %1 中的重复文件，目录越大耗时越久，请稍候……")
                  .arg(QDir::toNativeSeparators(root)));

    while (QLayoutItem *item = ui->dupRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    // 末尾留一个弹簧，避免只有少量分组时行被拉伸得过高
    ui->dupRowsLayout->addStretch(1);

    m_dupRoot = root;
    m_dupShown = 0;
    m_dupFiles = 0;
    m_dupBytes = 0;

    ui->dupDeleteBtn->setEnabled(false);
    ui->dupSummary->setText(QStringLiteral("正在扫描……"));
    showSystemResultCard(ui->cardDup);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Root"), QDir::toNativeSeparators(root),
        QStringLiteral("-Top"), QStringLiteral("20")
    };

    m_task = Task::DuplicateScan;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDuplicateScan()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DUPLICATES_JSON:");
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
        appendLog("扫描重复文件失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->dupSummary->setText(QStringLiteral("扫描失败"));
        ui->dupDeleteBtn->setEnabled(false);
        showMessage("扫描重复文件失败",
                    "未能从 PowerShell 获取有效的扫描结果。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或更换目录后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();

    if (data.value(QStringLiteral("error")).toString()
        == QLatin1String("no-root")) {
        ui->dupSummary->setText(QStringLiteral("目录不存在或无法访问"));
        ui->dupDeleteBtn->setEnabled(false);
        showMessage("无法扫描该目录",
                    "所选目录不存在或无法访问，请重新选择。",
                    QMessageBox::Warning);
        return;
    }

    const QString root = data.value(QStringLiteral("root")).toString();
    if (!root.isEmpty())
        m_dupRoot = root;

    const int scanned = data.value(QStringLiteral("scanned")).toInt();
    const int groups = data.value(QStringLiteral("groups")).toInt();
    const int shown = data.value(QStringLiteral("shown")).toInt();
    const int dupFiles = data.value(QStringLiteral("dupFiles")).toInt();
    const qint64 wasted = static_cast<qint64>(
        data.value(QStringLiteral("wasted")).toDouble());

    m_dupShown = shown;
    m_dupFiles = dupFiles;
    m_dupBytes = wasted;

    const QJsonArray items = data.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();

        const QString name = object.value(QStringLiteral("name")).toString();
        const int count = object.value(QStringLiteral("count")).toInt();
        const qint64 groupWasted = static_cast<qint64>(
            object.value(QStringLiteral("wasted")).toDouble());

        QStringList paths;
        for (const QJsonValue &pathValue : object.value(QStringLiteral("paths")).toArray()) {
            const QString path = pathValue.toString();
            if (!path.isEmpty())
                paths.append(QDir::toNativeSeparators(path));
        }

        const QString detail = QStringLiteral("共 %1 份 · 可释放 %2\n%3")
                                   .arg(count)
                                   .arg(formatFileSize(groupWasted))
                                   .arg(paths.join(QStringLiteral("\n")));
        addDupRow(formatFileSize(groupWasted), name, detail);
    }

    QString summary;
    if (groups == 0) {
        summary = QStringLiteral("未发现重复文件（已扫描 %1 个不小于 1 KB 的文件）")
                      .arg(scanned);
    } else {
        summary = QStringLiteral("扫描 %1 个文件 · 共 %2 组重复 · %3 个多余副本 · "
                                 "可释放 %4")
                      .arg(scanned)
                      .arg(groups)
                      .arg(dupFiles)
                      .arg(formatFileSize(wasted));
        if (groups > shown)
            summary += QStringLiteral("（仅列出占用最大的 %1 组）").arg(shown);
        summary += QStringLiteral(" · 删除时每组保留创建最早的一份");
    }

    ui->dupSummary->setText(summary);
    ui->dupDeleteBtn->setEnabled(shown > 0);
    ui->dupScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::runDuplicateDelete()
{
    if (m_dupRoot.isEmpty() || m_dupShown <= 0)
        return;

    const QString confirmText =
        QStringLiteral("将删除 %1 中前 %2 组重复文件的额外副本（每组保留创建时间最早的一份），"
                       "共 %3 个副本，可释放约 %4。\n\n"
                       "删除后文件无法恢复，请确认。")
            .arg(QDir::toNativeSeparators(m_dupRoot))
            .arg(m_dupShown)
            .arg(m_dupFiles)
            .arg(formatFileSize(m_dupBytes));

    if (!confirmAction(QStringLiteral("确认删除重复副本"), confirmText,
                       QMessageBox::Warning))
        return;

    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/duplicates.ps1"));

    ++m_flow;

    clearLog();
    appendLog(QStringLiteral("正在删除重复副本，请稍候……"));

    ui->dupDeleteBtn->setEnabled(false);
    ui->dupSummary->setText(QStringLiteral("正在删除重复副本……"));

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("delete"),
        QStringLiteral("-Root"), QDir::toNativeSeparators(m_dupRoot),
        QStringLiteral("-Top"), QString::number(m_dupShown)
    };

    m_task = Task::DuplicateDelete;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDuplicateDelete()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DUPLICATES_ACTION_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    int deleted = 0;
    int failed = 0;
    qint64 freed = 0;

    if (!payload.isEmpty() && parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        const QJsonObject data = document.object();
        deleted = data.value(QStringLiteral("deleted")).toInt();
        failed = data.value(QStringLiteral("failed")).toInt();
        freed = static_cast<qint64>(data.value(QStringLiteral("freed")).toDouble());
    } else {
        appendLog("-------------------");
        appendLog("删除重复副本失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->dupSummary->setText(QStringLiteral("删除失败，正在重新扫描……"));
        showMessage("删除重复副本失败",
                    "未能从 PowerShell 获取有效的删除结果，正在重新扫描当前目录。",
                    QMessageBox::Warning);
        const QString root = m_dupRoot;
        QTimer::singleShot(0, this, [this, root] { runDuplicateScanFor(root); });
        return;
    }

    QString message = QStringLiteral("已删除 %1 个重复副本，释放约 %2。")
                          .arg(deleted)
                          .arg(formatFileSize(freed));
    if (failed > 0)
        message += QStringLiteral("\n\n有 %1 个文件无法删除（可能被占用或没有权限）。")
                       .arg(failed);

    appendLog("-------------------");
    appendLog(message);
    ui->dupSummary->setText(message.section(QLatin1Char('\n'), 0, 0));
    showMessage(failed > 0 ? QStringLiteral("部分文件未能删除")
                           : QStringLiteral("删除完成"),
                message,
                failed > 0 ? QMessageBox::Warning : QMessageBox::Information);

    // 重新扫描同一目录，让清单反映最新状态。
    // 必须延后到下一轮事件循环：本函数由 onTaskFinished 调用，它返回后会把
    // m_task 重置为 None，若此时新扫描已在跑，输出就会被当成普通日志刷进
    // 控制台，且扫描结束时也不会有对应的处理函数。
    const QString root = m_dupRoot;
    QTimer::singleShot(0, this, [this, root] { runDuplicateScanFor(root); });
}

void MainWindow::addDupRow(const QString &badge, const QString &name,
                           const QString &detail)
{
    QWidget *row = new QWidget(ui->dupRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badgeLabel = new QLabel(badge, row);
    badgeLabel->setObjectName(QStringLiteral("dupBadge"));
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setFixedWidth(64);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("dupName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("dupDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badgeLabel, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    const int insertAt = qMax(0, ui->dupRowsLayout->count() - 1);
    ui->dupRowsLayout->insertWidget(insertAt, row);
}

// ---------------------------------------------------------------- 磁盘健康

void MainWindow::runDiskHealth()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/diskhealth.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少磁盘脚本",
                    "未找到 ps1\\diskhealth.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取磁盘健康信息，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->diskHealthRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->diskHealthSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardDiskHealth);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::DiskHealth;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDiskHealth()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DISKS_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError) {
        appendLog("-------------------");
        appendLog("读取磁盘健康失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->diskHealthSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取磁盘健康失败",
                    "未能从 PowerShell 获取有效的磁盘信息。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    // 只有一块磁盘时 PowerShell 可能输出单个对象而不是数组
    QJsonArray items;
    if (document.isArray())
        items = document.array();
    else if (document.isObject())
        items.append(document.object());

    int total = 0;
    int bad = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString model = object.value(QStringLiteral("model")).toString();
        if (model.isEmpty())
            continue;

        QString state;
        const QString health = diskHealthLabel(
            object.value(QStringLiteral("health")).toString(), &state);
        if (state != QLatin1String("ok"))
            ++bad;

        QStringList parts;
        parts << diskMediaLabel(object.value(QStringLiteral("media")).toString());
        const QString bus = object.value(QStringLiteral("bus")).toString();
        if (!bus.isEmpty())
            parts << bus;
        parts << formatFileSize(static_cast<qint64>(
            object.value(QStringLiteral("size")).toDouble()));

        // 可靠性计数器需要管理员权限，普通权限下为 null
        const QJsonValue temp = object.value(QStringLiteral("temperature"));
        const QJsonValue wear = object.value(QStringLiteral("wear"));
        const QJsonValue hours = object.value(QStringLiteral("hours"));
        if (!temp.isNull() && !temp.isUndefined())
            parts << QStringLiteral("温度 %1℃").arg(temp.toInt());
        if (!wear.isNull() && !wear.isUndefined())
            parts << QStringLiteral("磨损 %1%").arg(wear.toInt());
        if (!hours.isNull() && !hours.isUndefined())
            parts << QStringLiteral("通电 %1 小时").arg(
                static_cast<qint64>(hours.toDouble()));

        QString detail = parts.join(QStringLiteral(" · "));
        if (temp.isNull() && wear.isNull() && hours.isNull())
            detail += QStringLiteral(" · 温度/磨损等详细数据需管理员权限");

        addDiskRow(health, model, detail);
        ++total;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现物理磁盘");
    else if (bad > 0)
        summary = QStringLiteral("共 %1 块磁盘 · %2 块存在异常，建议尽快备份重要数据")
                      .arg(total).arg(bad);
    else
        summary = QStringLiteral("共 %1 块磁盘 · 状态正常（仅展示，不做修复）").arg(total);

    ui->diskHealthSummary->setText(summary);
    ui->diskHealthScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addDiskRow(const QString &health, const QString &model,
                            const QString &detail)
{
    QWidget *row = new QWidget(ui->diskHealthRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(health, row);
    badge->setObjectName(QStringLiteral("diskBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(48);
    // 供 style.qss 按状态配色（ok / warn / bad）
    badge->setProperty("state",
                       health == QStringLiteral("健康") ? QStringLiteral("ok")
                       : health == QStringLiteral("异常") ? QStringLiteral("bad")
                                                          : QStringLiteral("warn"));

    QLabel *title = new QLabel(model, row);
    title->setObjectName(QStringLiteral("diskName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("diskDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->diskHealthRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 硬件信息

void MainWindow::runHwInfo()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/hwinfo.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少硬件脚本",
                    "未找到 ps1\\hwinfo.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取硬件信息，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->hwinfoRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->hwinfoSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardHwinfo);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::HwInfo;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleHwInfo()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("HWINFO_JSON:");
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
        appendLog("读取硬件信息失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->hwinfoSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取硬件信息失败",
                    "未能从 PowerShell 获取有效的硬件信息。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();

    // PowerShell 序列化单个元素时可能输出对象而不是数组，统一按数组处理
    auto toObjects = [](const QJsonValue &value) {
        QJsonArray array;
        if (value.isArray())
            array = value.toArray();
        else if (value.isObject())
            array.append(value.toObject());
        return array;
    };

    int count = 0;

    const QJsonObject device = data.value(QStringLiteral("device")).toObject();
    if (!device.isEmpty()) {
        const QString model = device.value(QStringLiteral("model")).toString();
        const QString name = device.value(QStringLiteral("name")).toString();
        QString title = model.isEmpty() ? name : model;
        if (title.isEmpty())
            title = QStringLiteral("未知设备");

        QStringList parts;
        const QString maker = device.value(QStringLiteral("manufacturer")).toString();
        if (!maker.isEmpty() && maker != title)
            parts << maker;
        if (!name.isEmpty() && name != title)
            parts << name;

        addHwRow("设备", title, parts.join(QStringLiteral(" · ")));
        ++count;
    }

    for (const QJsonValue &value : toObjects(data.value(QStringLiteral("cpu")))) {
        const QJsonObject cpu = value.toObject();
        const QString name = cpu.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        QStringList parts;
        const int cores = cpu.value(QStringLiteral("cores")).toInt();
        const int threads = cpu.value(QStringLiteral("threads")).toInt();
        const int clock = cpu.value(QStringLiteral("clock")).toInt();
        if (cores > 0)
            parts << QStringLiteral("%1 核").arg(cores);
        if (threads > 0)
            parts << QStringLiteral("%1 线程").arg(threads);
        if (clock > 0)
            parts << QStringLiteral("%1 GHz").arg(
                QString::number(clock / 1000.0, 'f', 2));
        const QString socket = cpu.value(QStringLiteral("socket")).toString();
        if (!socket.isEmpty())
            parts << socket;

        addHwRow("处理器", name, parts.join(QStringLiteral(" · ")));
        ++count;
    }

    const QJsonObject board = data.value(QStringLiteral("board")).toObject();
    if (!board.isEmpty()) {
        const QString manufacturer = board.value(QStringLiteral("manufacturer")).toString();
        const QString product = board.value(QStringLiteral("product")).toString();
        QString title = product.isEmpty() ? manufacturer : product;
        if (title.isEmpty())
            title = QStringLiteral("未知主板");

        QStringList parts;
        if (!manufacturer.isEmpty() && manufacturer != title)
            parts << manufacturer;

        const QString biosVendor = board.value(QStringLiteral("bios_vendor")).toString();
        const QString biosVersion = board.value(QStringLiteral("bios_version")).toString();
        const QString biosDate = board.value(QStringLiteral("bios_date")).toString();
        QString bios = QStringList{biosVendor, biosVersion}.join(QLatin1Char(' ')).trimmed();
        if (!biosDate.isEmpty())
            bios = bios.isEmpty() ? biosDate
                                  : QStringLiteral("%1 (%2)").arg(bios, biosDate);
        if (!bios.isEmpty())
            parts << QStringLiteral("BIOS %1").arg(bios);

        addHwRow("主板", title, parts.join(QStringLiteral(" · ")));
        ++count;
    }

    const QJsonObject memory = data.value(QStringLiteral("memory")).toObject();
    const int memTotal = memory.value(QStringLiteral("total_gb")).toInt();
    const QJsonArray modules = toObjects(memory.value(QStringLiteral("modules")));
    for (const QJsonValue &value : modules) {
        const QJsonObject module = value.toObject();
        const int capacity = module.value(QStringLiteral("capacity_gb")).toInt();
        const int speed = module.value(QStringLiteral("speed")).toInt();

        QString title = capacity > 0
                            ? QStringLiteral("%1 GB").arg(capacity)
                            : QStringLiteral("内存模组");
        if (speed > 0)
            title += QStringLiteral(" · %1 MHz").arg(speed);

        QStringList parts;
        const QString slot = module.value(QStringLiteral("slot")).toString();
        const QString maker = module.value(QStringLiteral("manufacturer")).toString();
        const QString part = module.value(QStringLiteral("part")).toString();
        if (!slot.isEmpty())
            parts << slot;
        if (!maker.isEmpty())
            parts << maker;
        if (!part.isEmpty())
            parts << part;

        addHwRow("内存", title, parts.join(QStringLiteral(" · ")));
        ++count;
    }
    if (modules.isEmpty() && memTotal > 0) {
        addHwRow("内存", QStringLiteral("共 %1 GB").arg(memTotal),
                 QStringLiteral("未读取到内存条信息"));
        ++count;
    }

    for (const QJsonValue &value : toObjects(data.value(QStringLiteral("gpu")))) {
        const QJsonObject gpu = value.toObject();
        const QString name = gpu.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        QStringList parts;
        const int vramMb = gpu.value(QStringLiteral("vram_mb")).toInt();
        if (vramMb > 0)
            parts << (vramMb >= 1024
                          ? QStringLiteral("显存 %1 GB").arg(
                                QString::number(vramMb / 1024.0, 'f', 1))
                          : QStringLiteral("显存 %1 MB").arg(vramMb));

        const QString driverVersion = gpu.value(QStringLiteral("driver_version")).toString();
        const QString driverDate = gpu.value(QStringLiteral("driver_date")).toString();
        if (!driverVersion.isEmpty()) {
            parts << (driverDate.isEmpty()
                          ? QStringLiteral("驱动 %1").arg(driverVersion)
                          : QStringLiteral("驱动 %1 (%2)").arg(driverVersion, driverDate));
        }

        const QString resolution = gpu.value(QStringLiteral("resolution")).toString();
        if (!resolution.isEmpty())
            parts << resolution;

        addHwRow("显卡", name, parts.join(QStringLiteral(" · ")));
        ++count;
    }

    QString summary;
    if (count == 0) {
        summary = QStringLiteral("未读取到硬件信息");
    } else {
        summary = QStringLiteral("共 %1 项硬件信息").arg(count);
        if (memTotal > 0)
            summary += QStringLiteral(" · 内存共 %1 GB").arg(memTotal);
        summary += QStringLiteral("（仅读取展示，不做修改）");
    }

    ui->hwinfoSummary->setText(summary);
    ui->hwinfoScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addHwRow(const QString &category, const QString &title,
                          const QString &detail)
{
    QWidget *row = new QWidget(ui->hwinfoRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(category, row);
    badge->setObjectName(QStringLiteral("hwBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(52);

    QLabel *name = new QLabel(title, row);
    name->setObjectName(QStringLiteral("hwName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("hwDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(name);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->hwinfoRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 电池健康

void MainWindow::runBattery()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/battery.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少电池脚本",
                    "未找到 ps1\\battery.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取电池信息，请稍候……");

    ui->batterySummary->setText(QStringLiteral("正在读取电池信息……"));
    showSystemResultCard(ui->cardBattery);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::Battery;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleBattery()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("BATTERY_JSON:");
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
        appendLog("读取电池信息失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->batterySummary->setText(QStringLiteral("读取失败"));
        showMessage("读取电池信息失败",
                    "未能从 PowerShell 获取有效的电池信息。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();

    if (!data.value(QStringLiteral("present")).toBool()) {
        // 台式机常见：没有电池，只保留说明，不显示空的数据行
        ui->batteryRows->setVisible(false);
        ui->batterySummary->setText(
            QStringLiteral("未检测到电池，当前设备可能使用台式机电源供电，"
                           "因此无法评估电池损耗。"));
        appendLog("-------------------");
        appendLog("未检测到电池，跳过电池健康评估。");
        return;
    }

    ui->batteryRows->setVisible(true);

    const QString name = data.value(QStringLiteral("name")).toString();
    const qint64 design = static_cast<qint64>(
        data.value(QStringLiteral("design")).toDouble());
    const qint64 full = static_cast<qint64>(
        data.value(QStringLiteral("full")).toDouble());
    const qint64 cycles = static_cast<qint64>(
        data.value(QStringLiteral("cycles")).toDouble());
    const int charge = data.value(QStringLiteral("charge")).toInt();
    const int status = data.value(QStringLiteral("status")).toInt();
    const int runtime = data.value(QStringLiteral("runtime")).toInt();

    ui->batteryNameVal->setText(name.isEmpty() ? QStringLiteral("—") : name);
    ui->batteryDesignVal->setText(formatCapacity(design));
    ui->batteryFullVal->setText(formatCapacity(full));
    ui->batteryChargeVal->setText(charge > 0 ? QStringLiteral("%1%").arg(charge)
                                             : QStringLiteral("—"));

    QString statusText = batteryStatusLabel(status);
    if (status == 1 && runtime > 0 && runtime < 100000)
        statusText += QStringLiteral("（预计剩余 %1 小时 %2 分）")
                          .arg(runtime / 60)
                          .arg(runtime % 60);
    ui->batteryStatusVal->setText(statusText);

    ui->batteryCycleVal->setText(cycles > 0 ? QStringLiteral("%1 次").arg(cycles)
                                            : QStringLiteral("系统未提供"));

    QString summary;
    if (design > 0 && full > 0) {
        const double health = full * 100.0 / design;
        const double wear = 100.0 - health;
        summary = QStringLiteral("健康度 %1%（损耗 %2%）")
                      .arg(health, 0, 'f', 1)
                      .arg(qMax(0.0, wear), 0, 'f', 1);
        if (health < 60.0)
            summary += QStringLiteral(" · 电池损耗较大，建议考虑更换");
        else if (health < 80.0)
            summary += QStringLiteral(" · 容量已有明显衰减");
    } else {
        summary = QStringLiteral("系统未提供完整的容量数据，无法计算健康度");
    }

    ui->batterySummary->setText(summary);

    appendLog("-------------------");
    appendLog(summary);
}

// ---------------------------------------------------------------- 进程列表

void MainWindow::runProcessList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/processes.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少进程脚本",
                    "未找到 ps1\\processes.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取进程列表，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->processesRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->processesSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardProcesses);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::ProcessList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleProcessList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("PROCESSES_JSON:");
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
        appendLog("读取进程列表失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->processesSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取进程列表失败",
                    "未能从 PowerShell 获取有效的进程列表。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const int total = data.value(QStringLiteral("total")).toInt();

    // 只有一个进程时 PowerShell 可能输出对象而不是数组
    QJsonArray items;
    const QJsonValue value = data.value(QStringLiteral("items"));
    if (value.isArray())
        items = value.toArray();
    else if (value.isObject())
        items.append(value.toObject());

    int shown = 0;
    for (const QJsonValue &item : items) {
        if (!item.isObject())
            continue;
        const QJsonObject object = item.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        QStringList parts;
        parts << QStringLiteral("内存 %1").arg(formatFileSize(
            static_cast<qint64>(object.value(QStringLiteral("mem")).toDouble())));

        const QJsonValue cpu = object.value(QStringLiteral("cpu"));
        if (!cpu.isNull() && !cpu.isUndefined())
            parts << QStringLiteral("CPU %1 秒").arg(
                QString::number(cpu.toDouble(), 'f', 1));

        QString command = object.value(QStringLiteral("cmd")).toString();
        if (!command.isEmpty()) {
            // 命令行普遍很长，截断后再展示，避免单行撑爆宽度
            if (command.size() > 100)
                command = command.left(100) + QStringLiteral("…");
            parts << QStringLiteral("命令行 %1").arg(command);
        }

        addProcessRow(QString::number(object.value(QStringLiteral("pid")).toInt()),
                      name, parts.join(QStringLiteral(" · ")));
        ++shown;
    }

    QString summary;
    if (shown == 0) {
        summary = QStringLiteral("未读取到进程信息");
    } else if (total > shown) {
        summary = QStringLiteral("共 %1 个进程 · 仅列出内存占用最高的 %2 个（只读展示，不结束进程）")
                      .arg(total).arg(shown);
    } else {
        summary = QStringLiteral("共 %1 个进程 · 按内存占用排序（只读展示，不结束进程）")
                      .arg(shown);
    }

    ui->processesSummary->setText(summary);
    ui->processesScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addProcessRow(const QString &pid, const QString &name,
                               const QString &detail)
{
    QWidget *row = new QWidget(ui->processesRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(pid, row);
    badge->setObjectName(QStringLiteral("procBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(56);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("procName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("procDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->processesRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 目录占用

void MainWindow::runDiskUsage()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/diskusage.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少统计脚本",
                    "未找到 ps1\\diskusage.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    // 让用户选择要分析的目录；取消则不执行
    const QString root = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要分析的目录"), QDir::homePath());
    if (root.isEmpty())
        return;

    ++m_flow;

    clearLog();
    appendLog(QStringLiteral("正在统计 %1 的目录占用，目录越大耗时越久，请稍候……")
                  .arg(QDir::toNativeSeparators(root)));

    // 清空上一次的结果
    while (QLayoutItem *item = ui->diskUsageRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->diskUsageSummary->setText(QStringLiteral("正在统计……"));
    showSystemResultCard(ui->cardDiskUsage);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Root"), QDir::toNativeSeparators(root)
    };

    m_task = Task::DiskUsage;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDiskUsage()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DISKUSAGE_JSON:");
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
        appendLog("统计目录占用失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->diskUsageSummary->setText(QStringLiteral("统计失败"));
        showMessage("统计目录占用失败",
                    "未能从 PowerShell 获取有效的统计结果。\n\n"
                    "请确认目录仍然存在，或更换目录后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    if (data.contains(QStringLiteral("error"))) {
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->diskUsageSummary->setText(QStringLiteral("目录不存在或无法访问"));
        showMessage("无法统计该目录",
                    "所选目录不存在、已被移动，或当前账户无权读取。",
                    QMessageBox::Warning);
        return;
    }

    const QString root = data.value(QStringLiteral("root")).toString();
    const qint64 total = static_cast<qint64>(
        data.value(QStringLiteral("total")).toDouble());
    const int folders = data.value(QStringLiteral("folders")).toInt();

    // 只有一个子目录时 PowerShell 可能输出对象而不是数组
    QJsonArray items;
    const QJsonValue value = data.value(QStringLiteral("items"));
    if (value.isArray())
        items = value.toArray();
    else if (value.isObject())
        items.append(value.toObject());

    int shown = 0;
    for (const QJsonValue &item : items) {
        if (!item.isObject())
            continue;
        const QJsonObject object = item.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        const qint64 size = static_cast<qint64>(
            object.value(QStringLiteral("size")).toDouble());
        const int files = object.value(QStringLiteral("files")).toInt();

        int percent = 0;
        if (total > 0)
            percent = static_cast<int>(size * 100.0 / total + 0.5);

        addUsageRow(QStringLiteral("%1%").arg(percent), name,
                    QStringLiteral("%1 · %2 个文件")
                        .arg(formatFileSize(size)).arg(files));
        ++shown;
    }

    QString summary;
    if (shown == 0) {
        summary = QStringLiteral("%1 下没有可统计的子目录")
                      .arg(QDir::toNativeSeparators(root));
    } else {
        summary = QStringLiteral("%1 · 共 %2 · %3 个子目录")
                      .arg(QDir::toNativeSeparators(root),
                           formatFileSize(total))
                      .arg(folders);
        if (folders > shown)
            summary += QStringLiteral("（列出占用最大的 %1 个）").arg(shown);
        summary += QStringLiteral("（只读统计）");
    }

    ui->diskUsageSummary->setText(summary);
    ui->diskUsageScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addUsageRow(const QString &percent, const QString &name,
                             const QString &detail)
{
    QWidget *row = new QWidget(ui->diskUsageRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(percent, row);
    badge->setObjectName(QStringLiteral("usageBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(48);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("usageName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("usageDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->diskUsageRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- 已安装程序

void MainWindow::runUninstallList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/uninstall.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少程序清单脚本",
                    "未找到 ps1\\uninstall.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取已安装程序清单，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->uninstallRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->uninstallSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardUninstall);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::UninstallList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleUninstallList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("UNINSTALL_JSON:");
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
        appendLog("读取已安装程序失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->uninstallSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取已安装程序失败",
                    "未能从 PowerShell 获取有效的程序清单。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const int total = data.value(QStringLiteral("total")).toInt();

    // 只有一项时 PowerShell 可能输出对象而不是数组
    QJsonArray items;
    const QJsonValue value = data.value(QStringLiteral("items"));
    if (value.isArray())
        items = value.toArray();
    else if (value.isObject())
        items.append(value.toObject());

    int shown = 0;
    for (const QJsonValue &item : items) {
        if (!item.isObject())
            continue;
        const QJsonObject object = item.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        const qint64 size = static_cast<qint64>(
            object.value(QStringLiteral("size")).toDouble());
        const QString key = object.value(QStringLiteral("key")).toString();
        const bool uninstallable = object.value(QStringLiteral("uninstall")).toBool();

        QStringList parts;
        const QString version = object.value(QStringLiteral("version")).toString();
        const QString publisher = object.value(QStringLiteral("publisher")).toString();
        const QString date = object.value(QStringLiteral("date")).toString();
        if (!version.isEmpty())
            parts << QStringLiteral("版本 %1").arg(version);
        if (!publisher.isEmpty())
            parts << publisher;
        if (!date.isEmpty())
            parts << QStringLiteral("安装于 %1").arg(date);
        if (parts.isEmpty())
            parts << QStringLiteral("缺少版本与发布者信息");

        addAppRow(size > 0 ? formatFileSize(size) : QStringLiteral("—"),
                  name, parts.join(QStringLiteral(" · ")), key, uninstallable);
        ++shown;
    }

    QString summary;
    if (shown == 0) {
        summary = QStringLiteral("未读取到已安装程序");
    } else if (total > shown) {
        summary = QStringLiteral("共 %1 个已安装程序 · 列出占用最大的 %2 个 · "
                                 "可点击右侧「卸载」启动卸载程序")
                      .arg(total).arg(shown);
    } else {
        summary = QStringLiteral("共 %1 个已安装程序 · 按占用空间排序 · "
                                 "可点击右侧「卸载」启动卸载程序")
                      .arg(shown);
    }

    ui->uninstallSummary->setText(summary);
    ui->uninstallScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addAppRow(const QString &size, const QString &name,
                           const QString &detail, const QString &key,
                           bool uninstallable)
{
    QWidget *row = new QWidget(ui->uninstallRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(size, row);
    badge->setObjectName(QStringLiteral("appBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(64);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("appName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("appDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignVCenter);
    layout->addLayout(text, 1);

    // 只有注册表项里带 UninstallString 的程序才能从清单里直接卸载
    if (uninstallable && !key.isEmpty()) {
        QPushButton *uninstallButton = new QPushButton(QStringLiteral("卸载"), row);
        uninstallButton->setObjectName(QStringLiteral("appUninstallBtn"));
        uninstallButton->setMinimumSize(60, 28);
        uninstallButton->setCursor(Qt::PointingHandCursor);
        connect(uninstallButton, &QAbstractButton::clicked, this,
                [this, key, name] { runUninstall(key, name); });
        layout->addWidget(uninstallButton, 0, Qt::AlignVCenter);
    }

    ui->uninstallRowsLayout->addWidget(row);
}

void MainWindow::runUninstall(const QString &key, const QString &name)
{
    if (!confirmAction(QStringLiteral("确认卸载"),
                       QStringLiteral("将启动「%1」自带的卸载程序。\n\n"
                                      "卸载由系统或软件厂商的向导完成，过程中可能需要管理员权限；"
                                      "若弹出权限提示请选择“是”。")
                           .arg(name),
                       QMessageBox::Question))
        return;

    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/uninstall.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少卸载脚本",
                    "未找到 ps1\\uninstall.ps1，无法卸载。", QMessageBox::Critical);
        return;
    }

    ++m_flow;

    appendLog("-------------------");
    appendLog(QStringLiteral("正在启动「%1」的卸载程序，若弹出权限提示请选择“是”……")
                  .arg(name));
    ui->uninstallSummary->setText(QStringLiteral("正在卸载「%1」……").arg(name));

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("uninstall"),
        QStringLiteral("-Key"), key
    };

    m_task = Task::UninstallAction;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleUninstallAction()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("UNINSTALL_ACTION_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    QString code;
    QString name;
    QString detail;
    if (!payload.isEmpty() && parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        const QJsonObject data = document.object();
        code = data.value(QStringLiteral("code")).toString();
        name = data.value(QStringLiteral("name")).toString();
        detail = data.value(QStringLiteral("detail")).toString();
    }

    if (code == QLatin1String("removed")) {
        appendLog(QStringLiteral("「%1」的卸载程序已结束，程序项已从清单移除。").arg(name));
        showMessage("卸载完成", QStringLiteral("「%1」已卸载，正在刷新程序清单。").arg(name));
        runUninstallList();
        return;
    }

    if (code == QLatin1String("still-present")) {
        appendLog(QStringLiteral("「%1」的卸载程序已结束，但程序项仍然存在。").arg(name));
        showMessage("卸载未完成",
                    QStringLiteral("卸载程序已结束，但「%1」仍在程序清单中，"
                                   "可能已取消或未彻底卸载。\n\n正在刷新清单。")
                        .arg(name),
                    QMessageBox::Warning);
        runUninstallList();
        return;
    }

    if (code == QLatin1String("pending")) {
        appendLog(QStringLiteral("「%1」的卸载程序仍在运行。").arg(name));
        ui->uninstallSummary->setText(QStringLiteral("卸载程序仍在运行，完成后重新点击"
                                                     "「已安装程序」磁贴即可刷新清单"));
        showMessage("卸载进行中",
                    QStringLiteral("「%1」的卸载程序仍在运行。\n\n"
                                   "请在卸载窗口中完成操作，之后重新点击"
                                   "「已安装程序」磁贴刷新清单。")
                        .arg(name));
        return;
    }

    // 其余情况：提示原因，并刷新清单让用户看到最新状态
    QString message;
    if (code == QLatin1String("canceled")) {
        message = QStringLiteral("已取消提权，卸载未执行。");
    } else if (code == QLatin1String("no-entry")) {
        message = QStringLiteral("该程序项已不存在，可能已被卸载。");
    } else if (code == QLatin1String("no-command")) {
        message = QStringLiteral("该系统项没有提供卸载命令，请到「设置 → 应用」中卸载。");
    } else if (code == QLatin1String("no-result")) {
        message = QStringLiteral("提权进程没有返回结果，卸载可能未执行。");
    } else if (code == QLatin1String("error")) {
        message = QStringLiteral("卸载时出错：%1").arg(detail);
    } else {
        message = QStringLiteral("未能解析卸载结果，请刷新清单确认当前状态。");
    }

    appendLog("-------------------");
    appendLog(message);
    showMessage(code == QLatin1String("canceled")
                    ? QStringLiteral("已取消卸载")
                    : QStringLiteral("未能完成卸载"),
                message, QMessageBox::Warning);
    runUninstallList();
}

// ---------------------------------------------------------------- 驱动清单

void MainWindow::runDriverList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/drivers.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少驱动清单脚本",
                    "未找到 ps1\\drivers.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    clearLog();
    appendLog("正在读取驱动清单，请稍候……");

    // 清空上一次的结果
    while (QLayoutItem *item = ui->driversRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->driversSummary->setText(QStringLiteral("正在读取……"));
    showSystemResultCard(ui->cardDrivers);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::DriverList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDriverList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DRIVERS_JSON:");
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
        appendLog("读取驱动清单失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->driversSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取驱动清单失败",
                    "未能从 PowerShell 获取有效的驱动清单。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const int total = data.value(QStringLiteral("total")).toInt();

    // 只有一项时 PowerShell 可能输出对象而不是数组
    QJsonArray items;
    const QJsonValue value = data.value(QStringLiteral("items"));
    if (value.isArray())
        items = value.toArray();
    else if (value.isObject())
        items.append(value.toObject());

    int shown = 0;
    for (const QJsonValue &item : items) {
        if (!item.isObject())
            continue;
        const QJsonObject object = item.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        QStringList parts;
        const QString deviceClass = object.value(QStringLiteral("class")).toString();
        const QString version = object.value(QStringLiteral("version")).toString();
        const QString manufacturer = object.value(QStringLiteral("manufacturer")).toString();
        const QString date = object.value(QStringLiteral("date")).toString();
        parts << QStringLiteral("类别 %1").arg(driverClassLabel(deviceClass));
        if (!version.isEmpty())
            parts << QStringLiteral("版本 %1").arg(version);
        if (!manufacturer.isEmpty())
            parts << manufacturer;
        if (!date.isEmpty())
            parts << QStringLiteral("发布于 %1").arg(date);

        addDriverRow(name, parts.join(QStringLiteral(" · ")));
        ++shown;
    }

    QString summary;
    if (shown == 0) {
        summary = QStringLiteral("未读取到驱动信息");
    } else if (total > shown) {
        summary = QStringLiteral("共 %1 个设备驱动 · 按设备类别列出前 %2 个（只读清单）")
                      .arg(total).arg(shown);
    } else {
        summary = QStringLiteral("共 %1 个设备驱动 · 按设备类别排序（只读清单）")
                      .arg(shown);
    }

    ui->driversSummary->setText(summary);
    ui->driversScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addDriverRow(const QString &name, const QString &detail)
{
    QWidget *row = new QWidget(ui->driversRows);
    QVBoxLayout *layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("driverName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("driverDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(title);
    layout->addWidget(desc);

    ui->driversRowsLayout->addWidget(row);
}

// ---------------------------------------------------------------- Hosts 编辑

void MainWindow::runHostsRead()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/hosts.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少 Hosts 脚本",
                    "未找到 ps1\\hosts.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    appendLog("正在读取 Hosts 文件……");

    ui->hostsSummary->setText(QStringLiteral("正在读取……"));
    ui->hostsSaveBtn->setEnabled(false);
    ui->hostsRestoreBtn->setEnabled(false);
    ui->cardHosts->setVisible(true);

    QScrollArea *page = ui->pageNetwork;
    QTimer::singleShot(0, this, [page, this] {
        page->ensureWidgetVisible(ui->cardHosts);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("read")
    };

    m_task = Task::HostsRead;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleHostsRead()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("HOSTS_JSON:");
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
        appendLog("读取 Hosts 失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->hostsSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取 Hosts 失败",
                    "未能从 PowerShell 获取 Hosts 内容。\n\n"
                    "请确认当前账户可以读取 "
                    "C:\\Windows\\System32\\drivers\\etc\\hosts，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const QString path = data.value(QStringLiteral("path")).toString();
    const QString content = data.value(QStringLiteral("content")).toString();
    const int backups = data.value(QStringLiteral("backups")).toInt();
    const QString backupName = data.value(QStringLiteral("backup")).toString();
    const QString backupTime = data.value(QStringLiteral("backupTime")).toString();

    ui->hostsEditor->setPlainText(content);
    QTextCursor cursor = ui->hostsEditor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    ui->hostsEditor->setTextCursor(cursor);

    QString summary = QStringLiteral("%1 · 共 %2 行 · 备份 %3 份")
                          .arg(QDir::toNativeSeparators(path))
                          .arg(content.isEmpty() ? 0
                                                 : content.count(QLatin1Char('\n')) + 1)
                          .arg(backups);
    if (backups > 0 && !backupName.isEmpty())
        summary += QStringLiteral("（最近 %1 · %2）").arg(backupName, backupTime);
    else
        summary += QStringLiteral("（保存时会自动创建备份）");

    ui->hostsSummary->setText(summary);
    ui->hostsSaveBtn->setEnabled(true);
    ui->hostsRestoreBtn->setEnabled(backups > 0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::saveHosts()
{
    QString detail = QStringLiteral(
        "将把编辑框中的内容写入系统 Hosts 文件。\n\n"
        "写入前会自动备份原文件，写入需要管理员权限。");
    if (ui->hostsEditor->toPlainText().trimmed().isEmpty())
        detail = QStringLiteral(
            "编辑框当前为空，保存后会清空 Hosts 里的全部规则。\n\n"
            "写入前会自动备份原文件，写入需要管理员权限。");

    if (QMessageBox::question(this, QStringLiteral("保存 Hosts"), detail)
        != QMessageBox::Yes)
        return;

    // 内容先落到临时文件，提权脚本只按路径读取，避免命令行转义问题
    const QString tempPath = QDir(QDir::tempPath())
                                 .filePath(QStringLiteral("wt-hosts-edit.txt"));
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showMessage("无法写入临时文件",
                    file.errorString(), QMessageBox::Critical);
        return;
    }
    file.write(ui->hostsEditor->toPlainText().toUtf8());
    file.close();

    runHostsWrite(QStringLiteral("save"), tempPath);
}

void MainWindow::restoreHosts()
{
    if (QMessageBox::question(this, QStringLiteral("恢复 Hosts 备份"),
                              QStringLiteral(
                                  "将用最近一次备份覆盖当前 Hosts。\n\n"
                                  "当前内容不会再次备份，恢复需要管理员权限。"))
        != QMessageBox::Yes)
        return;

    runHostsWrite(QStringLiteral("restore"), QString());
}

void MainWindow::runHostsWrite(const QString &action, const QString &source)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/hosts.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少 Hosts 脚本",
                    "未找到 ps1\\hosts.ps1，无法写入。", QMessageBox::Critical);
        return;
    }

    ++m_flow;

    m_hostsRestore = (action == QLatin1String("restore"));
    appendLog(m_hostsRestore
                  ? QStringLiteral("正在恢复 Hosts 备份，若弹出权限提示请选择“是”……")
                  : QStringLiteral("正在写入 Hosts，若弹出权限提示请选择“是”……"));

    ui->hostsSummary->setText(QStringLiteral("正在写入……"));
    ui->hostsSaveBtn->setEnabled(false);
    ui->hostsRestoreBtn->setEnabled(false);

    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), action
    };
    if (!source.isEmpty()) {
        args << QStringLiteral("-Source") << source;
    }

    m_task = Task::HostsWrite;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleHostsWrite()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("HOSTS_WRITE_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    const bool restore = m_hostsRestore;
    m_hostsRestore = false;

    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        appendLog("-------------------");
        appendLog("写入 Hosts 失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        showMessage("写入 Hosts 失败",
                    "提权进程没有返回有效结果，请重新读取 Hosts 确认当前内容。",
                    QMessageBox::Warning);
        runHostsRead();
        return;
    }

    const QJsonObject data = document.object();
    const QString code = data.value(QStringLiteral("code")).toString();
    const QString backup = data.value(QStringLiteral("backup")).toString();
    const QString detail = data.value(QStringLiteral("detail")).toString();

    // 脚本只输出英文代号，提示文案统一在这里组装
    const bool ok = code == QLatin1String("saved") || code == QLatin1String("restored");
    QString message;
    if (code == QLatin1String("saved")) {
        message = QStringLiteral("Hosts 已保存，修改前已备份。");
        if (!backup.isEmpty())
            message += QStringLiteral("\n备份文件：%1").arg(backup);
    } else if (code == QLatin1String("restored")) {
        message = QStringLiteral("已恢复备份 %1。").arg(backup);
    } else if (code == QLatin1String("canceled")) {
        message = QStringLiteral("已取消提权，Hosts 未修改。");
    } else if (code == QLatin1String("source-missing")) {
        message = QStringLiteral("待写入的临时内容已丢失，请重新保存。");
    } else if (code == QLatin1String("no-backup")) {
        message = QStringLiteral("没有可恢复的备份。");
    } else if (code == QLatin1String("no-result")) {
        message = QStringLiteral("提权进程没有返回结果，Hosts 可能未修改。");
    } else {
        message = detail.isEmpty() ? QStringLiteral("写入失败。")
                                   : QStringLiteral("操作失败：%1").arg(detail);
    }

    appendLog("-------------------");
    appendLog(message);

    if (ok) {
        showMessage(restore ? QStringLiteral("已恢复备份")
                            : QStringLiteral("Hosts 已保存"),
                    message);
        runHostsRead();
    } else {
        showMessage(restore ? QStringLiteral("恢复 Hosts 失败")
                            : QStringLiteral("保存 Hosts 失败"),
                    message, QMessageBox::Warning);
        ui->hostsSaveBtn->setEnabled(true);
        ui->hostsRestoreBtn->setEnabled(true);
        ui->hostsSummary->setText(QStringLiteral("未写入（%1）").arg(message));
    }
}

// ---------------------------------------------------------------- DNS 切换

void MainWindow::runDnsRead()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/dns.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少 DNS 脚本",
                    "未找到 ps1\\dns.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    ++m_flow;

    appendLog("正在读取当前 DNS 配置……");

    ui->dnsSummary->setText(QStringLiteral("正在读取……"));
    for (QPushButton *button : {ui->dnsAutoBtn, ui->dnsAliBtn,
                                ui->dnsTencentBtn, ui->dns114Btn,
                                ui->dnsGoogleBtn, ui->dnsBaiduBtn})
        button->setEnabled(false);
    ui->cardDns->setVisible(true);

    QScrollArea *page = ui->pageNetwork;
    QTimer::singleShot(0, this, [page, this] {
        page->ensureWidgetVisible(ui->cardDns);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("read")
    };

    m_task = Task::DnsRead;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDnsRead()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DNS_JSON:");
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
        appendLog("读取 DNS 配置失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->dnsSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取 DNS 配置失败",
                    "未能从 PowerShell 获取当前 DNS 配置，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    const int index = data.value(QStringLiteral("index")).toInt(-1);
    const QString alias = data.value(QStringLiteral("alias")).toString();
    const QString connection = data.value(QStringLiteral("connection")).toString();
    const bool autoMode = data.value(QStringLiteral("auto")).toBool();

    // 只有一项时 PowerShell 可能输出对象而不是数组
    QStringList servers;
    const QJsonValue value = data.value(QStringLiteral("dns"));
    if (value.isArray()) {
        for (const QJsonValue &server : value.toArray())
            servers << server.toString();
    } else if (value.isString()) {
        servers << value.toString();
    }

    m_dnsAlias = alias;

    QString summary;
    if (index < 0 || alias.isEmpty()) {
        summary = QStringLiteral(
            "未找到承载默认路由的网卡，暂不能切换 DNS。\n"
            "请先连接网络后重新打开本功能。");
        ui->dnsSummary->setText(summary);
        appendLog("-------------------");
        appendLog("未找到承载默认路由的网卡。");
        return;
    }

    const QString dnsText = (servers.isEmpty() || autoMode)
                                ? QStringLiteral("自动获取（由路由器分配）")
                                : servers.join(QStringLiteral(" / "));
    summary = QStringLiteral("当前网卡：%1%2\n当前 DNS：%3")
                  .arg(alias,
                       connection.isEmpty() ? QString()
                                            : QStringLiteral("（%1）").arg(connection),
                       dnsText);
    ui->dnsSummary->setText(summary);

    for (QPushButton *button : {ui->dnsAutoBtn, ui->dnsAliBtn,
                                ui->dnsTencentBtn, ui->dns114Btn,
                                ui->dnsGoogleBtn, ui->dnsBaiduBtn})
        button->setEnabled(true);

    appendLog("-------------------");
    appendLog(QStringLiteral("网卡 %1 当前 DNS：%2")
                  .arg(alias, servers.isEmpty() ? QStringLiteral("自动获取")
                                               : servers.join(QStringLiteral(", "))));
}

void MainWindow::applyDns(const QString &servers, const QString &label)
{
    if (m_dnsAlias.isEmpty()) {
        showMessage("请先读取当前配置",
                    "还没有获取到承载默认路由的网卡，请先点击「DNS 切换」磁贴读取一次。",
                    QMessageBox::Information);
        return;
    }

    const QString target = servers.isEmpty()
                               ? QStringLiteral("自动获取（由路由器分配）")
                               : servers.split(QLatin1Char(','))
                                     .join(QStringLiteral(" / "));

    if (QMessageBox::question(this, QStringLiteral("切换 DNS"),
                              QStringLiteral("将把「%1」的 DNS 设置为：\n%2（%3）\n\n"
                                             "切换需要管理员权限，确定继续？")
                                  .arg(m_dnsAlias, label, target))
        != QMessageBox::Yes)
        return;

    runDnsWrite(servers);
}

void MainWindow::runDnsWrite(const QString &servers)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/dns.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少 DNS 脚本",
                    "未找到 ps1\\dns.ps1，无法切换。", QMessageBox::Critical);
        return;
    }

    ++m_flow;

    appendLog("正在切换 DNS，若弹出权限提示请选择“是”……");

    ui->dnsSummary->setText(QStringLiteral("正在切换……"));
    for (QPushButton *button : {ui->dnsAutoBtn, ui->dnsAliBtn,
                                ui->dnsTencentBtn, ui->dns114Btn,
                                ui->dnsGoogleBtn, ui->dnsBaiduBtn})
        button->setEnabled(false);

    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("apply")
    };
    if (servers.isEmpty()) {
        args << QStringLiteral("-Auto");
    } else {
        args << QStringLiteral("-Servers") << servers;
    }

    m_task = Task::DnsWrite;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDnsWrite()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("DNS_WRITE_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    QString code;
    if (!payload.isEmpty() && parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        code = document.object().value(QStringLiteral("code")).toString();
    }

    if (code == QLatin1String("applied")) {
        appendLog("-------------------");
        appendLog("DNS 已切换，正在刷新解析缓存。");
        showMessage("DNS 已切换", QStringLiteral("新的 DNS 已生效，正在重新读取当前配置。"));
        runDnsRead();
        return;
    }

    QString message;
    if (code == QLatin1String("canceled")) {
        message = QStringLiteral("已取消提权，DNS 未修改。");
    } else if (code == QLatin1String("no-adapter")) {
        message = QStringLiteral("没有找到承载默认路由的网卡，请先连接网络。");
    } else if (code == QLatin1String("no-servers")) {
        message = QStringLiteral("没有提供可用的 DNS 地址。");
    } else if (code == QLatin1String("no-result")) {
        message = QStringLiteral("提权进程没有返回结果，DNS 可能未修改。");
    } else if (code == QLatin1String("error")) {
        message = QStringLiteral("设置 DNS 时出错，可能是权限不足或网卡不支持。");
    } else {
        message = QStringLiteral("未能解析切换结果，请重新读取 DNS 确认当前配置。");
    }

    appendLog("-------------------");
    appendLog(message);
    if (!code.isEmpty() && code != QLatin1String("error"))
        appendLog(QStringLiteral("（返回码：%1）").arg(code));

    showMessage("切换 DNS 失败", message, QMessageBox::Warning);

    ui->dnsSummary->setText(message);
    for (QPushButton *button : {ui->dnsAutoBtn, ui->dnsAliBtn,
                                ui->dnsTencentBtn, ui->dns114Btn,
                                ui->dnsGoogleBtn, ui->dnsBaiduBtn})
        button->setEnabled(!m_dnsAlias.isEmpty());
}

// ---------------------------------------------------------------- 实时监控

void MainWindow::runMonitor()
{
    ui->cardMonitor->setVisible(true);
    ui->monitorStopBtn->setEnabled(true);

    if (!m_monitor->isRunning()) {
        ui->monitorSummary->setText(
            QStringLiteral("每秒刷新一次（CPU 占用率需要两次采样后才有数值）"));
        appendLog("实时监控已启动，每秒刷新一次。");
    }

    m_monitor->start();

    QScrollArea *page = ui->pageHardware;
    QTimer::singleShot(0, this, [page, this] {
        page->ensureWidgetVisible(ui->cardMonitor);
    });
}

void MainWindow::stopMonitor()
{
    if (!m_monitor->isRunning())
        return;

    m_monitor->stop();
    ui->monitorStopBtn->setEnabled(false);
    ui->monitorSummary->setText(
        QStringLiteral("已暂停（再次点击「实时监控」磁贴可继续）"));
    appendLog("实时监控已暂停。");
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
