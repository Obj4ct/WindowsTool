/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processrunner.h"
#include "netcheck.h"
#include "sysmonitor.h"
#include "treemapwidget.h"
#include "uiicons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextDocument>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
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

/** fixdns.ps1 的错误代码 → 中文提示 */
QString fixDnsErrorLabel(const QString &code)
{
    if (code == QLatin1String("not-elevated"))
        return QStringLiteral("没有得到管理员权限，修复未执行");
    if (code == QLatin1String("no-result"))
        return QStringLiteral("提权后的脚本没有返回结果，请重试");
    if (code == QLatin1String("no-physical-adapter"))
        return QStringLiteral("没有找到已连接的物理网卡，请检查网线或 Wi-Fi 是否已连接");
    if (code == QLatin1String("dns-reset-failed"))
        return QStringLiteral("重置 DNS 配置失败，可能被安全软件拦截");
    if (code == QLatin1String("backup-failed"))
        return QStringLiteral("备份原网络配置失败，为避免无法回滚已中止修复");
    if (code == QLatin1String("dhcp-failed"))
        return QStringLiteral("把网卡改回自动获取 IP 失败");
    if (code == QLatin1String("flush-failed"))
        return QStringLiteral("清空 DNS 缓存失败");
    return QStringLiteral("未知错误（%1）").arg(code);
}

/** DNS 地址 → 预设名称，用于测速结果展示 */
QString dnsServerLabel(const QString &server)
{
    if (server == QLatin1String("223.5.5.5") || server == QLatin1String("223.6.6.6"))
        return QStringLiteral("阿里 DNS");
    if (server == QLatin1String("119.29.29.29")
        || server == QLatin1String("182.254.116.116"))
        return QStringLiteral("腾讯 DNS");
    if (server == QLatin1String("114.114.114.114")
        || server == QLatin1String("114.114.115.115"))
        return QStringLiteral("114 DNS");
    if (server == QLatin1String("8.8.8.8") || server == QLatin1String("8.8.4.4"))
        return QStringLiteral("Google DNS");
    if (server == QLatin1String("180.76.76.76"))
        return QStringLiteral("百度 DNS");
    return server;
}

/** 测速命中的服务器 → 该预设的完整服务器串（应用时保持主备双地址） */
QString dnsPresetServers(const QString &server)
{
    if (server == QLatin1String("223.5.5.5") || server == QLatin1String("223.6.6.6"))
        return QStringLiteral("223.5.5.5,223.6.6.6");
    if (server == QLatin1String("119.29.29.29")
        || server == QLatin1String("182.254.116.116"))
        return QStringLiteral("119.29.29.29,182.254.116.116");
    if (server == QLatin1String("114.114.114.114")
        || server == QLatin1String("114.114.115.115"))
        return QStringLiteral("114.114.114.114,114.114.115.115");
    if (server == QLatin1String("8.8.8.8") || server == QLatin1String("8.8.4.4"))
        return QStringLiteral("8.8.8.8,8.8.4.4");
    if (server == QLatin1String("180.76.76.76"))
        return QStringLiteral("180.76.76.76");
    return QString();
}

/** DNS 卡片里所有会改动配置的按钮（预设切换 / 应用推荐 / 恢复上次） */
QVector<QPushButton *> dnsActionButtons(Ui::MainWindow *ui)
{
    return {ui->dnsAutoBtn,      ui->dnsAliBtn,     ui->dnsTencentBtn,
            ui->dns114Btn,       ui->dnsGoogleBtn,  ui->dnsBaiduBtn,
            ui->btn_applyDnsRecommend, ui->btn_restoreDns};
}

/** 系统代理卡片里所有会改动配置或重新读取的按钮 */
QVector<QPushButton *> proxyActionButtons(Ui::MainWindow *ui)
{
    return {ui->btn_proxyOn, ui->btn_proxyOff, ui->btn_proxyRefresh,
            ui->btn_proxyRestore};
}

/** 从 proxy.ps1 输出里取出 PROXY_JSON 对象 */
bool parseProxyPayload(const QString &output, QJsonObject *object)
{
    const QString marker = QStringLiteral("PROXY_JSON:");
    const int pos = output.indexOf(marker);
    if (pos < 0)
        return false;
    const QString payload = output.mid(pos + marker.size())
                                .section(QLatin1Char('\n'), 0, 0)
                                .trimmed();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (payload.isEmpty() || error.error != QJsonParseError::NoError
        || !document.isObject())
        return false;
    *object = document.object();
    return true;
}

/** fakecheck.ps1 的风险等级 → 中文徽章 */
QString fakeCheckRiskLabel(const QString &risk)
{
    if (risk == QLatin1String("high"))
        return QStringLiteral("高风险");
    if (risk == QLatin1String("suspicious"))
        return QStringLiteral("可疑");
    return QStringLiteral("正常");
}

/** 风险等级 → QSS 状态代号 */
QString fakeCheckRiskState(const QString &risk)
{
    if (risk == QLatin1String("high"))
        return QStringLiteral("fail");
    if (risk == QLatin1String("suspicious"))
        return QStringLiteral("warn");
    return QStringLiteral("ok");
}

/** fakecheck.ps1 的风险原因代码 → 中文说明 */
QString fakeCheckReasonLabel(const QString &code)
{
    if (code == QLatin1String("tampered"))
        return QStringLiteral("签名与文件内容不匹配，文件可能被篡改");
    if (code == QLatin1String("untrusted"))
        return QStringLiteral("数字证书不受信任");
    if (code == QLatin1String("sig-error"))
        return QStringLiteral("数字签名校验失败");
    if (code == QLatin1String("unsigned"))
        return QStringLiteral("没有数字签名");
    if (code == QLatin1String("header-mismatch"))
        return QStringLiteral("扩展名与真实文件类型不符，可能被伪装");
    if (code == QLatin1String("double-ext"))
        return QStringLiteral("使用双扩展名，常见于伪装文件");
    if (code == QLatin1String("system-name"))
        return QStringLiteral("仿冒系统进程名，且不在系统目录");
    if (code == QLatin1String("temp-dir"))
        return QStringLiteral("位于临时目录且未通过签名校验");
    return code;
}

/** 归一化哈希文本：去掉空白、连字符和常见前缀，统一为大写 */
QString normalizeHashText(const QString &text)
{
    QString value = text;
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('\t'));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char(':'));
    if (value.startsWith(QLatin1String("SHA256"), Qt::CaseInsensitive))
        value.remove(0, 6);
    else if (value.startsWith(QLatin1String("SHA1"), Qt::CaseInsensitive))
        value.remove(0, 4);
    else if (value.startsWith(QLatin1String("MD5"), Qt::CaseInsensitive))
        value.remove(0, 3);
    if (value.startsWith(QLatin1String("0X"), Qt::CaseInsensitive))
        value.remove(0, 2);
    return value.toUpper();
}

/** fixdns.ps1 的动作代码 → 中文描述 */
QString fixDnsActionLabel(const QString &code)
{
    if (code == QLatin1String("backup-ipconfig"))
        return QStringLiteral("已备份网络配置");
    if (code == QLatin1String("backup-dns"))
        return QStringLiteral("已备份 DNS 设置");
    if (code == QLatin1String("ensure-dhcp"))
        return QStringLiteral("已确认使用自动获取 IP");
    if (code == QLatin1String("reset-dns"))
        return QStringLiteral("已把 DNS 改回自动获取");
    if (code == QLatin1String("flush-dns"))
        return QStringLiteral("已清空本机 DNS 缓存");
    return code;
}

/** 报告里的文本转义，避免文件路径里的 < > & 破坏 HTML 结构 */
QString escapeHtml(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return text;
}

/**
 * 解码外部工具写出的日志文件。
 *
 * SFC 输出按系统代码页（中文 Windows 为 GBK），DISM 重定向的日志是
 * UTF-16LE；个别环境也可能给出 UTF-8。这里按 BOM / 字节分布判断：
 *  - 有 UTF-16LE BOM（FF FE）或大量 0x00 落在奇数位 → UTF-16LE；
 *  - 否则先按 UTF-8 解码，出现非法字节再退回本地代码页（GBK）。
 */
QString decodeLogFile(const QByteArray &raw)
{
    if (raw.isEmpty())
        return QString();

    if (raw.size() >= 2 && raw.at(0) == '\xFF' && raw.at(1) == '\xFE') {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        return codec ? codec->toUnicode(raw.mid(2)) : QString();
    }

    // 无 BOM 的 UTF-16LE：ASCII 字符的高字节都是 0x00，落在奇数位
    bool looksUtf16 = raw.size() >= 8;
    if (looksUtf16) {
        const int limit = qMin(raw.size() / 2, 256);
        int zeros = 0;
        for (int i = 0; i < limit; ++i) {
            if (raw.at(2 * i + 1) == '\0')
                ++zeros;
        }
        looksUtf16 = zeros * 2 > limit;
    }
    if (looksUtf16) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        return codec ? codec->toUnicode(raw) : QString();
    }

    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    QTextCodec::ConverterState state;
    QString text = utf8->toUnicode(raw.constData(), raw.size(), &state);
    if (state.invalidChars == 0)
        return text;

    QTextCodec *gbk = QTextCodec::codecForName("GBK");
    return gbk ? gbk->toUnicode(raw) : text;
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
    , m_runner(nullptr)
{
    ui->setupUi(this);

    setWindowIcon(QIcon(":/ico/toolBox.png"));

    setupTheme();
    setupAbout();
    setupNavigation();
    setupOverview();
    setupNetwork();
    setupSystem();
    setupTools();

    // 每次任务可能换用新的 ProcessRunner（后台运行的旧任务仍持有旧的），
    // 连接逻辑集中在这里，避免每处都重复接线
    m_runner = createRunner();

    setupDiagSchedule();
}

ProcessRunner *MainWindow::createRunner()
{
    ProcessRunner *runner = new ProcessRunner(this);

    connect(runner, &ProcessRunner::outputReceived,
            this, [this](const QString &text) {
                // 体检/启动项/端口/大文件/磁盘/硬件脚本只输出一行 JSON 标记，不把原始数据刷进控制台
                if (m_task == Task::HealthCheck || m_task == Task::StartupList
                    || m_task == Task::NetDiagSnapshot
                    || m_task == Task::PortsList || m_task == Task::LargeFiles
                    || m_task == Task::DuplicateScan || m_task == Task::DuplicateDelete
                    || m_task == Task::DiskHealth || m_task == Task::HwInfo
                    || m_task == Task::Battery
                    || m_task == Task::ProcessList || m_task == Task::DiskUsage
                    || m_task == Task::UninstallList || m_task == Task::UninstallAction
                    || m_task == Task::DriverList
                    || m_task == Task::HostsRead || m_task == Task::HostsWrite
                    || m_task == Task::DnsRead || m_task == Task::DnsWrite
                    || m_task == Task::FileLock || m_task == Task::BootTime
                    || m_task == Task::ContextMenuList
                    || m_task == Task::ContextMenuToggle
                    || m_task == Task::StartupToggle
                    || m_task == Task::Treemap
                    || m_task == Task::SxsCleanup
                    || m_task == Task::RestoreList
                    || m_task == Task::RestoreCreate
                    || m_task == Task::SfcRepair
                    || m_task == Task::WUpdateRepair)
                    return;
                appendLog(text);
            });
    connect(runner, &ProcessRunner::finished,
            this, &MainWindow::onTaskFinished);
    connect(runner, &ProcessRunner::failed,
            this, &MainWindow::onTaskFailed);

    // 「处理过程」面板的显隐跟随当前执行器：终止/转入后台的旧任务不再
    // 影响面板，新任务启动后重新计时
    connect(runner, &ProcessRunner::started, this, [this]() {
        m_busyIdle->stop();
        if (!m_busyShown)
            m_busyDelay->start();
    });

    const auto taskOver = [this]() {
        m_busyDelay->stop();
        if (m_busyShown)
            m_busyIdle->start();
    };
    connect(runner, &ProcessRunner::finished, this,
            [taskOver](int) { taskOver(); });
    connect(runner, &ProcessRunner::failed, this,
            [taskOver](const QString &) { taskOver(); });

    return runner;
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

    // 后台任务的执行器同样可能在销毁时同步回调，一并断开并终止
    for (const BackgroundTask &task : m_background) {
        if (!task.runner)
            continue;
        disconnect(task.runner, nullptr, this, nullptr);
        task.runner->stop();
    }
    m_background.clear();

    delete ui;
}

// ---------------------------------------------------------------- 主题

void MainWindow::setupTheme()
{
    applyStyleSheet(*qApp);

    // 侧栏品牌标识
    ui->logoLabel->setPixmap(
        UiIcons::makeBadge(UiIcons::Icon::Logo, cBadgeBg, cAccent, 40, 11));

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

    // 概览页快捷入口
    ui->btn_quickHealth->setIcon(tileIcon(UiIcons::Icon::Health));
    ui->btn_quickNetwork->setIcon(tileIcon(UiIcons::Icon::Network));
    ui->btn_quickPorts->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_quickDisk->setIcon(tileIcon(UiIcons::Icon::DiskPulse));

    // 网络
    ui->btn_autoCheck->setIcon(tileIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_showNetWork->setIcon(tileIcon(UiIcons::Icon::Info));
    ui->btn_ports->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_hosts->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_dns->setIcon(tileIcon(UiIcons::Icon::Network));
    ui->btn_proxy->setIcon(tileIcon(UiIcons::Icon::Link));

    // 系统维护
    ui->btn_cleaner->setIcon(tileIcon(UiIcons::Icon::Clean));
    ui->btn_fixWhite->setIcon(tileIcon(UiIcons::Icon::Picture));
    ui->btn_healthCheck->setIcon(tileIcon(UiIcons::Icon::Health));
    ui->btn_sfc->setIcon(tileIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_wupdate->setIcon(tileIcon(UiIcons::Icon::Refresh));
    ui->btn_sxsClean->setIcon(tileIcon(UiIcons::Icon::Clean));
    ui->btn_restore->setIcon(tileIcon(UiIcons::Icon::Reset));

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
    ui->btn_contextMenu->setIcon(tileIcon(UiIcons::Icon::Dots));
    ui->btn_bootTime->setIcon(tileIcon(UiIcons::Icon::Rocket));
    ui->btn_fileLock->setIcon(tileIcon(UiIcons::Icon::Link));
    ui->btn_fakeCheck->setIcon(tileIcon(UiIcons::Icon::ShieldCheck));
    ui->btn_treemap->setIcon(tileIcon(UiIcons::Icon::DiskPulse));

    // 磁贴图标尺寸（QToolButton 不会自动跟随 QSS 的 icon-size）
    for (QToolButton *b : {ui->btn_quickHealth, ui->btn_quickNetwork,
                            ui->btn_quickPorts, ui->btn_quickDisk,
                            ui->btn_autoCheck, ui->btn_showNetWork,
                            ui->btn_ports,
                            ui->btn_hosts, ui->btn_dns,
                            ui->btn_proxy,
                            ui->btn_cleaner, ui->btn_fixWhite,
                            ui->btn_healthCheck, ui->btn_sfc,
                            ui->btn_wupdate,
                            ui->btn_sxsClean, ui->btn_restore,
                            ui->btn_diskHealth, ui->btn_hwinfo,
                            ui->btn_battery, ui->btn_monitor,
                            ui->btn_startup, ui->btn_process,
                            ui->btn_uninstall, ui->btn_drivers,
                            ui->btn_largeFiles, ui->btn_treedisk,
                            ui->btn_dup, ui->btn_contextMenu,
                            ui->btn_bootTime, ui->btn_fileLock,
                            ui->btn_fakeCheck,
                            ui->btn_treemap}) {
        b->setIconSize(QSize(30, 30));
    }

    // 次级按钮图标
    ui->btn_refreshDNS->setIcon(UiIcons::make(UiIcons::Icon::Refresh, QColor("#6B7A93"), 16));
    ui->btn_resetWinSock->setIcon(UiIcons::make(UiIcons::Icon::Reset, QColor("#6B7A93"), 16));

    // 卡片加一层柔和投影，让白色卡片从浅色渐变背景上浮起来
    for (QFrame *card : findChildren<QFrame *>()) {
        if (!card->objectName().startsWith(QLatin1String("card")))
            continue;
        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(24);
        shadow->setOffset(0, 5);
        shadow->setColor(QColor(28, 46, 80, 24));
        card->setGraphicsEffect(shadow);
    }

    // 控制台
    ui->textBrowser->setOpenExternalLinks(true);

    // 清空日志
    connect(ui->clearLogButton, &QPushButton::clicked,
            this, &MainWindow::clearLog);

    // 后台任务入口（没有后台任务时隐藏）
    connect(ui->bgTaskButton, &QPushButton::clicked,
            this, &MainWindow::showBackgroundTasks);

    // ------------------------------------------------- 耗时任务的提示
    // 命令开始后先等 350ms 再弹出底部「处理过程」面板：太快结束的命令
    // （如读取 DNS）不会闪一下；仍然在跑的命令则给出明确的等待提示。
    // 命令结束后 1.6 秒内没有后续命令才收起，这样自动检测的
    // ping → DNS → 修复 串联步骤之间不会反复开关。
    m_busyDelay = new QTimer(this);
    m_busyDelay->setSingleShot(true);
    m_busyDelay->setInterval(350);
    m_busyIdle = new QTimer(this);
    m_busyIdle->setSingleShot(true);
    m_busyIdle->setInterval(1600);

    connect(m_busyDelay, &QTimer::timeout, this, &MainWindow::showBusyHint);
    connect(m_busyIdle, &QTimer::timeout, this, &MainWindow::hideBusyHint);
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
        {ui->navOperationsBtn, 6},
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
        ui->navOperationsBtn,
    };
    const int count = int(sizeof(buttons) / sizeof(buttons[0]));
    if (index >= 0 && index < count)
        buttons[index]->setChecked(true);

    if (index == 6)
        refreshOperationPage();

    // 页面切换做一次短暂淡入，替代生硬的整页硬切
    if (QWidget *page = ui->stackedWidget->currentWidget()) {
        auto *effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);

        auto *anim = new QPropertyAnimation(effect, "opacity", page);
        anim->setDuration(160);
        anim->setStartValue(0.3);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, page, [page]() {
            page->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

// ---------------------------------------------------------------- 操作记录

QString MainWindow::operationsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(dir).filePath(QStringLiteral("operations.jsonl"));
}

void MainWindow::recordOperation(const QString &feature, const QString &action,
                                 const QString &target, bool ok,
                                 bool undoable, const QString &undoKind,
                                 const QJsonObject &undoArgs)
{
    const QString path = operationsFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        appendLog(QStringLiteral("操作记录目录创建失败：%1").arg(path));
        return;
    }

    static int sequence = 0;
    const QDateTime now = QDateTime::currentDateTime();
    const QString id = now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
                       + QLatin1Char('-') + QString::number(++sequence);

    QJsonObject object;
    object.insert(QStringLiteral("kind"), QStringLiteral("op"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("time"),
                  now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    object.insert(QStringLiteral("feature"), feature);
    object.insert(QStringLiteral("action"), action);
    object.insert(QStringLiteral("target"), target);
    object.insert(QStringLiteral("ok"), ok);
    object.insert(QStringLiteral("undoable"), undoable);
    if (undoable) {
        object.insert(QStringLiteral("undoKind"), undoKind);
        object.insert(QStringLiteral("undoArgs"), undoArgs);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        appendLog(QStringLiteral("操作记录写入失败：%1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();

    refreshOperationPage();
}

QList<MainWindow::OperationRecord> MainWindow::readOperationRecords() const
{
    QList<OperationRecord> records;
    QHash<QString, int> indexById;

    QFile file(operationsFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return records;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            continue;

        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("kind")).toString()
            == QLatin1String("undo")) {
            const int index = indexById.value(
                object.value(QStringLiteral("id")).toString(), -1);
            if (index >= 0)
                records[index].undone = true;
            continue;
        }

        OperationRecord record;
        record.id = object.value(QStringLiteral("id")).toString();
        record.time = object.value(QStringLiteral("time")).toString();
        record.feature = object.value(QStringLiteral("feature")).toString();
        record.action = object.value(QStringLiteral("action")).toString();
        record.target = object.value(QStringLiteral("target")).toString();
        record.ok = object.value(QStringLiteral("ok")).toBool();
        record.undoable = object.value(QStringLiteral("undoable")).toBool();
        record.undoKind = object.value(QStringLiteral("undoKind")).toString();
        record.undoArgsJson = QString::fromUtf8(
            QJsonDocument(object.value(QStringLiteral("undoArgs")).toObject())
                .toJson(QJsonDocument::Compact));

        indexById.insert(record.id, records.size());
        records.append(record);
    }

    // 新记录显示在最上面
    QList<OperationRecord> ordered;
    ordered.reserve(records.size());
    for (int i = records.size() - 1; i >= 0; --i)
        ordered.append(records.at(i));
    return ordered;
}

void MainWindow::markOperationUndone(const QString &id)
{
    if (id.isEmpty())
        return;

    const QString path = operationsFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        appendLog(QStringLiteral("撤销状态写入失败：%1").arg(file.errorString()));
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("kind"), QStringLiteral("undo"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("time"),
                  QDateTime::currentDateTime().toString(
                      QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();
}

void MainWindow::refreshOperationPage()
{
    while (QLayoutItem *item = ui->operationsRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const QList<OperationRecord> records = readOperationRecords();
    int undoableCount = 0;
    for (const OperationRecord &record : records) {
        if (record.undoable && !record.undone)
            ++undoableCount;
    }

    ui->operationsEmpty->setVisible(records.isEmpty());
    ui->operationsSummary->setText(
        QStringLiteral("共 %1 条记录 · 可撤销 %2 条")
            .arg(records.size()).arg(undoableCount));

    for (const OperationRecord &record : records)
        addOperationRow(record);

    ui->operationsScroll->verticalScrollBar()->setValue(0);
}

void MainWindow::addOperationRow(const OperationRecord &record)
{
    QFrame *row = new QFrame(ui->operationsRows);
    row->setObjectName(QStringLiteral("operationRow"));
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(10);

    QLabel *time = new QLabel(record.time, row);
    time->setObjectName(QStringLiteral("operationsTime"));
    time->setFixedWidth(132);

    QLabel *badge = new QLabel(record.feature, row);
    badge->setObjectName(QStringLiteral("operationsBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(80);

    QLabel *action = new QLabel(record.action, row);
    action->setObjectName(QStringLiteral("operationsAction"));
    action->setWordWrap(true);

    QLabel *target = new QLabel(record.target, row);
    target->setObjectName(QStringLiteral("operationsTarget"));
    target->setWordWrap(true);
    target->setTextInteractionFlags(Qt::TextSelectableByMouse);
    target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(action);
    if (!record.target.isEmpty())
        text->addWidget(target);

    QString statusText = QStringLiteral("不可恢复");
    QString statusState = QStringLiteral("irreversible");
    if (record.undone) {
        statusText = QStringLiteral("已撤销");
        statusState = QStringLiteral("undone");
    } else if (record.undoable) {
        statusText = QStringLiteral("成功");
        statusState = QStringLiteral("ok");
    }

    QLabel *status = new QLabel(statusText, row);
    status->setObjectName(QStringLiteral("operationsStatus"));
    status->setProperty("state", statusState);
    status->setAlignment(Qt::AlignCenter);
    status->setFixedWidth(62);

    bool canUndo = record.undoable && !record.undone;
    const QJsonObject undoArgs = QJsonDocument::fromJson(
        record.undoArgsJson.toUtf8()).object();
    if (canUndo && record.undoKind == QLatin1String("hosts-save"))
        canUndo = !undoArgs.value(QStringLiteral("backup")).toString().isEmpty();
    if (canUndo && record.undoKind == QLatin1String("hosts-restore"))
        canUndo = QFileInfo(undoArgs.value(QStringLiteral("source")).toString())
                      .exists();

    layout->addWidget(time, 0, Qt::AlignTop);
    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);
    layout->addWidget(status, 0, Qt::AlignTop);

    if (canUndo) {
        QPushButton *undo = new QPushButton(QStringLiteral("撤销"), row);
        undo->setObjectName(QStringLiteral("operationsUndoBtn"));
        undo->setCursor(Qt::PointingHandCursor);
        undo->setFixedSize(56, 26);
        connect(undo, &QPushButton::clicked, this,
                [this, record]() { startUndo(record); });
        layout->addWidget(undo, 0, Qt::AlignTop);
    }

    ui->operationsRowsLayout->addWidget(row);
}

void MainWindow::startUndo(const OperationRecord &record)
{
    QString detail = QStringLiteral("将撤销「%1 · %2」。")
                         .arg(record.feature, record.action);
    if (!record.target.isEmpty())
        detail += QStringLiteral("\n目标：%1").arg(record.target);
    detail += QStringLiteral("\n\n会尽量把系统恢复到操作前的状态，继续吗？");

    if (!confirmAction("撤销操作", detail))
        return;

    if (!claimTask(QStringLiteral("撤销操作")))
        return;

    const QJsonObject args = QJsonDocument::fromJson(
        record.undoArgsJson.toUtf8()).object();

    m_undoId = record.id;
    m_undoKind = record.undoKind;

    if (record.undoKind == QLatin1String("startup")) {
        runStartupToggle(args.value(QStringLiteral("action")).toString(),
                         args.value(QStringLiteral("source")).toString(),
                         args.value(QStringLiteral("name")).toString(),
                         args.value(QStringLiteral("command")).toString());
    } else if (record.undoKind == QLatin1String("contextmenu")) {
        m_cmDisplay = args.value(QStringLiteral("display")).toString();
        if (m_cmDisplay.isEmpty())
            m_cmDisplay = args.value(QStringLiteral("entry")).toString();
        runContextMenuToggle(args.value(QStringLiteral("root")).toString(),
                             args.value(QStringLiteral("scope")).toString(),
                             args.value(QStringLiteral("entry")).toString(),
                             args.value(QStringLiteral("disable")).toBool());
    } else if (record.undoKind == QLatin1String("hosts-save")) {
        m_hostsBackup = args.value(QStringLiteral("backup")).toString();
        runHostsWrite(QStringLiteral("restore"), QString());
    } else if (record.undoKind == QLatin1String("hosts-restore")) {
        runHostsWrite(QStringLiteral("save"),
                      args.value(QStringLiteral("source")).toString());
    } else {
        m_undoId.clear();
        m_undoKind.clear();
    }
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

// ---------------------------------------------------------------- 网络模块

void MainWindow::setupNetwork()
{
    // 自动检测并修复。用户主动点一次就是新的一轮，允许再尝试一次自动修复。
    connect(ui->btn_autoCheck, &QAbstractButton::clicked, this, [this]() {
        m_autoFixAttempted = false;
        startAutoCheck();
    });

    // 导出分层诊断报告
    connect(ui->btn_exportDiagReport, &QPushButton::clicked,
            this, &MainWindow::exportDiagReport);

    // 查看网络信息
    connect(ui->btn_showNetWork, &QAbstractButton::clicked,
            this, &MainWindow::showNetworkInfo);

    // 刷新 DNS
    connect(ui->btn_refreshDNS, &QPushButton::clicked, this, [this]() {
        if (!confirmAction("刷新网络设置",
                           "会把你正在用的网卡改回「自动获取 DNS」，并清空本机的 DNS 缓存。"
                           "原来的设置会先自动备份，静态 IP、VPN 和虚拟网卡都不会被改动。\n\n"
                           "操作期间可能短暂上不了网，继续吗？"))
            return;

        ++m_flow;

        clearLog();
        appendLog("正在刷新网络配置，请稍候……");
        runFixDns(false);
    });

    // 刷新 winsock
    connect(ui->btn_resetWinSock, &QPushButton::clicked, this, [this]() {
        if (!confirmAction("重置网络组件",
                           "网页打不开、提示网络协议出错时，重置网络组件往往能修好。\n\n"
                           "重置后需要重启电脑才能完全生效，重启前网络可能暂时不可用。"
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
        appendLog("正在重置网络组件，请稍候……");
        m_task = Task::WinsockReset;
        m_runner->runBat("winsockReset.bat");
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

    connect(ui->dnsSpeedBtn, &QAbstractButton::clicked,
            this, &MainWindow::runDnsSpeedTest);
    connect(ui->btn_applyDnsRecommend, &QAbstractButton::clicked,
            this, &MainWindow::applyRecommendedDns);
    connect(ui->btn_restoreDns, &QAbstractButton::clicked,
            this, &MainWindow::restoreDns);

    // 系统代理开关
    connect(ui->btn_proxy, &QAbstractButton::clicked,
            this, &MainWindow::runProxyRead);
    connect(ui->btn_proxyRefresh, &QAbstractButton::clicked,
            this, &MainWindow::runProxyRead);
    connect(ui->btn_proxyOff, &QAbstractButton::clicked, this,
            [this] { writeProxy(QStringLiteral("off")); });
    connect(ui->btn_proxyRestore, &QAbstractButton::clicked, this,
            [this] { writeProxy(QStringLiteral("restore")); });
    connect(ui->btn_proxyOn, &QAbstractButton::clicked, this, [this] {
        writeProxy(QStringLiteral("apply"), ui->proxyServerInput->text());
    });

    // 分层诊断：非阻塞 DNS 探测器，事件驱动，不占线程
    m_dnsRunner = new DnsProbe::AsyncRunner(this);
    connect(m_dnsRunner, &DnsProbe::AsyncRunner::completed,
            this, &MainWindow::handleDnsProbeCompleted);

    // DNS 测速用独立探测器，避免和分层诊断互相取消
    m_dnsSpeedRunner = new DnsProbe::AsyncRunner(this);
    connect(m_dnsSpeedRunner, &DnsProbe::AsyncRunner::completed,
            this, &MainWindow::handleDnsSpeedResults);

    // L4–L6：TCP / HTTPS / 代理端口探测，同样在事件循环里完成
    m_deepProbe = new NetProbe::Runner(this);
    connect(m_deepProbe, &NetProbe::Runner::finished,
            this, &MainWindow::handleDeepProbeFinished);

    // 多目标站点连通性：百度 / 腾讯 / 微软，独立于主探测流程
    m_targetRunner = new NetProbe::TargetRunner(this);
    connect(m_targetRunner, &NetProbe::TargetRunner::finished,
            this, &MainWindow::handleTargetProbeFinished);

    // 诊断历史：启动时载入本地记录，检测完成后自动追加
    loadDiagHistory();
}

/**
 * 异步任务完成回调：按当前任务类型分发。
 * 同一时刻只跑一个任务，ProcessRunner 会自动终止上一个进程。
 */
void MainWindow::onTaskFinished(int exitCode)
{
    dispatchTaskFinished(exitCode);
    m_task = Task::None;
}

void MainWindow::dispatchTaskFinished(int exitCode)
{
    switch (m_task) {
    case Task::PingAutoCheck:
        handleAutoCheckPing();
        break;
    case Task::DnsCheck:
        handleDnsCheck();
        break;
    case Task::NetDiagSnapshot:
        handleNetDiagSnapshot();
        break;
    case Task::FixDnsAuto:
        handleFixDnsResult(true);
        break;
    case Task::FixDnsManual:
        handleFixDnsResult(false);
        break;
    case Task::Cleaner:
        appendLog("-------------------");
        appendLog("系统临时文件清理完成");
        recordOperation(QStringLiteral("系统清理"),
                        QStringLiteral("清理系统临时文件"),
                        QStringLiteral("系统临时目录"), true);
        showMessage("清理完成", "系统临时文件已清理完毕。");
        break;
    case Task::WinsockReset:
        appendLog("-------------------");
        appendLog("网络组件已重置，重启电脑后生效");
        recordOperation(QStringLiteral("网络修复"),
                        QStringLiteral("重置网络组件（Winsock）"),
                        QStringLiteral("本机网络协议栈"), true);
        showMessage("网络组件已重置",
                    "网络组件已经重新初始化，重启电脑后才会完全生效。");
        break;
    case Task::FixWhite:
        appendLog("-------------------");
        appendLog("桌面图标缓存重建完成");
        recordOperation(QStringLiteral("系统清理"),
                        QStringLiteral("重建桌面图标缓存"),
                        QStringLiteral("资源管理器图标缓存"), true);
        showMessage("图标缓存已重建",
                    "桌面图标缓存已重建完成。\n\n"
                    "若图标仍未恢复，可重启资源管理器或重启计算机后再查看。");
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
    case Task::ProxyRead:
        handleProxyRead();
        break;
    case Task::ProxyWrite:
        handleProxyWrite();
        break;
    case Task::FileLock:
        handleFileLock();
        break;
    case Task::FileVerify:
        handleFakeCheck();
        break;
    case Task::BootTime:
        handleBootTime();
        break;
    case Task::ContextMenuList:
        handleContextMenuList();
        break;
    case Task::ContextMenuToggle:
        handleContextMenuToggle();
        break;
    case Task::StartupToggle:
        handleStartupToggle();
        break;
    case Task::Treemap:
        handleTreemap();
        break;
    case Task::SxsCleanup:
        handleSxsCleanup(exitCode);
        break;
    case Task::RestoreList:
        handleRestorePointList();
        break;
    case Task::RestoreCreate:
        handleRestorePointCreate();
        break;
    case Task::SfcRepair:
        handleSfcRepair(exitCode);
        break;
    case Task::WUpdateRepair:
        handleWUpdateFix(exitCode);
        break;
    case Task::NetInfo:
        handleNetworkInfo();
        break;
    case Task::None:
        break;
    }
}

void MainWindow::onTaskFailed(const QString &reason)
{
    m_undoId.clear();
    m_undoKind.clear();
    m_hostsBackup.clear();
    m_hostsUndoSource.clear();

    if (m_task == Task::SfcRepair) {
        if (m_sfcTimer)
            m_sfcTimer->stop();
        pollSfcLog();
        setSfcRunning(false);
        ui->sfcSummary->setText(
            QStringLiteral("修复未能启动或异常中断：%1").arg(reason));
        ui->sfcSummary->setProperty("state", QStringLiteral("risk"));
        ui->sfcSummary->style()->unpolish(ui->sfcSummary);
        ui->sfcSummary->style()->polish(ui->sfcSummary);
    }

    if (m_task == Task::WUpdateRepair) {
        ui->btnWUpdateFix->setEnabled(true);
        ui->wupdateSummary->setText(
            QStringLiteral("重置未能启动或异常中断：%1").arg(reason));
        ui->wupdateSummary->setProperty("state", QStringLiteral("risk"));
        ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
        ui->wupdateSummary->style()->polish(ui->wupdateSummary);
    }

    if (m_task == Task::SxsCleanup) {
        ui->btnWUpdateFix->setEnabled(true);
        ui->wupdateSummary->setText(
            QStringLiteral("清理未能启动或异常中断：%1").arg(reason));
        ui->wupdateSummary->setProperty("state", QStringLiteral("risk"));
        ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
        ui->wupdateSummary->style()->polish(ui->wupdateSummary);
        m_sxsLogPath.clear();
    }

    if (m_task == Task::RestoreList) {
        ui->restoreSummary->setText(
            QStringLiteral("读取还原点失败：%1").arg(reason));
        ui->restoreSummary->setProperty("state", QStringLiteral("risk"));
        ui->restoreSummary->style()->unpolish(ui->restoreSummary);
        ui->restoreSummary->style()->polish(ui->restoreSummary);
    }

    if (m_task == Task::RestoreCreate) {
        m_restoreBusy = false;
        ui->restoreCreateBtn->setEnabled(true);
        ui->restoreSummary->setText(
            QStringLiteral("创建还原点失败：%1").arg(reason));
        ui->restoreSummary->setProperty("state", QStringLiteral("risk"));
        ui->restoreSummary->style()->unpolish(ui->restoreSummary);
        ui->restoreSummary->style()->polish(ui->restoreSummary);
    }

    if (m_task == Task::Treemap) {
        ui->treemapSummary->setText(
            QStringLiteral("统计失败：%1").arg(reason));
        if (m_treemap)
            m_treemap->showPlaceholder(QStringLiteral("统计失败，请重试"));
    }

    if (m_task == Task::FileVerify) {
        m_verifyResultFile.clear();
        setFakeCheckRunning(false);
        ui->fakeCheckSummary->setText(
            QStringLiteral("检查失败：%1").arg(reason));
        ui->fakeCheckSummary->setProperty("state", QStringLiteral("fail"));
        ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
    }

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
    if (!claimTask(QStringLiteral("一键自动检测"))) {
        m_silentDiag = false;
        return;
    }

    ++m_flow;

    if (!m_silentDiag)
        clearLog();
    appendLog(m_silentDiag ? "定时巡检开始……" : "正在分层诊断网络，稍等片刻……");

    clearDiagRows();
    m_diagSiteResults.clear();
    m_hasDiagReport = false;
    ui->netDiagSummary->setText(QStringLiteral("正在读取网卡、路由与 DNS 配置……"));
    ui->netDiagSummary->setProperty("state", QString());
    ui->netDiagSummary->style()->unpolish(ui->netDiagSummary);
    ui->netDiagSummary->style()->polish(ui->netDiagSummary);
    ui->netDiagHint->setText(
        QStringLiteral("依次检查网卡、路由、DNS、目标连接与网站响应，只报告首个出问题的环节"));

    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/netdiag.ps1"));
    if (!QFileInfo::exists(script)) {
        if (m_silentDiag) {
            m_silentDiag = false;
            appendLog("定时巡检跳过：未找到 ps1\\netdiag.ps1。");
            showTrayNotification(QStringLiteral("定时巡检无法执行"),
                                 QStringLiteral("未找到 ps1\\netdiag.ps1，请确认程序目录完整。"));
            return;
        }
        appendLog("未找到 ps1\\netdiag.ps1，改用传统检测……");
        startLegacyAutoCheck();
        return;
    }

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::NetDiagSnapshot;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

/** 诊断脚本缺失或输出损坏时的降级路径：沿用旧的 ping → DNS 串联检测 */
void MainWindow::startLegacyAutoCheck()
{
    ui->netDiagSummary->setText(QStringLiteral("诊断脚本不可用，已改用传统检测"));
    ui->netDiagSummary->setProperty("state", QString());
    ui->netDiagSummary->style()->unpolish(ui->netDiagSummary);
    ui->netDiagSummary->style()->polish(ui->netDiagSummary);
    m_task = Task::PingAutoCheck;
    m_runner->runCommand(QStringLiteral("ping www.baidu.com"));
}

/** netdiag.ps1 执行完毕：解析快照，成功则继续 DNS 探测，失败则降级 */
void MainWindow::handleNetDiagSnapshot()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("NETDIAG_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QString error;
    m_diagSnapshot = NetDiag::parseSnapshot(payload.toUtf8(), &error);
    if (!m_diagSnapshot.error.isEmpty()) {
        appendLog("-------------------");
        appendLog(QStringLiteral("读取网络状态失败：%1").arg(m_diagSnapshot.error));
        appendLog("改用传统检测继续……");
        startLegacyAutoCheck();
        return;
    }

    startDnsProbe();
}

/** 收集在线网卡的 DNS 服务器，交给非阻塞探测器并发查询 */
void MainWindow::startDnsProbe()
{
    QStringList primary;
    QStringList backup;
    const auto collect = [](const NetDiag::AdapterInfo &adapter, QStringList *target,
                            const QStringList &exclude) {
        for (const QString &server : adapter.dns) {
            if (exclude.contains(server) || target->contains(server))
                continue;
            target->append(server);
        }
    };

    // 主探测用默认路由网卡的 DNS；其余在线网卡的 DNS 留作备用。
    // 家用路由器的 DNS 偶尔响应慢，这时后备服务器能避免误判成解析失败。
    for (const NetDiag::AdapterInfo &adapter : m_diagSnapshot.adapters) {
        if (adapter.name == m_diagSnapshot.defaultAdapter)
            collect(adapter, &primary, QStringList());
    }
    for (const NetDiag::AdapterInfo &adapter : m_diagSnapshot.adapters) {
        if (adapter.name == m_diagSnapshot.defaultAdapter)
            continue;
        if (adapter.status.compare(QLatin1String("Up"), Qt::CaseInsensitive) == 0)
            collect(adapter, &backup, primary);
    }

    if (primary.isEmpty()) {
        primary = backup;
        backup.clear();
    }

    m_diagBackupServers = backup;
    m_diagRetried = false;

    if (primary.isEmpty() || !m_dnsRunner) {
        handleDnsProbeCompleted(QVector<DnsProbe::Result>());
        return;
    }

    appendLog(QStringLiteral("正在探测 %1 个 DNS 服务器的响应……").arg(primary.size()));
    ui->netDiagSummary->setText(QStringLiteral("正在探测 DNS 响应……"));

    m_diagProbing = true;
    m_diagFlow = m_flow;
    m_dnsRunner->start(primary, QStringLiteral("www.baidu.com"), 1500);
}

/** DNS 探测结束：汇总 L1→L7 结论、渲染面板并按首个失败层决定下一步 */
void MainWindow::handleDnsProbeCompleted(const QVector<DnsProbe::Result> &results)
{
    const bool wasProbing = m_diagProbing;
    m_diagProbing = false;

    // 探测期间用户开始了别的任务，这次结果作废
    if (wasProbing && m_diagFlow != m_flow)
        return;

    // 主 DNS 全部没响应时，用其它在线网卡的 DNS 复查一次，避免路由器
    // 偶发慢响应就把正常网络判成「DNS 解析失败」而误触发修复。
    bool anyOk = false;
    for (const DnsProbe::Result &item : results) {
        if (item.status == DnsProbe::Status::Ok) {
            anyOk = true;
            break;
        }
    }
    if (wasProbing && !anyOk && !m_diagRetried && !m_diagBackupServers.isEmpty()
        && m_dnsRunner) {
        m_diagRetried = true;
        m_diagProbing = true;
        appendLog("主 DNS 未响应，正在用备用 DNS 服务器复查……");
        ui->netDiagSummary->setText(QStringLiteral("主 DNS 未响应，正在复查……"));
        m_dnsRunner->start(m_diagBackupServers, QStringLiteral("www.baidu.com"), 1500);
        return;
    }

    QVector<DnsProbe::Result> effective = results;
    if (m_diagRetried && anyOk) {
        // 备用复查成功：只保留成功的服务器。其它网卡上失效的 DNS 配置
        // 与当前网络无关，不应变成用户需要处理的告警。
        effective.clear();
        for (const DnsProbe::Result &item : results) {
            if (item.status == DnsProbe::Status::Ok)
                effective.append(item);
        }
    }

    m_diagDnsResults = effective;
    startDeepProbe();
}

/** 从 DNS 结果里取目标地址，发起 TCP / HTTPS / 代理探测 */
void MainWindow::startDeepProbe()
{
    QString target;
    int elapsed = -1;
    for (const DnsProbe::Result &item : m_diagDnsResults) {
        if (item.status != DnsProbe::Status::Ok || item.addresses.isEmpty())
            continue;
        if (target.isEmpty() || (item.elapsedMs >= 0
                                 && (elapsed < 0 || item.elapsedMs < elapsed))) {
            target = item.addresses.first();
            elapsed = item.elapsedMs;
        }
    }

    // DNS 层没解析成功，后面的探测没有意义，直接汇总（那里会停在上一个失败层）
    if (target.isEmpty()) {
        completeDiagnosis(NetProbe::Outcome());
        return;
    }

    QString proxyHost;
    quint16 proxyPort = 0;
    if (m_diagSnapshot.proxy.enabled) {
        const NetDiag::ProxyEndpoint endpoint =
            NetDiag::parseProxyServer(m_diagSnapshot.proxy.server);
        if (endpoint.valid()) {
            proxyHost = endpoint.host;
            proxyPort = endpoint.port;
        }
    }

    appendLog(QStringLiteral("正在检查 %1:443 的连接与 HTTPS 响应……").arg(target));
    ui->netDiagSummary->setText(QStringLiteral("正在检查目标端口与 HTTPS 响应……"));

    m_diagProbing = true;
    m_diagFlow = m_flow;
    m_deepProbe->start(target, 443, proxyHost, proxyPort, 1500);
}

/** L4–L6 探测结束：汇总并展示结论 */
void MainWindow::handleDeepProbeFinished(const NetProbe::Outcome &outcome)
{
    const bool wasProbing = m_diagProbing;
    m_diagProbing = false;

    if (wasProbing && m_diagFlow != m_flow)
        return;

    m_diagOutcome = outcome;
    startTargetProbe();
}

/**
 * 主探测结束后并发检查常用站点：先解析域名，再连接 443。
 * 结论用于区分「全局断网」和「单站点故障」，不阻塞其它流程。
 */
void MainWindow::startTargetProbe()
{
    QStringList servers;
    for (const DnsProbe::Result &result : m_diagDnsResults) {
        if (result.status == DnsProbe::Status::Ok && !servers.contains(result.server))
            servers.append(result.server);
    }

    const QVector<NetProbe::SiteTarget> targets = {
        {QStringLiteral("百度"), QStringLiteral("www.baidu.com"), 443},
        {QStringLiteral("腾讯"), QStringLiteral("www.qq.com"), 443},
        {QStringLiteral("微软"), QStringLiteral("www.microsoft.com"), 443}
    };

    // 主 DNS 都没解析成功时，目标检测没有意义，直接给出分层结论
    if (!m_targetRunner || servers.isEmpty()) {
        completeDiagnosis(m_diagOutcome);
        return;
    }

    appendLog("正在检测常用目标站点（百度 / 腾讯 / 微软）……");
    ui->netDiagSummary->setText(QStringLiteral("正在检测常用目标站点……"));

    m_diagProbing = true;
    m_diagFlow = m_flow;
    m_targetRunner->start(targets, servers, 1200);
}

/** 多目标检测结束：把结果并入分层结论 */
void MainWindow::handleTargetProbeFinished(
    const QVector<NetProbe::SiteResult> &results)
{
    const bool wasProbing = m_diagProbing;
    m_diagProbing = false;

    if (wasProbing && m_diagFlow != m_flow)
        return;

    m_diagSiteResults = results;
    completeDiagnosis(m_diagOutcome);
}

/** 用完整的探测结果生成结论、渲染面板，并按首个失败层决定后续动作 */
void MainWindow::completeDiagnosis(const NetProbe::Outcome &outcome)
{
    // L5 的 HTTPS 结果来自脚本快照（Qt 端只做 TCP 探测）
    NetProbe::Outcome merged = outcome;
    merged.https = m_diagSnapshot.https;

    NetDiag::DiagResult result =
        NetDiag::diagnose(m_diagSnapshot, m_diagDnsResults, merged);

    // 多目标检测只作为补充结论：全部失败才升级为失败，部分失败仅提示
    NetDiag::CheckItem siteItem;
    bool hasSiteItem = false;
    if (!m_diagSiteResults.isEmpty()) {
        siteItem = NetDiag::evaluateSites(m_diagSiteResults);
        hasSiteItem = true;
        result.items.append(siteItem);

        if (!result.hasFailure && siteItem.status == NetDiag::Status::Fail) {
            result.hasFailure = true;
            result.firstFailed = NetDiag::Layer::Targets;
            result.summary = QStringLiteral("目标连通性未通过：%1").arg(siteItem.detail);
            result.suggestion = QStringLiteral(
                "目标站点均无法访问：检查系统代理、防火墙或运营商网络，稍后重试。");
        } else if (!result.hasFailure && siteItem.status == NetDiag::Status::Warn) {
            result.summary = siteItem.detail;
            result.suggestion = QStringLiteral(
                "部分站点无法访问：可能是站点自身故障或运营商/防火墙拦截，"
                "可稍后重试或关闭代理后对比。");
        }
    }

    m_lastDiagResult = result;
    m_diagGeneratedAt = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm"));
    m_hasDiagReport = true;

    appendDiagHistory(result);
    renderDiagResult(result);

    appendLog("-------------------");
    for (const NetDiag::CheckItem &item : result.items) {
        if (item.status == NetDiag::Status::Skip)
            continue;
        appendLog(QStringLiteral("%1：%2").arg(item.name, item.detail));
    }

    // 定时巡检不弹窗、不自动修复，只在发现异常时发桌面通知
    if (m_silentDiag) {
        m_silentDiag = false;
        if (result.hasFailure) {
            appendLog("定时巡检发现异常，已发送桌面通知。");
            showTrayNotification(
                QStringLiteral("网络巡检发现异常"),
                QStringLiteral("%1\n\n建议：%2").arg(result.summary, result.suggestion));
        } else if (hasSiteItem && siteItem.status == NetDiag::Status::Warn) {
            appendLog("定时巡检完成：部分站点无法访问。");
            showTrayNotification(QStringLiteral("部分站点无法访问"),
                                 QStringLiteral("%1\n\n建议：%2")
                                     .arg(result.summary, result.suggestion));
        } else {
            appendLog("定时巡检通过：网络各层检查正常。");
        }
        return;
    }

    if (!result.hasFailure) {
        appendLog("-------------------");
        if (hasSiteItem && siteItem.status == NetDiag::Status::Warn) {
            appendLog("部分站点无法访问，自动检测完成");
            showMessage("部分站点无法访问",
                        QStringLiteral("%1\n\n建议：%2")
                            .arg(result.summary, result.suggestion),
                        QMessageBox::Warning);
            return;
        }
        appendLog("网络各层检查通过，自动检测完成");
        showMessage("网络连接正常",
                    "链路、路由、DNS、代理与 HTTPS 访问都正常，没有发现需要修复的问题。");
        return;
    }

    if (result.firstFailed == NetDiag::Layer::Dns) {
        appendLog("-------------------");

        // 一轮检测只自动修一次：修复后复检仍失败就停下来提示用户，
        // 避免「失败 → 修复 → 复检失败 → 再修复」的死循环。
        if (m_autoFixAttempted) {
            appendLog("自动修复后 DNS 仍不可用，已停止重试。");
            showMessage("DNS 仍无法解析",
                        "已经重置过 DNS 配置并清空缓存，但域名解析依然失败。\n\n"
                        "可能是路由器或宽带运营商的 DNS 有问题，建议重启路由器；"
                        "也可以在「DNS 切换」里改用公共 DNS 再试。",
                        QMessageBox::Warning);
            return;
        }

        m_autoFixAttempted = true;
        appendLog(QStringLiteral("DNS 解析异常：%1，正在尝试修复网络配置……")
                      .arg(result.summary));
        QTimer::singleShot(600, this, [this]() { runFixDns(true); });
        return;
    }

    showMessage("网络检查发现问题",
                QStringLiteral("%1\n\n建议：%2").arg(result.summary, result.suggestion),
                QMessageBox::Warning);
}

/** 把诊断结论渲染到网络页的「分层诊断结果」卡片 */
void MainWindow::renderDiagResult(const NetDiag::DiagResult &result)
{
    clearDiagRows();
    for (const NetDiag::CheckItem &item : result.items)
        addDiagRow(item);

    QString state = QStringLiteral("ok");
    if (result.hasFailure) {
        state = QStringLiteral("fail");
    } else {
        for (const NetDiag::CheckItem &item : result.items) {
            if (item.status == NetDiag::Status::Warn) {
                state = QStringLiteral("warn");
                break;
            }
        }
    }

    ui->netDiagSummary->setText(result.summary);
    ui->netDiagSummary->setProperty("state", state);
    ui->netDiagSummary->style()->unpolish(ui->netDiagSummary);
    ui->netDiagSummary->style()->polish(ui->netDiagSummary);
    ui->netDiagHint->setText(result.suggestion);
    ui->netDiagScroll->verticalScrollBar()->setValue(0);
}

/** 清空诊断行，只保留末尾的弹簧 */
void MainWindow::clearDiagRows()
{
    while (QLayoutItem *item = ui->netDiagRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->netDiagRowsLayout->addStretch(1);
}

/** 追加一行诊断结果：状态徽标 + 层名 + 结论 + 耗时 */
void MainWindow::addDiagRow(const NetDiag::CheckItem &item)
{
    QString badgeText;
    QString state;
    switch (item.status) {
    case NetDiag::Status::Pass: badgeText = QStringLiteral("通过"); state = QStringLiteral("pass"); break;
    case NetDiag::Status::Fail: badgeText = QStringLiteral("失败"); state = QStringLiteral("fail"); break;
    case NetDiag::Status::Warn: badgeText = QStringLiteral("注意"); state = QStringLiteral("warn"); break;
    case NetDiag::Status::Skip: badgeText = QStringLiteral("跳过"); state = QStringLiteral("skip"); break;
    }

    QFrame *row = new QFrame(ui->netDiagRows);
    row->setObjectName(QStringLiteral("netDiagRow"));
    row->setFrameShape(QFrame::NoFrame);

    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 12, 8);
    layout->setSpacing(10);

    QLabel *statusLabel = new QLabel(badgeText, row);
    statusLabel->setObjectName(QStringLiteral("netDiagStatus"));
    statusLabel->setProperty("state", state);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setFixedWidth(40);

    QLabel *nameLabel = new QLabel(item.name, row);
    nameLabel->setObjectName(QStringLiteral("netDiagName"));
    nameLabel->setFixedWidth(110);

    QLabel *detailLabel = new QLabel(item.detail, row);
    detailLabel->setObjectName(QStringLiteral("netDiagDetail"));
    detailLabel->setWordWrap(true);
    detailLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detailLabel->setToolTip(item.detail);

    QLabel *elapsedLabel = new QLabel(
        item.elapsedMs >= 0 ? QStringLiteral("%1 ms").arg(item.elapsedMs) : QString(),
        row);
    elapsedLabel->setObjectName(QStringLiteral("netDiagElapsed"));
    elapsedLabel->setFixedWidth(64);
    elapsedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    layout->addWidget(statusLabel, 0, Qt::AlignTop);
    layout->addWidget(nameLabel, 0, Qt::AlignTop);
    layout->addWidget(detailLabel, 1);
    layout->addWidget(elapsedLabel, 0, Qt::AlignTop);

    const int insertAt = qMax(0, ui->netDiagRowsLayout->count() - 1);
    ui->netDiagRowsLayout->insertWidget(insertAt, row);
}

// ---------------------------------------------------------------- 诊断历史

QString MainWindow::diagHistoryFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(dir).filePath(QStringLiteral("diag-history.json"));
}

/** 启动时载入最近 20 次诊断记录；文件缺失或损坏时静默跳过 */
void MainWindow::loadDiagHistory()
{
    m_diagHistory.clear();

    QFile file(diagHistoryFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isArray()) {
            for (const QJsonValue &value : document.array()) {
                if (m_diagHistory.size() >= 20)
                    break;
                if (!value.isObject())
                    continue;

                const QJsonObject record = value.toObject();
                DiagHistoryEntry entry;
                entry.time = record.value(QStringLiteral("time")).toString();
                entry.result.hasFailure =
                    record.value(QStringLiteral("hasFailure")).toBool();
                const int failedLayer =
                    record.value(QStringLiteral("firstFailed")).toInt();
                entry.result.firstFailed =
                    (failedLayer >= 1 && failedLayer <= 8)
                        ? static_cast<NetDiag::Layer>(failedLayer)
                        : NetDiag::Layer::App;
                entry.result.summary =
                    record.value(QStringLiteral("summary")).toString();
                entry.result.suggestion =
                    record.value(QStringLiteral("suggestion")).toString();

                for (const QJsonValue &itemValue
                     : record.value(QStringLiteral("items")).toArray()) {
                    if (!itemValue.isObject())
                        continue;
                    const QJsonObject data = itemValue.toObject();
                    NetDiag::CheckItem item;
                    const int layer =
                        data.value(QStringLiteral("layer")).toInt();
                    item.layer = (layer >= 1 && layer <= 8)
                                     ? static_cast<NetDiag::Layer>(layer)
                                     : NetDiag::Layer::Link;
                    const int status =
                        data.value(QStringLiteral("status")).toInt();
                    item.status = (status >= 0 && status <= 3)
                                      ? static_cast<NetDiag::Status>(status)
                                      : NetDiag::Status::Skip;
                    item.name = data.value(QStringLiteral("name")).toString();
                    item.detail =
                        data.value(QStringLiteral("detail")).toString();
                    item.elapsedMs =
                        data.value(QStringLiteral("elapsedMs")).toInt(-1);
                    entry.result.items.append(item);
                }

                m_diagHistory.append(entry);
            }
        }
    }

    refreshDiagHistory();
}

/** 把本次诊断结果写入本地历史并刷新列表 */
void MainWindow::appendDiagHistory(const NetDiag::DiagResult &result)
{
    DiagHistoryEntry entry;
    entry.time = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    entry.result = result;
    m_diagHistory.prepend(entry);
    while (m_diagHistory.size() > 20)
        m_diagHistory.removeLast();

    const QString path = diagHistoryFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        appendLog(QStringLiteral("诊断历史目录创建失败：%1").arg(path));
        refreshDiagHistory();
        return;
    }

    QJsonArray records;
    for (const DiagHistoryEntry &entryItem : m_diagHistory) {
        QJsonArray items;
        for (const NetDiag::CheckItem &check : entryItem.result.items) {
            QJsonObject data;
            data.insert(QStringLiteral("layer"), static_cast<int>(check.layer));
            data.insert(QStringLiteral("name"), check.name);
            data.insert(QStringLiteral("status"), static_cast<int>(check.status));
            data.insert(QStringLiteral("detail"), check.detail);
            data.insert(QStringLiteral("elapsedMs"), check.elapsedMs);
            items.append(data);
        }

        QJsonObject record;
        record.insert(QStringLiteral("time"), entryItem.time);
        record.insert(QStringLiteral("hasFailure"), entryItem.result.hasFailure);
        record.insert(QStringLiteral("firstFailed"),
                      static_cast<int>(entryItem.result.firstFailed));
        record.insert(QStringLiteral("summary"), entryItem.result.summary);
        record.insert(QStringLiteral("suggestion"), entryItem.result.suggestion);
        record.insert(QStringLiteral("items"), items);
        records.append(record);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(QStringLiteral("诊断历史写入失败：%1").arg(path));
        refreshDiagHistory();
        return;
    }
    file.write(QJsonDocument(records).toJson(QJsonDocument::Compact));

    refreshDiagHistory();
}

/** 重建「诊断历史」卡片：时间 + 结论 + 与上一次的对比 */
void MainWindow::refreshDiagHistory()
{
    while (QLayoutItem *item = ui->diagHistoryRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->diagHistoryRowsLayout->addStretch(1);

    if (m_diagHistory.isEmpty()) {
        ui->cardDiagHistory->setVisible(false);
        return;
    }

    ui->cardDiagHistory->setVisible(true);
    ui->diagHistorySummary->setText(
        QStringLiteral("共 %1 条记录 · 最近一次 %2")
            .arg(m_diagHistory.size())
            .arg(m_diagHistory.first().time));

    for (int index = 0; index < m_diagHistory.size(); ++index) {
        const DiagHistoryEntry &entry = m_diagHistory.at(index);

        QString state = QStringLiteral("ok");
        QString stateText = QStringLiteral("正常");
        if (entry.result.hasFailure) {
            state = QStringLiteral("fail");
            stateText = QStringLiteral("失败");
        } else {
            for (const NetDiag::CheckItem &item : entry.result.items) {
                if (item.status == NetDiag::Status::Warn) {
                    state = QStringLiteral("warn");
                    stateText = QStringLiteral("注意");
                    break;
                }
            }
        }

        QString compareText = QStringLiteral("首次诊断记录");
        if (index + 1 < m_diagHistory.size()) {
            const NetDiag::DiagResult &older = m_diagHistory.at(index + 1).result;
            const QString nowLayer =
                entry.result.hasFailure
                    ? NetDiag::layerLabel(entry.result.firstFailed)
                    : QStringLiteral("全部通过");
            const QString oldLayer =
                older.hasFailure
                    ? NetDiag::layerLabel(older.firstFailed)
                    : QStringLiteral("全部通过");

            if (!entry.result.hasFailure && !older.hasFailure) {
                compareText = QStringLiteral("与上次一致：各层通过");
            } else if (entry.result.hasFailure && older.hasFailure
                       && entry.result.firstFailed == older.firstFailed) {
                compareText = QStringLiteral("与上次相同：%1 仍有问题").arg(nowLayer);
            } else if (!entry.result.hasFailure) {
                compareText = QStringLiteral("较上次好转：%1 → 全部通过").arg(oldLayer);
            } else if (!older.hasFailure) {
                compareText = QStringLiteral("较上次变化：全部通过 → %1").arg(nowLayer);
            } else {
                compareText = QStringLiteral("较上次变化：%1 → %2")
                                  .arg(oldLayer, nowLayer);
            }
        }

        QFrame *row = new QFrame(ui->diagHistoryRows);
        row->setObjectName(QStringLiteral("diagHistoryRow"));
        row->setFrameShape(QFrame::NoFrame);

        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(10);

        QLabel *timeLabel = new QLabel(entry.time, row);
        timeLabel->setObjectName(QStringLiteral("diagHistoryTime"));
        timeLabel->setFixedWidth(144);

        QVBoxLayout *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        QLabel *summaryLabel = new QLabel(entry.result.summary, row);
        summaryLabel->setObjectName(QStringLiteral("diagHistoryText"));
        summaryLabel->setWordWrap(true);
        summaryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        summaryLabel->setToolTip(entry.result.summary);

        QLabel *compareLabel = new QLabel(compareText, row);
        compareLabel->setObjectName(QStringLiteral("diagHistoryCompare"));
        compareLabel->setWordWrap(true);

        textLayout->addWidget(summaryLabel);
        textLayout->addWidget(compareLabel);

        QLabel *stateLabel = new QLabel(stateText, row);
        stateLabel->setObjectName(QStringLiteral("diagHistoryState"));
        stateLabel->setProperty("state", state);
        stateLabel->setAlignment(Qt::AlignCenter);
        stateLabel->setFixedWidth(44);

        QPushButton *viewButton = new QPushButton(QStringLiteral("查看"), row);
        viewButton->setObjectName(QStringLiteral("diagHistoryViewBtn"));
        viewButton->setCursor(Qt::PointingHandCursor);
        viewButton->setFixedSize(56, 28);
        connect(viewButton, &QPushButton::clicked, this,
                [this, index]() { showDiagHistoryEntry(index); });

        layout->addWidget(timeLabel, 0, Qt::AlignTop);
        layout->addLayout(textLayout, 1);
        layout->addWidget(stateLabel, 0, Qt::AlignTop);
        layout->addWidget(viewButton, 0, Qt::AlignTop);

        const int insertAt = qMax(0, ui->diagHistoryRowsLayout->count() - 1);
        ui->diagHistoryRowsLayout->insertWidget(insertAt, row);
    }
}

/** 把某条历史记录重新渲染到「分层诊断结果」，不影响后续新检测 */
void MainWindow::showDiagHistoryEntry(int index)
{
    if (index < 0 || index >= m_diagHistory.size())
        return;

    const DiagHistoryEntry &entry = m_diagHistory.at(index);
    m_lastDiagResult = entry.result;
    m_diagGeneratedAt = entry.time.left(16);
    m_hasDiagReport = true;
    renderDiagResult(entry.result);

    appendLog(QStringLiteral("已载入 %1 的诊断记录").arg(entry.time));
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
        appendLog("网站请求超时，请检查网络是否连接、是否开着代理");
        showMessage("网络连不上",
                    "打开百度超时了。\n\n"
                    "请先确认电脑已经连上网络，并关掉系统里的代理设置后再试一次，"
                    "也可以点「刷新网络设置」重置 DNS。",
                    QMessageBox::Warning);
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
    showMessage("网络连接正常",
                "已通过连通性与 DNS 解析检查，没有发现需要修复的问题。");
}

/**
 * 执行 DNS 修复脚本。
 *
 * fixdns.ps1 会自行申请管理员权限，并用 -Wait 等待提权后的实例执行完毕，
 * 因此这里的 finished 信号就代表修复真正结束——不再靠固定延时猜测。
 *
 * @param askAfter true 表示自动检测触发的修复，成功后自动复检网络；
 *                 false 表示用户手动刷新，仅提示结果。
 */
void MainWindow::runFixDns(bool askAfter)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/fixdns.ps1"));
    if (!QFileInfo::exists(script)) {
        showMessage("缺少脚本文件",
                    "未找到所需的脚本文件 fixdns.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("刷新网络配置")))
        return;

    appendLog("-------------------");
    appendLog("正在刷新网络配置，请稍候……");
    appendLog("-------------------");

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = askAfter ? Task::FixDnsAuto : Task::FixDnsManual;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

/**
 * 解析 fixdns.ps1 的结果并按实际情况反馈。
 *
 * 脚本输出一行 FIXDNS_JSON:，其中 target 是实际被处理的网卡名；失败时
 * error 给出错误代码（提权取消、没有物理网卡等），由本函数翻译成中文。
 * 自动检测触发的修复成功后会直接再跑一次分层诊断，用结果验证修复效果，
 * 而不是让用户自己判断网页能不能打开。
 */
void MainWindow::handleFixDnsResult(bool askAfter)
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("FIXDNS_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    appendLog("-------------------");
    if (payload.isEmpty() || parseError.error != QJsonParseError::NoError
        || !doc.isObject()) {
        appendLog("没有读到修复结果，脚本可能提前退出。");
        recordOperation(QStringLiteral("网络修复"),
                        QStringLiteral("刷新网络配置（改回自动获取）"),
                        QStringLiteral("当前默认网卡"), false);
        showMessage("修复结果未知",
                    "网络配置修复没有返回可识别的结果。\n\n"
                    "如果刚才取消了管理员授权弹窗，重试并选择「是」即可。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject obj = doc.object();
    const bool ok = obj.value(QStringLiteral("ok")).toBool();
    const QString error = obj.value(QStringLiteral("error")).toString();
    const QString target = obj.value(QStringLiteral("target")).toString();
    const QString dhcp = obj.value(QStringLiteral("dhcp")).toString();
    const QString backup = obj.value(QStringLiteral("backup")).toString();
    const QString targetText =
        target.isEmpty() ? QStringLiteral("当前默认网卡") : target;

    // ConvertTo-Json 会把只有一项的数组写成字符串，两种形式都要认
    const QJsonValue actionsValue = obj.value(QStringLiteral("actions"));
    QStringList actionCodes;
    if (actionsValue.isArray()) {
        for (const QJsonValue &value : actionsValue.toArray())
            actionCodes.append(value.toString());
    } else if (actionsValue.isString()) {
        actionCodes.append(actionsValue.toString());
    }
    for (const QString &code : actionCodes) {
        const QString label = fixDnsActionLabel(code);
        if (!label.isEmpty())
            appendLog(label);
    }

    recordOperation(QStringLiteral("网络修复"),
                    QStringLiteral("刷新网络配置（改回自动获取）"),
                    targetText, ok);

    if (!ok) {
        appendLog(QStringLiteral("修复未完成：%1").arg(fixDnsErrorLabel(error)));
        showMessage("网络配置修复未完成", fixDnsErrorLabel(error),
                    QMessageBox::Critical);
        return;
    }

    if (dhcp == QLatin1String("Disabled"))
        appendLog("该网卡使用静态 IP，已保持原设置不变");
    appendLog(QStringLiteral("已重置 %1 的 DNS 配置").arg(targetText));
    if (!backup.isEmpty())
        appendLog(QStringLiteral("原配置已备份到 %1").arg(backup));

    if (askAfter) {
        appendLog("正在复检修复后的网络状态……");
        ui->netDiagHint->setText(
            QStringLiteral("修复完成，正在复检各层状态"));
        QTimer::singleShot(600, this, [this]() { startAutoCheck(); });
    } else {
        showMessage("网络设置已刷新",
                    "已经把 DNS 改回「自动获取」，并清空了本机的 DNS 缓存。\n\n"
                    "如果还是上不了网，重启电脑后再试一次。");
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

/**
 * 展示 ipconfig /all 的完整结果。
 *
 * 运行日志面板默认隐藏，这类纯文本结果改为可滚动、可复制的弹窗呈现，
 * 避免功能因为没有输出区域而失去反馈。
 */
void MainWindow::handleNetworkInfo()
{
    const QString output = m_runner->collectedOutput().trimmed();

    if (output.isEmpty()) {
        appendLog("-------------------");
        appendLog("网络信息获取失败：没有任何输出。");
        showMessage("获取网络信息失败",
                    "未能读取到网络配置信息，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    appendLog("-------------------");
    appendLog("网络信息获取完成");

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("网络信息"));
    dialog.setWindowIcon(windowIcon());
    dialog.setMinimumSize(760, 540);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);

    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setPlainText(output);
    view->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background:#151D2E; border:1px solid #0D1422;"
        " border-radius:10px; color:#DCE4F0; padding:10px;"
        " font-family:\"Consolas\",\"Courier New\",monospace; font-size:12px; }"));
    layout->addWidget(view, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
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
    connect(ui->btn_exportHealth, &QPushButton::clicked,
            this, &MainWindow::exportHealthReport);

    // 系统文件修复（SFC / DISM）：点磁贴展开卡片，卡片内两个按钮启动修复
    connect(ui->btn_sfc, &QAbstractButton::clicked,
            this, &MainWindow::toggleSfcCard);
    connect(ui->btnSfcScan, &QPushButton::clicked, this, [this]() {
        runSfcRepair(QStringLiteral("sfc"));
    });
    connect(ui->btnDismRepair, &QPushButton::clicked, this, [this]() {
        runSfcRepair(QStringLiteral("dism"));
    });

    // Windows 更新修复：点磁贴展开/收起卡片，卡片内按钮启动重置
    connect(ui->btn_wupdate, &QAbstractButton::clicked, this, [this]() {
        if (m_task == Task::WUpdateRepair) {
            showSystemResultCard(ui->cardWUpdate);
            return;
        }
        if (ui->cardWUpdate->isVisible()) {
            ui->cardWUpdate->setVisible(false);
            return;
        }
        showSystemResultCard(ui->cardWUpdate);
    });
    connect(ui->btnWUpdateFix, &QPushButton::clicked,
            this, &MainWindow::runWUpdateFix);

    // 清理 Windows 更新组件（DISM StartComponentCleanup）
    connect(ui->btn_sxsClean, &QAbstractButton::clicked,
            this, &MainWindow::runSxsCleanup);

    // 系统还原点：点磁贴展开卡片并读取列表
    ui->restoreRowsLayout->setAlignment(Qt::AlignTop);
    connect(ui->btn_restore, &QAbstractButton::clicked, this, [this]() {
        if (m_task == Task::RestoreList || m_task == Task::RestoreCreate) {
            showSystemResultCard(ui->cardRestore);
            return;
        }
        runRestorePointList();
    });
    connect(ui->restoreCreateBtn, &QPushButton::clicked,
            this, &MainWindow::runRestorePointCreate);
    connect(ui->restoreOpenBtn, &QPushButton::clicked, this, [this]() {
        if (!confirmAction(
                "打开系统还原",
                QStringLiteral("接下来会打开 Windows 自带的系统还原窗口，"
                               "可以选择一个还原点把系统恢复到之前的状态。\n\n"
                               "系统还原不会影响个人文件，但会还原当时之后的软件安装和设置。\n\n"
                               "继续吗？")))
            return;

        // rstrui.exe 会由系统自行弹出 UAC 请求
        if (!QProcess::startDetached(QStringLiteral("rstrui.exe"))) {
            showMessage("无法打开系统还原",
                        "未能启动 rstrui.exe。\n\n"
                        "可以在开始菜单搜索「创建还原点」手动打开系统属性中的系统保护。",
                        QMessageBox::Warning);
        }
    });

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

    if (!claimTask(QStringLiteral("系统体检")))
        return;

    ++m_flow;

    clearLog();
    appendLog("正在采集系统信息，请稍候……");

    // 清空上一次的体检结果
    m_healthItems.clear();
    m_healthSummaryText.clear();
    m_healthScore = 0;
    m_healthGrade.clear();
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
    m_healthSummaryText = summary;
    ui->healthSummary->setText(summary);
    ui->healthSummary->setProperty("state", summaryState);
    ui->healthSummary->style()->unpolish(ui->healthSummary);
    ui->healthSummary->style()->polish(ui->healthSummary);

    appendLog("-------------------");
    appendLog(summary);

    updateHealthScore(warnCount, riskCount);
}

void MainWindow::updateHealthScore(int warnCount, int riskCount)
{
    // 每项「注意」扣 18 分，「建议」扣 8 分，最低 0 分
    int score = 100 - riskCount * 18 - warnCount * 8;
    score = qMax(0, qMin(100, score));

    QString grade;
    QString state;
    if (score >= 90) {
        grade = QStringLiteral("优秀");
        state = QStringLiteral("ok");
    } else if (score >= 75) {
        grade = QStringLiteral("良好");
        state = QStringLiteral("info");
    } else if (score >= 60) {
        grade = QStringLiteral("一般");
        state = QStringLiteral("warn");
    } else {
        grade = QStringLiteral("较差");
        state = QStringLiteral("bad");
    }

    ui->overviewScoreValue->setText(QString::number(score));
    ui->overviewScoreGrade->setText(
        riskCount > 0 ? QStringLiteral("%1 · %2 项注意").arg(grade).arg(riskCount)
                      : QStringLiteral("%1 · %2 项建议").arg(grade).arg(warnCount));

    m_healthScore = score;
    m_healthGrade = grade;

    for (QLabel *label : {ui->overviewScoreValue, ui->overviewScoreGrade}) {
        label->setProperty("state", state);
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
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
    m_healthItems.append({state, name, detail});
}

void MainWindow::exportHealthReport()
{
    if (m_healthItems.isEmpty()) {
        showMessage("还没有体检结果",
                    "请先点「电脑体检」，等结果出来后就能导出报告了。",
                    QMessageBox::Information);
        return;
    }

    const QString defaultName = QStringLiteral("电脑体检报告_%1.html")
                                    .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd")));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存体检报告"),
        QDir(QDir::homePath()).filePath(defaultName),
        QStringLiteral("网页文件 (*.html)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive))
        path += QStringLiteral(".html");

    const QString nowText = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm"));
    const QString computer = qEnvironmentVariable("COMPUTERNAME");

    QString scoreClass = QStringLiteral("ok");
    if (m_healthScore < 60)
        scoreClass = QStringLiteral("bad");
    else if (m_healthScore < 75)
        scoreClass = QStringLiteral("warn");
    else if (m_healthScore < 90)
        scoreClass = QStringLiteral("info");

    QString rows;
    for (const HealthItem &item : m_healthItems) {
        const QString badge = item.state == QLatin1String("risk") ? QStringLiteral("注意")
                            : item.state == QLatin1String("warn") ? QStringLiteral("建议")
                                                                  : QStringLiteral("通过");
        const QString cls = item.state == QLatin1String("risk") ? QStringLiteral("risk")
                          : item.state == QLatin1String("warn") ? QStringLiteral("warn")
                                                                : QStringLiteral("ok");
        rows += QStringLiteral(
                    "<tr><td><span class=\"badge %1\">%2</span></td>"
                    "<td class=\"name\">%3</td><td class=\"detail\">%4</td></tr>\n")
                    .arg(cls, badge, escapeHtml(item.name), escapeHtml(item.detail));
    }

    QString html;
    html += QStringLiteral(
        "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<title>电脑体检报告</title>\n<style>\n"
        "body{margin:0;background:#F2F5FA;color:#1E2A3B;"
        "font-family:'Microsoft YaHei','PingFang SC',Arial,sans-serif;}\n"
        ".page{max-width:860px;margin:32px auto;padding:32px 36px;background:#FFFFFF;"
        "border-radius:16px;box-shadow:0 8px 28px rgba(28,46,80,.10);}\n"
        "h1{margin:0 0 6px;font-size:24px;}\n"
        ".meta{margin:0 0 22px;color:#93A0B4;font-size:12px;}\n"
        ".score{display:flex;align-items:baseline;gap:10px;padding:16px 22px;"
        "border-radius:12px;margin-bottom:16px;}\n"
        ".score.ok{background:#E8F7EE;}.score.info{background:#EEF4FE;}"
        ".score.warn{background:#FFF6E5;}.score.bad{background:#FDECEC;}\n"
        ".score-num{font-size:40px;font-weight:700;}\n"
        ".score.ok .score-num{color:#1F8A4C;}.score.info .score-num{color:#2F6FDE;}"
        ".score.warn .score-num{color:#B7791F;}.score.bad .score-num{color:#C0392B;}\n"
        ".score-unit{font-size:14px;color:#6B7A93;}\n"
        ".score-grade{margin-left:auto;font-size:15px;font-weight:600;}\n"
        ".score.ok .score-grade{color:#1F8A4C;}.score.info .score-grade{color:#2F6FDE;}"
        ".score.warn .score-grade{color:#B7791F;}.score.bad .score-grade{color:#C0392B;}\n"
        ".summary{font-size:13px;color:#4A576F;margin:0 0 20px;}\n"
        "table{width:100%;border-collapse:collapse;font-size:13px;}\n"
        "th{text-align:left;padding:10px 12px;background:#F7F9FD;color:#5B6980;"
        "font-weight:600;border-bottom:1px solid #E6ECF6;}\n"
        "td{padding:12px;border-bottom:1px solid #EEF1F6;vertical-align:top;}\n"
        ".badge{display:inline-block;padding:3px 8px;border-radius:8px;font-size:11px;"
        "font-weight:600;white-space:nowrap;}\n"
        ".badge.ok{background:#E8F7EE;color:#1F8A4C;}"
        ".badge.warn{background:#FFF6E5;color:#B7791F;}"
        ".badge.risk{background:#FDECEC;color:#C0392B;}\n"
        ".name{font-weight:600;white-space:nowrap;}\n"
        ".detail{color:#7A879B;font-size:12px;}\n"
        ".footer{margin-top:22px;color:#A6B2C4;font-size:11px;line-height:1.7;}\n"
        "</style>\n</head>\n<body>\n<div class=\"page\">\n"
        "<h1>电脑体检报告</h1>\n"
        "<p class=\"meta\">生成时间：%1　·　电脑名称：%2　·　WindowsTool 电脑工具箱</p>\n"
        "<div class=\"score %3\"><span class=\"score-num\">%4</span>"
        "<span class=\"score-unit\">分</span><span class=\"score-grade\">%5</span></div>\n"
        "<p class=\"summary\">%6</p>\n"
        "<table>\n<thead><tr><th style=\"width:64px\">结果</th>"
        "<th style=\"width:110px\">检查项</th><th>说明</th></tr></thead>\n<tbody>\n")
            .arg(nowText, escapeHtml(computer), scoreClass,
                 QString::number(m_healthScore), escapeHtml(m_healthGrade),
                 escapeHtml(m_healthSummaryText));
    html += rows;
    html += QStringLiteral(
        "</tbody>\n</table>\n"
        "<p class=\"footer\">本报告由 WindowsTool 自动生成，仅供参考。"
        "若某项显示「注意」，建议按说明处理后重新体检一次。</p>\n"
        "</div>\n</body>\n</html>\n");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showMessage("报告保存失败",
                    QStringLiteral("无法写入文件：%1\n\n请换一个保存位置再试。").arg(path),
                    QMessageBox::Warning);
        return;
    }
    file.write(html.toUtf8());
    file.close();

    appendLog("-------------------");
    appendLog(QStringLiteral("体检报告已导出：%1").arg(path));
    showMessage("报告已导出",
                QStringLiteral("体检报告已保存到：\n%1\n\n"
                               "用浏览器打开就能查看，也可以发给别人。").arg(path));
}

void MainWindow::exportDiagReport()
{
    if (!m_hasDiagReport || m_lastDiagResult.items.isEmpty()) {
        showMessage("还没有诊断结果",
                    "请先点「一键检测修复」，等分层结果出来后就能导出报告了。",
                    QMessageBox::Information);
        return;
    }

    const QString defaultName = QStringLiteral("网络诊断报告_%1.html")
                                    .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd_HHmm")));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存诊断报告"),
        QDir(QDir::homePath()).filePath(defaultName),
        QStringLiteral("网页文件 (*.html)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive))
        path += QStringLiteral(".html");

    int failed = 0;
    int warned = 0;
    for (const NetDiag::CheckItem &item : m_lastDiagResult.items) {
        if (item.status == NetDiag::Status::Fail)
            ++failed;
        else if (item.status == NetDiag::Status::Warn)
            ++warned;
    }

    QString stateClass = QStringLiteral("ok");
    QString stateText = QStringLiteral("各层检查通过");
    if (m_lastDiagResult.hasFailure) {
        stateClass = QStringLiteral("risk");
        stateText = QStringLiteral("发现 %1 个问题").arg(failed);
    } else if (warned > 0) {
        stateClass = QStringLiteral("warn");
        stateText = QStringLiteral("%1 项提示").arg(warned);
    }

    QString rows;
    for (const NetDiag::CheckItem &item : m_lastDiagResult.items) {
        QString cls;
        QString badge;
        switch (item.status) {
        case NetDiag::Status::Pass:
            cls = QStringLiteral("ok");
            badge = QStringLiteral("通过");
            break;
        case NetDiag::Status::Warn:
            cls = QStringLiteral("warn");
            badge = QStringLiteral("建议");
            break;
        case NetDiag::Status::Fail:
            cls = QStringLiteral("risk");
            badge = QStringLiteral("未通过");
            break;
        case NetDiag::Status::Skip:
            cls = QStringLiteral("skip");
            badge = QStringLiteral("跳过");
            break;
        }

        const QString elapsed = item.elapsedMs >= 0
                                    ? QStringLiteral("%1 ms").arg(item.elapsedMs)
                                    : QStringLiteral("—");
        rows += QStringLiteral(
                    "<tr><td><span class=\"badge %1\">%2</span></td>"
                    "<td class=\"layer\">%3</td><td class=\"name\">%4</td>"
                    "<td class=\"mono\">%5</td><td class=\"detail\">%6</td></tr>\n")
                    .arg(cls, badge,
                         escapeHtml(NetDiag::layerLabel(item.layer)),
                         escapeHtml(item.name), elapsed,
                         escapeHtml(item.detail));
    }

    const QString computer = qEnvironmentVariable("COMPUTERNAME");

    QString html;
    html += QStringLiteral(
        "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<title>网络诊断报告</title>\n<style>\n"
        "body{margin:0;background:#F2F5FA;color:#1E2A3B;"
        "font-family:'Microsoft YaHei','PingFang SC',Arial,sans-serif;}\n"
        ".page{max-width:900px;margin:32px auto;padding:32px 36px;background:#FFFFFF;"
        "border-radius:16px;box-shadow:0 8px 28px rgba(28,46,80,.10);}\n"
        "h1{margin:0 0 6px;font-size:24px;}\n"
        ".meta{margin:0 0 22px;color:#93A0B4;font-size:12px;}\n"
        ".result{display:flex;align-items:center;gap:12px;padding:14px 18px;"
        "border-radius:12px;margin-bottom:14px;}\n"
        ".result.ok{background:#E8F7EE;}.result.warn{background:#FFF6E5;}"
        ".result.risk{background:#FDECEC;}\n"
        ".result-badge{background:#FFFFFF;border-radius:8px;padding:4px 10px;"
        "font-size:12px;font-weight:600;color:#4A576F;}\n"
        ".result.ok .result-badge{color:#1F8A4C;}"
        ".result.warn .result-badge{color:#B7791F;}"
        ".result.risk .result-badge{color:#C0392B;}\n"
        ".result-text{font-size:14px;font-weight:600;}\n"
        ".summary{font-size:13px;color:#4A576F;margin:0 0 12px;}\n"
        ".suggestion{font-size:13px;color:#4A576F;background:#F7F9FD;"
        "border-radius:10px;padding:12px 16px;margin:0 0 20px;}\n"
        "table{width:100%;border-collapse:collapse;font-size:13px;}\n"
        "th{text-align:left;padding:10px 12px;background:#F7F9FD;color:#5B6980;"
        "font-weight:600;border-bottom:1px solid #E6ECF6;}\n"
        "td{padding:12px;border-bottom:1px solid #EEF1F6;vertical-align:top;}\n"
        ".badge{display:inline-block;padding:3px 8px;border-radius:8px;font-size:11px;"
        "font-weight:600;white-space:nowrap;}\n"
        ".badge.ok{background:#E8F7EE;color:#1F8A4C;}"
        ".badge.warn{background:#FFF6E5;color:#B7791F;}"
        ".badge.risk{background:#FDECEC;color:#C0392B;}"
        ".badge.skip{background:#EEF1F6;color:#8B97A8;}\n"
        ".layer{color:#7A879B;font-size:12px;white-space:nowrap;}\n"
        ".name{font-weight:600;white-space:nowrap;}\n"
        ".mono{color:#7A879B;font-size:12px;white-space:nowrap;}\n"
        ".detail{color:#7A879B;font-size:12px;}\n"
        ".footer{margin-top:22px;color:#A6B2C4;font-size:11px;line-height:1.7;}\n"
        "</style>\n</head>\n<body>\n<div class=\"page\">\n"
        "<h1>网络诊断报告</h1>\n"
        "<p class=\"meta\">生成时间：%1　·　电脑名称：%2　·　WindowsTool 网络工具箱</p>\n"
        "<div class=\"result %3\"><span class=\"result-badge\">%4</span>"
        "<span class=\"result-text\">%5</span></div>\n"
        "<p class=\"summary\">%6</p>\n")
            .arg(m_diagGeneratedAt, escapeHtml(computer), stateClass, stateText,
                 escapeHtml(m_lastDiagResult.summary));

    if (!m_lastDiagResult.suggestion.isEmpty()) {
        html += QStringLiteral("<p class=\"suggestion\">建议：%1</p>\n")
                    .arg(escapeHtml(m_lastDiagResult.suggestion));
    }

    html += QStringLiteral(
        "<table>\n<thead><tr><th style=\"width:64px\">结果</th>"
        "<th style=\"width:76px\">层级</th><th style=\"width:110px\">检查项</th>"
        "<th style=\"width:70px\">耗时</th><th>说明</th></tr></thead>\n<tbody>\n");
    html += rows;
    html += QStringLiteral(
        "</tbody>\n</table>\n"
        "<p class=\"footer\">本报告由 WindowsTool 的只读网络诊断生成，"
        "仅反映检测时刻的网络状态；重新检测后旧结论可能不再适用。</p>\n"
        "</div>\n</body>\n</html>\n");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showMessage("报告保存失败",
                    QStringLiteral("无法写入文件：%1\n\n请换一个保存位置再试。").arg(path),
                    QMessageBox::Warning);
        return;
    }
    file.write(html.toUtf8());
    file.close();

    appendLog("-------------------");
    appendLog(QStringLiteral("诊断报告已导出：%1").arg(path));
    showMessage("报告已导出",
                QStringLiteral("网络诊断报告已保存到：\n%1\n\n"
                               "用浏览器打开就能查看，也可以发给别人。").arg(path));
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

    if (!claimTask(QStringLiteral("读取开机启动项")))
        return;

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
    int disabledCount = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString command = object.value(QStringLiteral("command")).toString();
        if (name.isEmpty() && command.isEmpty())
            continue;

        const QString sourceCode = object.value(QStringLiteral("source")).toString();
        const bool disabled = object.value(QStringLiteral("disabled")).toBool();
        addStartupRow(startupSourceLabel(sourceCode), name, command, sourceCode, disabled);
        ++total;
        if (disabled)
            ++disabledCount;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现开机启动项");
    else if (disabledCount > 0)
        summary = QStringLiteral("共 %1 项开机启动 · 已关掉 %2 项")
                      .arg(total).arg(disabledCount);
    else
        summary = QStringLiteral("共 %1 项开机启动 · 不需要的可以点「禁用」").arg(total);

    ui->startupSummary->setText(summary);
    ui->startupScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::runStartupToggle(const QString &action, const QString &source,
                                  const QString &name, const QString &command)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/startupToggle.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少启动项脚本",
                    "未找到 ps1\\startupToggle.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("修改开机启动项")))
        return;

    m_startupAction = action;
    m_startupSource = source;
    m_startupName = name;
    m_startupCommand = command;

    ++m_flow;

    clearLog();
    appendLog(action == QLatin1String("disable")
                  ? QStringLiteral("正在关闭「%1」的开机启动……").arg(name)
                  : QStringLiteral("正在恢复「%1」的开机启动……").arg(name));

    ui->startupSummary->setText(action == QLatin1String("disable")
                                    ? QStringLiteral("正在关闭……")
                                    : QStringLiteral("正在启用……"));

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), action,
        QStringLiteral("-Source"), source,
        QStringLiteral("-Name"), name,
        QStringLiteral("-Command"), command
    };

    m_task = Task::StartupToggle;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleStartupToggle()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("STARTUP_TOGGLE_JSON:");
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
        appendLog("修改启动项失败：未能解析脚本输出。");
        ui->startupSummary->setText(QStringLiteral("修改失败"));
        showMessage("修改启动项失败",
                    "没能完成这次修改，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("ok")).toBool()) {
        if (!m_undoId.isEmpty()) {
            const QString undoId = m_undoId;
            m_undoId.clear();
            m_undoKind.clear();
            markOperationUndone(undoId);
            refreshOperationPage();
            showMessage("已撤销",
                        QStringLiteral("启动项已恢复到操作前的状态，正在刷新列表。"));
        } else {
            QJsonObject undoArgs;
            undoArgs.insert(QStringLiteral("action"),
                            m_startupAction == QLatin1String("disable")
                                ? QStringLiteral("enable")
                                : QStringLiteral("disable"));
            undoArgs.insert(QStringLiteral("source"), m_startupSource);
            undoArgs.insert(QStringLiteral("name"), m_startupName);
            undoArgs.insert(QStringLiteral("command"), m_startupCommand);

            const QString display = m_startupName.isEmpty() ? m_startupCommand
                                                            : m_startupName;
            recordOperation(
                QStringLiteral("开机启动项"),
                m_startupAction == QLatin1String("disable")
                    ? QStringLiteral("禁用开机启动")
                    : QStringLiteral("启用开机启动"),
                display, true, true, QStringLiteral("startup"), undoArgs);
        }

        // 等本次任务状态复位后再重新读列表，避免再次触发并发提示
        QTimer::singleShot(0, this, &MainWindow::runStartupList);
        return;
    }

    const bool undoMode = !m_undoId.isEmpty();
    m_undoId.clear();
    m_undoKind.clear();

    const QString message = object.value(QStringLiteral("message")).toString();
    QString reason;
    if (message == QLatin1String("item-missing"))
        reason = QStringLiteral("找不到这个启动项，可能刚被别的软件删掉了。");
    else if (message == QLatin1String("record-missing"))
        reason = QStringLiteral("没有找到之前的关闭记录，可能被手动改过。");
    else if (message == QLatin1String("access-denied"))
        reason = QStringLiteral("没有权限修改。\n\n"
                                "「所有用户」的启动项需要管理员权限："
                                "请右键本程序，选择「以管理员身份运行」后再试。");
    else
        reason = QStringLiteral("修改没有成功。\n\n"
                                "如果这一项属于「所有用户」，"
                                "请右键本程序，选择「以管理员身份运行」后再试。");

    ui->startupSummary->setText(QStringLiteral("修改失败"));
    appendLog("-------------------");
    appendLog(QStringLiteral("修改启动项失败：%1").arg(message));
    showMessage(undoMode ? "撤销失败" : "修改启动项失败", reason, QMessageBox::Warning);
}

void MainWindow::addStartupRow(const QString &source, const QString &name,
                               const QString &command, const QString &sourceCode,
                               bool disabled)
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
    title->setWordWrap(true);

    QLabel *desc = new QLabel(command, row);
    desc->setObjectName(QStringLiteral("startupCommand"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    const QString displayName = name.isEmpty() ? QStringLiteral("(未命名)") : name;
    QPushButton *toggle = new QPushButton(disabled ? QStringLiteral("启用")
                                                   : QStringLiteral("禁用"), row);
    toggle->setObjectName(QStringLiteral("startupToggleBtn"));
    toggle->setProperty("disabledState", disabled);
    toggle->setFixedWidth(56);
    toggle->setMinimumHeight(26);
    toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toggle->setCursor(Qt::PointingHandCursor);

    const QString action = disabled ? QStringLiteral("enable") : QStringLiteral("disable");
    connect(toggle, &QPushButton::clicked, this,
            [this, action, sourceCode, name, command, displayName, disabled]() {
                if (!disabled) {
                    if (!confirmAction("关闭开机启动",
                                       QStringLiteral("关闭后，「%1」开机时不会再自动运行，"
                                                      "随时可以点「启用」恢复。\n\n继续吗？")
                                           .arg(displayName))) {
                        return;
                    }
                }
                runStartupToggle(action, sourceCode, name, command);
            });

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);
    layout->addWidget(toggle, 0, Qt::AlignTop);

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

    if (!claimTask(QStringLiteral("检测端口占用")))
        return;

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
    ui->cardSfc->setVisible(card == ui->cardSfc);
    ui->cardWUpdate->setVisible(card == ui->cardWUpdate);
    ui->cardRestore->setVisible(card == ui->cardRestore);
    ui->cardTreemap->setVisible(card == ui->cardTreemap);

    // 结果卡片分散在不同功能页，取卡片所在页面并滚入视野
    QScrollArea *page = ui->pageSystem;
    if (card == ui->cardStartup || card == ui->cardProcesses
        || card == ui->cardUninstall || card == ui->cardDrivers)
        page = ui->pageManage;
    else if (card == ui->cardLargeFiles || card == ui->cardDiskUsage
             || card == ui->cardDup || card == ui->cardTreemap)
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

    if (!claimTask(QStringLiteral("扫描大文件")))
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

    if (!claimTask(QStringLiteral("扫描重复文件")))
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

    if (!claimTask(QStringLiteral("删除重复文件")))
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

    if (deleted > 0) {
        recordOperation(
            QStringLiteral("重复文件"),
            QStringLiteral("删除 %1 个重复副本").arg(deleted),
            QDir::toNativeSeparators(m_dupRoot), true);
    }

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

    if (!claimTask(QStringLiteral("检测磁盘健康")))
        return;

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

    if (!claimTask(QStringLiteral("读取硬件信息")))
        return;

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

    if (!claimTask(QStringLiteral("读取电池信息")))
        return;

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

    if (!claimTask(QStringLiteral("读取进程列表")))
        return;

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

    if (!claimTask(QStringLiteral("统计磁盘占用")))
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

// ---------------------------------------------------------------- 目录空间树图

void MainWindow::runTreemap(const QString &root)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/treemap.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少统计脚本",
                    "未找到 ps1\\treemap.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }
    if (root.isEmpty())
        return;

    if (!claimTask(QStringLiteral("C 盘空间图")))
        return;

    ++m_flow;

    m_treemapRoot = root;
    clearLog();
    appendLog(QStringLiteral("正在统计 %1 的空间占用，目录越大耗时越久，请稍候……")
                  .arg(QDir::toNativeSeparators(root)));

    ui->treemapSummary->setText(QStringLiteral("正在统计……（目录越大耗时越久）"));
    if (m_treemap)
        m_treemap->showPlaceholder(QStringLiteral("正在统计……"));
    updateTreemapPathButtons();
    showSystemResultCard(ui->cardTreemap);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Root"), QDir::toNativeSeparators(root)
    };

    m_task = Task::Treemap;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleTreemap()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("TREEMAP_JSON:");
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
        appendLog("统计空间占用失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        ui->treemapSummary->setText(QStringLiteral("统计失败"));
        if (m_treemap)
            m_treemap->showPlaceholder(QStringLiteral("统计失败，请重试"));
        showMessage("统计空间占用失败",
                    "未能从 PowerShell 获取有效的统计结果。\n\n"
                    "请确认目录仍然存在，或更换目录后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject data = document.object();
    if (data.contains(QStringLiteral("error"))) {
        ui->treemapSummary->setText(QStringLiteral("目录不存在或无法访问"));
        if (m_treemap)
            m_treemap->showPlaceholder(QStringLiteral("这个目录无法读取"));
        showMessage("无法统计该目录",
                    "所选目录不存在、已被移动，或当前账户无权读取。",
                    QMessageBox::Warning);
        return;
    }

    const QString root = data.value(QStringLiteral("root")).toString();
    const qint64 total = static_cast<qint64>(
        data.value(QStringLiteral("total")).toDouble());
    const int folders = data.value(QStringLiteral("folders")).toInt();

    // 只有一个子项时 PowerShell 可能输出对象而不是数组
    QJsonArray items;
    const QJsonValue value = data.value(QStringLiteral("items"));
    if (value.isArray())
        items = value.toArray();
    else if (value.isObject())
        items.append(value.toObject());

    QList<TreemapWidget::Item> list;
    for (const QJsonValue &item : items) {
        if (!item.isObject())
            continue;
        const QJsonObject object = item.toObject();
        QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        TreemapWidget::Item entry;
        entry.isDir = object.value(QStringLiteral("isDir")).toBool();
        entry.name = entry.isDir ? name : QStringLiteral("本目录文件");
        entry.path = object.value(QStringLiteral("path")).toString();
        entry.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        entry.files = object.value(QStringLiteral("files")).toInt();
        list.append(entry);
    }

    QString summary;
    if (list.isEmpty()) {
        summary = QStringLiteral("%1 下没有可显示的内容")
                      .arg(QDir::toNativeSeparators(root));
        if (m_treemap)
            m_treemap->showPlaceholder(QStringLiteral("这个文件夹里没有可统计的内容"));
    } else {
        if (m_treemap)
            m_treemap->setItems(list);
        summary = QStringLiteral("%1 · 共 %2 · %3 个文件夹")
                      .arg(QDir::toNativeSeparators(root), formatFileSize(total))
                      .arg(folders);
        if (list.size() < folders)
            summary += QStringLiteral("（显示占用最大的 %1 项）").arg(list.size());
        summary += QStringLiteral(" · 点击方块可进入文件夹");
    }

    ui->treemapSummary->setText(summary);
    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::updateTreemapPathButtons()
{
    ui->treemapPath->setText(QDir::toNativeSeparators(m_treemapRoot));

    // 已经到盘符根目录时「上一级」不可用
    QDir dir(m_treemapRoot);
    ui->treemapUpBtn->setEnabled(dir.cdUp());
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

    if (!claimTask(QStringLiteral("读取已安装程序")))
        return;

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
                       QStringLiteral("会打开「%1」自带的卸载程序。\n\n"
                                      "跟着卸载窗口一路点「下一步」就行；"
                                      "中途可能弹出权限提示，选「是」即可。")
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

    if (!claimTask(QStringLiteral("卸载程序")))
        return;

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
        recordOperation(QStringLiteral("软件卸载"),
                        QStringLiteral("卸载程序"), name, true);
        showMessage("卸载完成", QStringLiteral("「%1」已卸载，正在刷新程序清单。").arg(name));
        runUninstallList();
        return;
    }

    if (code == QLatin1String("still-present")) {
        appendLog(QStringLiteral("「%1」的卸载程序已结束，但程序项仍然存在。").arg(name));
        recordOperation(QStringLiteral("软件卸载"),
                        QStringLiteral("运行卸载程序（未彻底移除）"), name, true);
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

    if (!claimTask(QStringLiteral("读取驱动列表")))
        return;

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

    if (!claimTask(QStringLiteral("读取 Hosts")))
        return;

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

    // 记录恢复前的内容，万一要撤销这次恢复，可以再写回去
    const QString tempPath = QDir(QDir::tempPath())
                                 .filePath(QStringLiteral("wt-hosts-pre-undo.txt"));
    QFile file(tempPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(ui->hostsEditor->toPlainText().toUtf8());
        file.close();
        m_hostsUndoSource = tempPath;
    } else {
        m_hostsUndoSource.clear();
    }

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

    if (!claimTask(QStringLiteral("修改 Hosts")))
        return;

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
    if (!m_hostsBackup.isEmpty()) {
        args << QStringLiteral("-Backup") << m_hostsBackup;
        m_hostsBackup.clear();
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
        m_undoId.clear();
        m_undoKind.clear();
        m_hostsUndoSource.clear();
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
        if (!m_undoId.isEmpty()) {
            const QString undoId = m_undoId;
            m_undoId.clear();
            m_undoKind.clear();
            markOperationUndone(undoId);
            refreshOperationPage();
            showMessage("已撤销",
                        QStringLiteral("Hosts 已恢复到操作前的状态，正在重新读取。"));
        } else if (restore) {
            const bool canUndo = !m_hostsUndoSource.isEmpty()
                                 && QFileInfo(m_hostsUndoSource).exists();
            QJsonObject undoArgs;
            undoArgs.insert(QStringLiteral("source"), m_hostsUndoSource);
            recordOperation(QStringLiteral("Hosts"),
                            QStringLiteral("恢复 Hosts 备份"),
                            backup, true, canUndo,
                            canUndo ? QStringLiteral("hosts-restore") : QString(),
                            undoArgs);
        } else {
            QJsonObject undoArgs;
            undoArgs.insert(QStringLiteral("backup"), backup);
            recordOperation(QStringLiteral("Hosts"),
                            QStringLiteral("保存 Hosts"),
                            backup.isEmpty() ? QStringLiteral("Hosts 文件") : backup,
                            true, !backup.isEmpty(),
                            backup.isEmpty() ? QString()
                                             : QStringLiteral("hosts-save"),
                            undoArgs);
        }
        m_hostsUndoSource.clear();
        showMessage(restore ? QStringLiteral("已恢复备份")
                            : QStringLiteral("Hosts 已保存"),
                    message);
        runHostsRead();
    } else {
        m_undoId.clear();
        m_undoKind.clear();
        m_hostsUndoSource.clear();
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

    if (!claimTask(QStringLiteral("读取 DNS 设置")))
        return;

    ++m_flow;

    appendLog("正在读取当前 DNS 配置……");

    ui->dnsSummary->setText(QStringLiteral("正在读取……"));
    ui->dnsSpeedResult->clear();
    m_recommendedDns.clear();
    m_recommendedDnsLabel.clear();
    ui->btn_applyDnsRecommend->setVisible(false);
    for (QPushButton *button : dnsActionButtons(ui))
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
    m_dnsServers = servers;

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

    for (QPushButton *button : dnsActionButtons(ui))
        button->setEnabled(true);

    appendLog("-------------------");
    appendLog(QStringLiteral("网卡 %1 当前 DNS：%2")
                  .arg(alias, servers.isEmpty() ? QStringLiteral("自动获取")
                                               : servers.join(QStringLiteral(", "))));
}

/** 对预设 DNS 与当前 DNS 并发测速，按 RTT 排序并给出推荐 */
void MainWindow::runDnsSpeedTest()
{
    if (!m_dnsSpeedRunner || m_dnsSpeedRunning)
        return;

    QStringList servers;
    const auto appendServers = [&servers](const QString &addresses) {
        for (const QString &raw : addresses.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString server = raw.trimmed();
            if (!server.isEmpty() && !servers.contains(server))
                servers.append(server);
        }
    };

    // 与 DNS 切换卡片里的预设保持一致；当前网卡的 DNS 也一起测
    appendServers(QStringLiteral("223.5.5.5,223.6.6.6"));
    appendServers(QStringLiteral("119.29.29.29,182.254.116.116"));
    appendServers(QStringLiteral("114.114.114.114,114.114.115.115"));
    appendServers(QStringLiteral("8.8.8.8,8.8.4.4"));
    appendServers(QStringLiteral("180.76.76.76"));
    for (const QString &server : m_dnsServers)
        appendServers(server);

    if (servers.isEmpty()) {
        ui->dnsSpeedResult->setText(QStringLiteral("没有可测速的 DNS 服务器。"));
        return;
    }

    m_dnsSpeedRunning = true;
    ui->dnsSpeedBtn->setEnabled(false);
    m_recommendedDns.clear();
    m_recommendedDnsLabel.clear();
    ui->btn_applyDnsRecommend->setVisible(false);
    ui->btn_applyDnsRecommend->setEnabled(false);
    ui->dnsSpeedResult->setText(QStringLiteral("正在测速，请稍候……"));
    appendLog(QStringLiteral("正在测速 %1 个 DNS 服务器……").arg(servers.size()));
    m_dnsSpeedRunner->start(servers, QStringLiteral("www.baidu.com"), 1500, 16);
}

/** 测速结束：成功项按耗时升序展示，失败项单列，最后给出推荐 */
void MainWindow::handleDnsSpeedResults(const QVector<DnsProbe::Result> &results)
{
    m_dnsSpeedRunning = false;
    ui->dnsSpeedBtn->setEnabled(true);

    if (results.isEmpty()) {
        m_recommendedDns.clear();
        m_recommendedDnsLabel.clear();
        ui->btn_applyDnsRecommend->setVisible(false);
        ui->dnsSpeedResult->setText(QStringLiteral("测速未返回结果，请稍后重试。"));
        return;
    }

    const auto displayName = [this](const QString &server) {
        const QString label = dnsServerLabel(server);
        QString name = (label == server)
                           ? server
                           : QStringLiteral("%1（%2）").arg(label, server);
        if (m_dnsServers.contains(server))
            name += QStringLiteral("（当前）");
        return name;
    };

    const QVector<DnsProbe::Result> ranked = DnsProbe::rankBySpeed(results);
    QStringList lines;
    QStringList failed;
    int rank = 0;
    for (const DnsProbe::Result &result : ranked) {
        if (result.status == DnsProbe::Status::Ok && result.elapsedMs >= 0) {
            ++rank;
            lines.append(QStringLiteral("%1. %2  %3 ms")
                             .arg(rank)
                             .arg(displayName(result.server))
                             .arg(result.elapsedMs));
        } else {
            failed.append(QStringLiteral("%1 %2")
                              .arg(displayName(result.server),
                                   DnsProbe::statusLabel(result.status)));
        }
    }

    const QString fastest = DnsProbe::fastestServer(ranked);
    QString conclusion;
    m_recommendedDns.clear();
    m_recommendedDnsLabel.clear();
    if (fastest.isEmpty()) {
        conclusion = QStringLiteral("所有候选 DNS 都未能解析，请检查网络后再试。");
    } else if (m_dnsServers.contains(fastest)) {
        conclusion = QStringLiteral("推荐：当前 DNS %1 已是最快（%2 ms），无需切换。")
                         .arg(displayName(fastest))
                         .arg(ranked.first().elapsedMs);
    } else {
        m_recommendedDns = dnsPresetServers(fastest);
        if (m_recommendedDns.isEmpty())
            m_recommendedDns = fastest;
        m_recommendedDnsLabel = dnsServerLabel(fastest);
        conclusion = QStringLiteral("推荐：%1（%2 ms），可点下方「应用推荐」一键切换。")
                         .arg(displayName(fastest))
                         .arg(ranked.first().elapsedMs);
    }

    ui->btn_applyDnsRecommend->setVisible(!m_recommendedDns.isEmpty());
    ui->btn_applyDnsRecommend->setEnabled(!m_recommendedDns.isEmpty()
                                          && !m_dnsAlias.isEmpty());

    QString text = QStringLiteral("测速结果（www.baidu.com）：");
    if (!lines.isEmpty())
        text += QStringLiteral("\n") + lines.join(QLatin1Char('\n'));
    if (!failed.isEmpty())
        text += QStringLiteral("\n不可用：") + failed.join(QStringLiteral("、"));
    text += QStringLiteral("\n") + conclusion;

    ui->dnsSpeedResult->setText(text);
    appendLog(QStringLiteral("DNS 测速完成。%1").arg(conclusion));
}

void MainWindow::applyDns(const QString &servers, const QString &label)
{
    if (m_dnsAlias.isEmpty()) {
        showMessage("请先读取当前设置",
                    "还没读到现在的 DNS 设置，先点一下「换 DNS」磁贴，读完再选。",
                    QMessageBox::Information);
        return;
    }

    const QString target = servers.isEmpty()
                               ? QStringLiteral("自动获取（由路由器分配）")
                               : servers.split(QLatin1Char(','))
                                     .join(QStringLiteral(" / "));

    if (QMessageBox::question(this, QStringLiteral("切换 DNS"),
                              QStringLiteral("将把「%1」的 DNS 设置为：\n%2（%3）\n\n"
                                             "切换前会自动备份当前 DNS，之后可用"
                                             "「恢复上次 DNS」还原。\n\n"
                                             "切换需要管理员权限，确定继续？")
                                  .arg(m_dnsAlias, label, target))
        != QMessageBox::Yes)
        return;

    m_dnsPendingLabel = label;
    runDnsWrite(servers, QStringLiteral("apply"));
}

/** 应用测速推荐的最快 DNS */
void MainWindow::applyRecommendedDns()
{
    if (m_recommendedDns.isEmpty())
        return;

    applyDns(m_recommendedDns, m_recommendedDnsLabel);
}

/** 还原到上一次切换前的 DNS 设置 */
void MainWindow::restoreDns()
{
    if (m_dnsAlias.isEmpty()) {
        showMessage("请先读取当前设置",
                    "还没读到现在的 DNS 设置，先点一下「换 DNS」磁贴。",
                    QMessageBox::Information);
        return;
    }

    if (QMessageBox::question(
            this, QStringLiteral("恢复上次 DNS"),
            QStringLiteral("将把「%1」的 DNS 还原为上一次切换前的设置。\n\n"
                           "恢复需要管理员权限，确定继续？")
                .arg(m_dnsAlias))
        != QMessageBox::Yes)
        return;

    m_dnsRestoreMode = true;
    m_dnsPendingLabel = QStringLiteral("上次的 DNS 设置");
    runDnsWrite(QString(), QStringLiteral("restore"));
}

void MainWindow::runDnsWrite(const QString &servers, const QString &action)
{
    const bool restore = (action == QLatin1String("restore"));
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/dns.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少 DNS 脚本",
                    "未找到 ps1\\dns.ps1，无法切换。", QMessageBox::Critical);
        return;
    }

    if (!claimTask(restore ? QStringLiteral("恢复 DNS")
                           : QStringLiteral("切换 DNS")))
        return;

    ++m_flow;

    appendLog(restore
                  ? QStringLiteral("正在恢复上次的 DNS，若弹出权限提示请选择“是”……")
                  : QStringLiteral("正在切换 DNS，若弹出权限提示请选择“是”……"));

    ui->dnsSummary->setText(restore ? QStringLiteral("正在恢复……")
                                    : QStringLiteral("正在切换……"));
    for (QPushButton *button : dnsActionButtons(ui))
        button->setEnabled(false);

    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), action
    };
    if (!restore) {
        if (servers.isEmpty()) {
            args << QStringLiteral("-Auto");
        } else {
            args << QStringLiteral("-Servers") << servers;
        }
    }

    m_task = Task::DnsWrite;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleDnsWrite()
{
    const bool restore = m_dnsRestoreMode;
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
    QString backup;
    if (!payload.isEmpty() && parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        const QJsonObject data = document.object();
        code = data.value(QStringLiteral("code")).toString();
        backup = data.value(QStringLiteral("backup")).toString();
    }

    if (code == QLatin1String("applied") || code == QLatin1String("restored")) {
        const bool restored = (code == QLatin1String("restored"));
        appendLog("-------------------");
        appendLog(restored ? "已恢复上次的 DNS，正在刷新解析缓存。"
                           : "DNS 已切换，正在刷新解析缓存。");
        recordOperation(QStringLiteral("DNS"),
                        restored ? QStringLiteral("恢复上次 DNS")
                                 : QStringLiteral("切换 DNS"),
                        m_dnsPendingLabel.isEmpty()
                            ? QStringLiteral("当前默认网卡")
                            : m_dnsPendingLabel,
                        true);
        m_dnsPendingLabel.clear();
        m_dnsRestoreMode = false;

        QString detail = restored
                             ? QStringLiteral("已还原为上一次切换前的 DNS，"
                                              "正在重新读取当前配置。")
                             : QStringLiteral("新的 DNS 已生效，正在重新读取当前配置。");
        if (!backup.isEmpty())
            detail += QStringLiteral("\n\n原设置已备份到：\n%1").arg(backup);
        showMessage(restored ? "已恢复上次 DNS" : "DNS 已切换", detail);
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
    } else if (code == QLatin1String("backup-failed")) {
        message = QStringLiteral("备份当前 DNS 失败，为避免无法回滚已中止切换。");
    } else if (code == QLatin1String("no-backup")) {
        message = QStringLiteral("还没有可恢复的 DNS 备份，先切换一次 DNS 就会自动备份。");
    } else if (code == QLatin1String("restore-error")) {
        message = QStringLiteral("恢复上次 DNS 失败，可能是备份已失效或权限不足。");
    } else if (code == QLatin1String("error")) {
        message = restore
                      ? QStringLiteral("恢复 DNS 时出错，可能是权限不足或网卡不支持。")
                      : QStringLiteral("设置 DNS 时出错，可能是权限不足或网卡不支持。");
    } else {
        message = QStringLiteral("未能解析切换结果，请重新读取 DNS 确认当前配置。");
    }

    appendLog("-------------------");
    appendLog(message);
    if (!code.isEmpty() && code != QLatin1String("error"))
        appendLog(QStringLiteral("（返回码：%1）").arg(code));

    showMessage(restore ? "恢复 DNS 失败" : "切换 DNS 失败", message,
                QMessageBox::Warning);

    m_dnsPendingLabel.clear();
    m_dnsRestoreMode = false;

    ui->dnsSummary->setText(message);
    const bool ready = !m_dnsAlias.isEmpty();
    for (QPushButton *button : dnsActionButtons(ui))
        button->setEnabled(ready);
    ui->btn_applyDnsRecommend->setVisible(!m_recommendedDns.isEmpty());
}

// ---------------------------------------------------------------- 系统代理

void MainWindow::runProxyRead()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/proxy.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少系统代理脚本",
                    "未找到 ps1\\proxy.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("读取系统代理")))
        return;

    ++m_flow;

    appendLog("正在读取系统代理设置……");
    ui->proxySummary->setText(QStringLiteral("正在读取……"));
    for (QPushButton *button : proxyActionButtons(ui))
        button->setEnabled(false);
    ui->cardProxy->setVisible(true);

    QScrollArea *page = ui->pageNetwork;
    QTimer::singleShot(0, this, [page, this] {
        page->ensureWidgetVisible(ui->cardProxy);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("read")
    };

    m_task = Task::ProxyRead;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleProxyRead()
{
    QJsonObject data;
    if (!parseProxyPayload(m_runner->collectedOutput(), &data)) {
        appendLog("-------------------");
        appendLog("读取系统代理失败：未能解析脚本输出。");
        ui->proxySummary->setText(QStringLiteral("读取失败，请点「刷新状态」重试"));
        ui->btn_proxyRefresh->setEnabled(true);
        ui->btn_proxyOn->setEnabled(true);
        ui->btn_proxyOff->setEnabled(false);
        ui->btn_proxyRestore->setEnabled(false);
        showMessage("读取系统代理失败",
                    "未能解析 ps1\\proxy.ps1 的输出。请重试，或检查脚本是否完整。",
                    QMessageBox::Warning);
        return;
    }

    renderProxyState(data);
}

void MainWindow::renderProxyState(const QJsonObject &data)
{
    const bool enabled = data.value(QStringLiteral("enabled")).toBool();
    const QString server = data.value(QStringLiteral("server")).toString();
    const QString pac = data.value(QStringLiteral("pac")).toString();
    const bool hasBackup = data.value(QStringLiteral("hasBackup")).toBool();

    QString summary;
    if (enabled) {
        summary = server.isEmpty()
                      ? QStringLiteral("已开启（未填写代理服务器）")
                      : QStringLiteral("已开启 · %1").arg(server);
    } else if (!pac.isEmpty()) {
        summary = QStringLiteral("未启用手动代理，但启用了自动配置脚本（PAC）：%1")
                      .arg(pac);
    } else {
        summary = QStringLiteral("未启用系统代理（直连）");
    }

    if (hasBackup) {
        QString previous = data.value(QStringLiteral("backupServer")).toString();
        if (previous.isEmpty())
            previous = QStringLiteral("直连");
        summary += QStringLiteral("\n上次设置已备份：%1 · %2")
                       .arg(previous,
                            data.value(QStringLiteral("backupTime")).toString());
    }

    if (!m_proxyPendingNotice.isEmpty()) {
        summary.prepend(m_proxyPendingNotice + QLatin1Char('\n'));
        m_proxyPendingNotice.clear();
    }

    ui->proxySummary->setText(summary);
    if (enabled && !server.isEmpty() && ui->proxyServerInput->text().trimmed().isEmpty())
        ui->proxyServerInput->setText(server);

    ui->btn_proxyRefresh->setEnabled(true);
    ui->btn_proxyOn->setEnabled(true);
    ui->btn_proxyOff->setEnabled(enabled);
    ui->btn_proxyRestore->setEnabled(hasBackup);

    appendLog(summary.split(QLatin1Char('\n')).join(QStringLiteral("；")));
}

void MainWindow::writeProxy(const QString &action, const QString &server)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/proxy.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少系统代理脚本",
                    "未找到 ps1\\proxy.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    QString value = server.trimmed();
    if (action == QLatin1String("apply")) {
        static const QRegularExpression pattern(
            QStringLiteral("^[A-Za-z0-9.\\-]+:[0-9]{1,5}$"));
        const QRegularExpressionMatch match = pattern.match(value);
        bool portOk = false;
        if (match.hasMatch()) {
            const int port = value.section(QLatin1Char(':'), -1).toInt(&portOk);
            portOk = portOk && port >= 1 && port <= 65535;
        }
        if (!match.hasMatch() || !portOk) {
            showMessage("代理地址格式不正确",
                        "请按「主机:端口」填写，例如 127.0.0.1:7890。",
                        QMessageBox::Warning);
            ui->proxyServerInput->setFocus();
            return;
        }
    }

    QString confirmText;
    if (action == QLatin1String("off")) {
        confirmText = QStringLiteral("关闭后，使用系统代理的程序将改为直连。\n\n"
                                     "当前设置会先备份，可通过「恢复上次代理」还原。");
    } else if (action == QLatin1String("apply")) {
        confirmText = QStringLiteral("将系统代理设为：%1\n\n"
                                     "当前设置会先备份，可通过「恢复上次代理」还原。")
                          .arg(value);
    } else {
        confirmText = QStringLiteral("将恢复到上一次保存的代理设置，当前设置会被覆盖。");
    }

    if (!confirmAction(action == QLatin1String("apply")
                           ? QStringLiteral("启用系统代理")
                           : (action == QLatin1String("off")
                                  ? QStringLiteral("关闭系统代理")
                                  : QStringLiteral("恢复上次代理")),
                       confirmText, QMessageBox::Question))
        return;

    if (!claimTask(action == QLatin1String("off")
                       ? QStringLiteral("关闭系统代理")
                       : QStringLiteral("切换系统代理")))
        return;

    ++m_flow;

    m_proxyAction = action;
    appendLog(action == QLatin1String("off")
                  ? "正在关闭系统代理……"
                  : (action == QLatin1String("apply")
                         ? QStringLiteral("正在启用系统代理 %1……").arg(value)
                         : QStringLiteral("正在恢复上次的代理设置……")));

    ui->proxySummary->setText(QStringLiteral("正在应用……"));
    for (QPushButton *button : proxyActionButtons(ui))
        button->setEnabled(false);

    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), action
    };
    if (action == QLatin1String("apply"))
        args << QStringLiteral("-Server") << value;

    m_task = Task::ProxyWrite;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleProxyWrite()
{
    const QString action = m_proxyAction;
    m_proxyAction.clear();

    QJsonObject data;
    if (!parseProxyPayload(m_runner->collectedOutput(), &data)) {
        appendLog("-------------------");
        appendLog("切换系统代理失败：未能解析脚本输出。");
        ui->proxySummary->setText(QStringLiteral("切换失败，请点「刷新状态」确认当前设置"));
        ui->btn_proxyRefresh->setEnabled(true);
        ui->btn_proxyOn->setEnabled(true);
        showMessage("切换系统代理失败",
                    "未能解析脚本输出，无法确认设置是否生效。请点「刷新状态」核对。",
                    QMessageBox::Warning);
        return;
    }

    const QString code = data.value(QStringLiteral("code")).toString();
    if (code == QLatin1String("off") || code == QLatin1String("applied")
        || code == QLatin1String("restored")) {
        QString label;
        QString notice;
        if (code == QLatin1String("off")) {
            label = QStringLiteral("关闭系统代理");
            notice = QStringLiteral("已关闭系统代理，原设置已备份。");
        } else if (code == QLatin1String("applied")) {
            label = QStringLiteral("启用系统代理");
            notice = QStringLiteral("已启用系统代理：%1")
                         .arg(data.value(QStringLiteral("server")).toString());
        } else {
            label = QStringLiteral("恢复上次代理");
            notice = QStringLiteral("已恢复上次保存的代理设置。");
        }

        appendLog("-------------------");
        appendLog(notice);
        recordOperation(QStringLiteral("系统代理"), label,
                        action == QLatin1String("apply")
                            ? data.value(QStringLiteral("server")).toString()
                            : label,
                        true);

        m_proxyPendingNotice = notice;
        renderProxyState(data);
        showMessage(label, notice + QStringLiteral("\n\n如需还原，点「恢复上次代理」。"));
        return;
    }

    QString message;
    if (code == QLatin1String("invalid-server")) {
        message = QStringLiteral("代理地址格式不正确，请按「主机:端口」填写。");
    } else if (code == QLatin1String("backup-failed")) {
        message = QStringLiteral("备份当前代理设置失败，为避免无法回滚已中止操作。");
    } else if (code == QLatin1String("no-backup")) {
        message = QStringLiteral("还没有可恢复的代理备份，先关闭或切换一次代理就会自动备份。");
    } else if (code == QLatin1String("restore-error")) {
        message = QStringLiteral("恢复上次代理失败，可能是备份已损坏。");
    } else if (code == QLatin1String("write-failed")) {
        message = QStringLiteral("写入代理设置失败，可能被安全软件拦截。");
    } else {
        message = QStringLiteral("未能解析切换结果，请点「刷新状态」确认当前设置。");
    }

    appendLog("-------------------");
    appendLog(message);
    if (!code.isEmpty())
        appendLog(QStringLiteral("（返回码：%1）").arg(code));

    showMessage(QStringLiteral("切换系统代理失败"), message, QMessageBox::Warning);
    ui->proxySummary->setText(message);
    ui->btn_proxyRefresh->setEnabled(true);
    ui->btn_proxyOn->setEnabled(true);
    ui->btn_proxyOff->setEnabled(data.value(QStringLiteral("enabled")).toBool());
    ui->btn_proxyRestore->setEnabled(data.value(QStringLiteral("hasBackup")).toBool());
}

// ---------------------------------------------------------------- 定时巡检

QString MainWindow::settingsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("settings.ini"));
}

void MainWindow::setupDiagSchedule()
{
    m_diagTimer = new QTimer(this);
    m_diagTimer->setTimerType(Qt::CoarseTimer);
    connect(m_diagTimer, &QTimer::timeout,
            this, &MainWindow::runScheduledDiagnosis);

    connect(ui->chk_diagSchedule, &QCheckBox::toggled,
            this, &MainWindow::setDiagScheduleEnabled);

    connect(ui->combo_diagInterval,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_diagIntervalMinutes = index == 0 ? 10 : (index == 2 ? 60 : 30);
                QSettings settings(settingsFilePath(), QSettings::IniFormat);
                settings.setValue(QStringLiteral("diag/scheduleInterval"),
                                  m_diagIntervalMinutes);

                if (m_diagTimer->isActive()) {
                    m_diagTimer->start(m_diagIntervalMinutes * 60 * 1000);
                }
                updateDiagScheduleStatus();
            });

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const int stored = settings.value(QStringLiteral("diag/scheduleInterval"), 30).toInt();
    m_diagIntervalMinutes = stored == 10 ? 10 : (stored == 60 ? 60 : 30);
    ui->combo_diagInterval->setCurrentIndex(
        m_diagIntervalMinutes == 10 ? 0 : (m_diagIntervalMinutes == 60 ? 2 : 1));

    const bool enabled =
        settings.value(QStringLiteral("diag/scheduleEnabled"), false).toBool();
    ui->chk_diagSchedule->setChecked(enabled);
    // setChecked 会触发 toggled；这里再调用一次保证首次启动也建立托盘与定时器
    setDiagScheduleEnabled(enabled);
    updateDiagScheduleStatus();
}

void MainWindow::setDiagScheduleEnabled(bool enabled)
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("diag/scheduleEnabled"), enabled);

    if (enabled) {
        if (!m_diagTimer->isActive())
            m_diagTimer->start(m_diagIntervalMinutes * 60 * 1000);

        if (!m_trayIcon) {
            m_trayIcon = new QSystemTrayIcon(windowIcon(), this);
            m_trayIcon->setToolTip(QStringLiteral("WindowsTool 定时巡检"));

            QMenu *menu = new QMenu(this);
            QAction *openAction = menu->addAction(QStringLiteral("打开主界面"));
            connect(openAction, &QAction::triggered, this, [this] {
                showNormal();
                raise();
                activateWindow();
            });
            QAction *stopAction = menu->addAction(QStringLiteral("停止定时巡检"));
            connect(stopAction, &QAction::triggered, this, [this] {
                ui->chk_diagSchedule->setChecked(false);
            });
            m_trayIcon->setContextMenu(menu);

            connect(m_trayIcon, &QSystemTrayIcon::activated, this,
                    [this](QSystemTrayIcon::ActivationReason reason) {
                        if (reason == QSystemTrayIcon::Trigger
                            || reason == QSystemTrayIcon::DoubleClick) {
                            showNormal();
                            raise();
                            activateWindow();
                        }
                    });
            connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this] {
                showNormal();
                raise();
                activateWindow();
            });
        }

        if (QSystemTrayIcon::isSystemTrayAvailable())
            m_trayIcon->show();
    } else {
        m_diagTimer->stop();
        if (m_trayIcon)
            m_trayIcon->hide();
    }

    updateDiagScheduleStatus();
}

void MainWindow::updateDiagScheduleStatus()
{
    if (!m_diagTimer || !m_diagTimer->isActive()) {
        ui->diagScheduleStatus->setText(
            QStringLiteral("未开启（仅在程序运行时生效）"));
        return;
    }

    const int remain = m_diagTimer->remainingTime();
    const QString next =
        remain > 0
            ? QDateTime::currentDateTime().addMSecs(remain)
                  .toString(QStringLiteral("HH:mm"))
            : QStringLiteral("即将开始");
    ui->diagScheduleStatus->setText(
        QStringLiteral("已开启 · 每 %1 分钟巡检 · 下次 %2")
            .arg(QString::number(m_diagIntervalMinutes), next));
}

void MainWindow::runScheduledDiagnosis()
{
    updateDiagScheduleStatus();

    if (m_task != Task::None) {
        appendLog(QStringLiteral("定时巡检：上一个任务还在运行，本轮跳过。"));
        return;
    }

    appendLog("-------------------");
    m_silentDiag = true;
    startAutoCheck();
}

void MainWindow::showTrayNotification(const QString &title, const QString &body)
{
    if (m_trayIcon && m_trayIcon->isVisible()
        && QSystemTrayIcon::supportsMessages()) {
        m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, 10000);
        return;
    }

    // 托盘不可用时的兜底：仍然让用户看到巡检结论
    showMessage(title, body, QMessageBox::Warning);
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

// ---------------------------------------------------------------- 系统文件修复

void MainWindow::toggleSfcCard()
{
    // 修复运行中不允许收起卡片，避免用户误以为任务已结束
    if (m_task == Task::SfcRepair)
        return;

    ui->cardSfc->setVisible(!ui->cardSfc->isVisible());
    if (ui->cardSfc->isVisible())
        showSystemResultCard(ui->cardSfc);
}

void MainWindow::runSfcRepair(const QString &mode)
{
    if (m_task == Task::SfcRepair)
        return;

    if (!batExists("sfcRepair.bat")) {
        showMessage("缺少脚本文件",
                    "未找到所需的脚本文件 sfcRepair.bat。\n\n"
                    "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    const bool dism = (mode == QLatin1String("dism"));

    QString text = dism
        ? QStringLiteral("会检查系统组件有没有损坏，并把坏掉的部分换成正常版本，"
                         "适合系统功能异常、程序频繁报错的情况。")
        : QStringLiteral("会扫描 Windows 的核心系统文件，发现损坏就自动换成正确的版本。");
    text += QStringLiteral("\n\n整个过程可能要 10-30 分钟，期间电脑会明显变慢，"
                           "请不要关闭本程序。\n\n"
                           "开始时会弹出管理员权限请求，请选「是」。\n\n"
                           "继续吗？");

    if (!confirmAction(dism ? "修复系统映像" : "扫描并修复系统文件", text))
        return;

    if (!claimTask(QStringLiteral("系统文件检查")))
        return;

    ++m_flow;

    const QString logPath = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("wt_sfc_%1.log").arg(mode));
    // 先删旧日志，避免把上一次的结果当成本次输出
    QFile::remove(logPath);

    m_sfcMode = mode;
    m_sfcLogPath = logPath;
    m_sfcLogBytes = 0;
    m_sfcLogChars = 0;
    ui->sfcLog->clear();

    ui->sfcSummary->setText(
        QStringLiteral("正在执行 %1，日志每秒刷新。请耐心等待，不要关闭本程序。")
            .arg(dism ? QStringLiteral("DISM 系统映像修复")
                      : QStringLiteral("SFC 系统文件扫描")));
    ui->sfcSummary->setProperty("state", QString());
    ui->sfcSummary->style()->unpolish(ui->sfcSummary);
    ui->sfcSummary->style()->polish(ui->sfcSummary);
    showSystemResultCard(ui->cardSfc);

    setSfcRunning(true);

    if (!m_sfcTimer) {
        m_sfcTimer = new QTimer(this);
        m_sfcTimer->setInterval(1000);
        connect(m_sfcTimer, &QTimer::timeout, this, &MainWindow::pollSfcLog);
    }
    m_sfcTimer->start();

    appendLog("-------------------");
    appendLog(dism ? "正在修复系统映像（DISM），请稍候……"
                   : "正在扫描并修复系统文件（SFC），请稍候……");

    m_task = Task::SfcRepair;
    m_runner->runBat("sfcRepair.bat",
                     {mode, QDir::toNativeSeparators(logPath)});
}

void MainWindow::pollSfcLog()
{
    // 任务已被其它功能取代（或已结束）时停止轮询
    if (m_task != Task::SfcRepair) {
        if (m_sfcTimer && m_sfcTimer->isActive())
            m_sfcTimer->stop();
        return;
    }

    if (m_sfcLogPath.isEmpty())
        return;

    QFile file(m_sfcLogPath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const qint64 size = file.size();
    if (size <= m_sfcLogBytes) {
        file.close();
        return;
    }

    // 整份解码再取增量：日志可能被拆成多次写入，
    // 逐块解码会在多字节字符中间断开，产生乱码。
    const QString full = decodeLogFile(file.readAll());
    file.close();
    m_sfcLogBytes = size;

    if (full.size() <= m_sfcLogChars)
        return;
    appendSfcLog(full.mid(m_sfcLogChars));
    m_sfcLogChars = full.size();
}

void MainWindow::appendSfcLog(const QString &text)
{
    if (text.isEmpty())
        return;

    // 与主日志一样走 QTextCursor::insertText，避免把 <、& 当成标记解析
    QTextCursor cursor(ui->sfcLog->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    ui->sfcLog->setTextCursor(cursor);
    ui->sfcLog->ensureCursorVisible();
}

void MainWindow::handleSfcRepair(int exitCode)
{
    if (m_sfcTimer)
        m_sfcTimer->stop();
    pollSfcLog();   // 进程结束前可能还有最后一段输出

    setSfcRunning(false);

    const bool dism = (m_sfcMode == QLatin1String("dism"));
    const QString command = dism ? QStringLiteral("DISM") : QStringLiteral("SFC");

    QString state;
    if (exitCode == 0) {
        state = QStringLiteral("ok");
        ui->sfcSummary->setText(
            QStringLiteral("%1 执行完成（退出码 0）。请对照下方日志确认结果；"
                           "若日志提示修复了损坏文件，建议重启计算机。").arg(command));
        appendLog("-------------------");
        appendLog(QStringLiteral("%1 执行完成，日志已输出到下方窗口。").arg(command));
        recordOperation(QStringLiteral("系统文件"),
                        QStringLiteral("%1 修复").arg(command),
                        QStringLiteral("系统盘"), true);
    } else {
        state = QStringLiteral("risk");
        ui->sfcSummary->setText(
            QStringLiteral("%1 未成功完成（退出码 %2）。若日志中没有有效内容，"
                           "通常是取消了管理员权限请求；可重新运行并选择「是」。")
                .arg(command).arg(exitCode));
        appendLog("-------------------");
        appendLog(QStringLiteral("%1 未成功完成，退出码：%2").arg(command).arg(exitCode));
        showMessage(dism ? "系统映像修复未完成" : "系统文件修复未完成",
                    QStringLiteral("%1 没有正常结束（错误码 %2）。\n\n"
                                   "如果是刚才取消了管理员权限提示，重新点一下按钮，"
                                   "在弹窗里选「是」就行。").arg(command).arg(exitCode),
                    QMessageBox::Warning);
    }

    ui->sfcSummary->setProperty("state", state);
    ui->sfcSummary->style()->unpolish(ui->sfcSummary);
    ui->sfcSummary->style()->polish(ui->sfcSummary);
    m_sfcLogPath.clear();
}

void MainWindow::setSfcRunning(bool running)
{
    ui->btnSfcScan->setEnabled(!running);
    ui->btnDismRepair->setEnabled(!running);
}

// ---------------------------------------------------------------- Windows 更新修复

void MainWindow::runWUpdateFix()
{
    if (m_task == Task::WUpdateRepair)
        return;

    if (!batExists("winUpdateFix.bat")) {
        showMessage("缺少脚本文件",
                    "未找到所需的脚本文件 winUpdateFix.bat。\n\n"
                    "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!confirmAction(
            "重置 Windows 更新",
            QStringLiteral("会把 Windows 更新用到的缓存清空，相当于让更新重新开始，"
                           "专门解决「更新总是失败、卡在某个进度」的问题。\n\n"
                           "旧的缓存不会直接删掉，而是改名备份，需要时可以改回去。\n"
                           "过程中更新功能暂时用不了，正常上网不受影响，大约需要 1-3 分钟。\n\n"
                           "开始时会弹出管理员权限请求，请选「是」。\n\n"
                           "继续吗？")))
        return;

    if (!claimTask(QStringLiteral("重置更新组件")))
        return;

    ++m_flow;

    const QString logPath = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("wt_wupdate.log"));
    // 先删旧日志，避免上一次的结果混进本次展示
    QFile::remove(logPath);

    m_updateLogPath = logPath;
    ui->wupdateLog->clear();
    ui->wupdateSummary->setText(
        QStringLiteral("正在重置更新组件，请稍候……"));
    ui->wupdateSummary->setProperty("state", QString());
    ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
    ui->wupdateSummary->style()->polish(ui->wupdateSummary);
    showSystemResultCard(ui->cardWUpdate);

    ui->btnWUpdateFix->setEnabled(false);

    appendLog("-------------------");
    appendLog("正在重置 Windows 更新组件，请稍候……");

    m_task = Task::WUpdateRepair;
    m_runner->runBat("winUpdateFix.bat",
                     {QDir::toNativeSeparators(logPath)});
}

void MainWindow::handleWUpdateFix(int exitCode)
{
    ui->btnWUpdateFix->setEnabled(true);

    // 脚本输出写在日志文件里，结束后一次性读取展示
    QFile file(m_updateLogPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QString text = decodeLogFile(file.readAll()).trimmed();
        file.close();
        if (!text.isEmpty()) {
            QTextCursor cursor(ui->wupdateLog->document());
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(text);
            ui->wupdateLog->setTextCursor(cursor);
            ui->wupdateLog->ensureCursorVisible();
        }
    }

    QString state;
    if (exitCode == 0) {
        state = QStringLiteral("ok");
        ui->wupdateSummary->setText(
            QStringLiteral("更新组件已重置。旧缓存已重命名为带时间戳的 .bak，"
                           "需要时可在 C:\\Windows 下手动恢复。建议重启计算机后"
                           "再检查一次更新。"));
        appendLog("-------------------");
        appendLog("Windows 更新组件重置完成。");
        recordOperation(QStringLiteral("Windows 更新"),
                        QStringLiteral("重置更新组件"),
                        QStringLiteral("更新缓存目录"), true);
    } else {
        state = QStringLiteral("risk");
        ui->wupdateSummary->setText(
            QStringLiteral("重置未全部完成（退出码 %1）。若日志中没有有效内容，"
                           "通常是取消了管理员权限请求；否则请检查日志中是否有"
                           "目录被占用或权限不足的提示。")
                .arg(exitCode));
        appendLog("-------------------");
        appendLog(QStringLiteral("Windows 更新组件重置未完成，退出码：%1").arg(exitCode));
        showMessage("Windows 更新修复未完成",
                    QStringLiteral("退出码为 %1。\n\n"
                                   "若刚刚取消了管理员权限请求，重新点击按钮并在"
                                   "弹窗中选择「是」即可；其他情况请查看卡片中的日志。")
                        .arg(exitCode),
                    QMessageBox::Warning);
    }

    ui->wupdateSummary->setProperty("state", state);
    ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
    ui->wupdateSummary->style()->polish(ui->wupdateSummary);
    m_updateLogPath.clear();
}

// ---------------------------------------------------------------- 更新组件清理

void MainWindow::runSxsCleanup()
{
    if (m_task == Task::SxsCleanup)
        return;

    if (!batExists("winSxsClean.bat")) {
        showMessage("缺少脚本文件",
                    "未找到所需的脚本文件 winSxsClean.bat。\n\n"
                    "请确认程序目录下的 bat 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!confirmAction(
            "清理更新组件",
            QStringLiteral("会删除 Windows 更新缓存里已被新版取代的旧组件，"
                           "这些文件已经用不到了，清理后通常能腾出几个 GB 的 C 盘空间。\n\n"
                           "已安装的更新和系统功能不受影响，但清理后无法再卸载被删除的旧更新。\n"
                           "过程中更新功能暂时不可用，正常上网和使用电脑不受影响，"
                           "耗时可能几分钟到十几分钟。\n\n"
                           "开始时会弹出管理员权限请求，请选「是」。\n\n"
                           "继续吗？")))
        return;

    if (!claimTask(QStringLiteral("清理更新组件")))
        return;

    ++m_flow;

    const QString logPath = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("wt_sxs.log"));
    // 先删旧日志，避免上一次的结果混进本次展示
    QFile::remove(logPath);

    m_sxsLogPath = logPath;
    ui->wupdateLog->clear();
    ui->wupdateSummary->setText(
        QStringLiteral("正在清理更新组件，可能需要几分钟，请保持电脑开机……"));
    ui->wupdateSummary->setProperty("state", QString());
    ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
    ui->wupdateSummary->style()->polish(ui->wupdateSummary);
    showSystemResultCard(ui->cardWUpdate);

    // 清理期间禁用重置按钮，避免两套操作同时改动组件存储
    ui->btnWUpdateFix->setEnabled(false);

    appendLog("-------------------");
    appendLog("正在清理 Windows 更新组件，请稍候……");

    m_task = Task::SxsCleanup;
    m_runner->runBat("winSxsClean.bat",
                     {QDir::toNativeSeparators(logPath)});
}

void MainWindow::handleSxsCleanup(int exitCode)
{
    ui->btnWUpdateFix->setEnabled(true);

    // 脚本输出写在日志文件里，结束后一次性读取展示
    QFile file(m_sxsLogPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QString text = decodeLogFile(file.readAll()).trimmed();
        file.close();
        if (!text.isEmpty()) {
            QTextCursor cursor(ui->wupdateLog->document());
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(text);
            ui->wupdateLog->setTextCursor(cursor);
            ui->wupdateLog->ensureCursorVisible();
        }
    }

    QString state;
    if (exitCode == 0) {
        state = QStringLiteral("ok");
        ui->wupdateSummary->setText(
            QStringLiteral("更新组件清理完成。已被新版取代的旧更新文件已经删除，"
                           "C 盘可用空间应该有所增加。重启后可到「实用工具 → "
                           "C 盘空间图」再核对一次。"));
        appendLog("-------------------");
        appendLog("Windows 更新组件清理完成。");
        recordOperation(QStringLiteral("Windows 更新"),
                        QStringLiteral("清理更新组件"),
                        QStringLiteral("WinSxS 组件存储"), true);
    } else {
        state = QStringLiteral("risk");
        ui->wupdateSummary->setText(
            QStringLiteral("清理未完成（退出码 %1）。若日志中没有有效内容，"
                           "通常是取消了管理员权限请求；否则可能是组件存储正被"
                           "Windows 更新占用，重启后再试一次。").arg(exitCode));
        appendLog("-------------------");
        appendLog(QStringLiteral("Windows 更新组件清理未完成，退出码：%1").arg(exitCode));
        showMessage("组件清理未完成",
                    QStringLiteral("退出码为 %1。\n\n"
                                   "若刚刚取消了管理员权限请求，重新点击按钮并在"
                                   "弹窗中选择「是」即可；其他情况请查看卡片中的日志。")
                        .arg(exitCode),
                    QMessageBox::Warning);
    }

    ui->wupdateSummary->setProperty("state", state);
    ui->wupdateSummary->style()->unpolish(ui->wupdateSummary);
    ui->wupdateSummary->style()->polish(ui->wupdateSummary);
    m_sxsLogPath.clear();
}

// ---------------------------------------------------------------- 系统还原点

void MainWindow::runRestorePointList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/restorepoint.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少还原点脚本",
                    "未找到 ps1\\restorepoint.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("读取系统还原点")))
        return;

    ++m_flow;

    // 清空上一次的结果
    while (QLayoutItem *item = ui->restoreRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    ui->restoreSummary->setText(QStringLiteral("正在读取还原点……"));
    ui->restoreSummary->setProperty("state", QString());
    ui->restoreSummary->style()->unpolish(ui->restoreSummary);
    ui->restoreSummary->style()->polish(ui->restoreSummary);
    showSystemResultCard(ui->cardRestore);

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("list")
    };

    m_task = Task::RestoreList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleRestorePointList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("RESTOREPOINT_JSON:");
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
        ui->restoreSummary->setText(QStringLiteral("读取还原点失败，请重试"));
        ui->restoreSummary->setProperty("state", QStringLiteral("risk"));
        ui->restoreSummary->style()->unpolish(ui->restoreSummary);
        ui->restoreSummary->style()->polish(ui->restoreSummary);
        appendLog("-------------------");
        appendLog("读取系统还原点失败：未能解析脚本输出。");
        const QString raw = output.trimmed();
        if (!raw.isEmpty())
            appendLog(raw.left(2000));
        return;
    }

    const QJsonObject data = document.object();
    const bool enabled = data.value(QStringLiteral("enabled")).toBool();

    // 只有一个还原点时 PowerShell 可能输出对象而不是数组
    QJsonArray points;
    const QJsonValue value = data.value(QStringLiteral("points"));
    if (value.isArray())
        points = value.toArray();
    else if (value.isObject())
        points.append(value.toObject());

    int shown = 0;
    for (const QJsonValue &pointValue : points) {
        if (!pointValue.isObject())
            continue;
        const QJsonObject point = pointValue.toObject();

        QString time = point.value(QStringLiteral("time")).toString();
        if (time.isEmpty())
            time = QStringLiteral("时间未知");

        QString description = point.value(QStringLiteral("desc")).toString().trimmed();
        if (description.isEmpty())
            description = QStringLiteral("未命名还原点");

        const int seq = point.value(QStringLiteral("seq")).toInt();
        addRestoreRow(time, description,
                      seq > 0 ? QStringLiteral("序号 %1").arg(seq)
                              : QStringLiteral("系统还原点"));
        ++shown;
    }

    QString summary;
    QString state;
    if (!enabled) {
        state = QStringLiteral("warn");
        summary = QStringLiteral("系统还原当前是关闭的，所以没有可用的还原点。"
                                 "点下面的「现在创建一个还原点」，程序会自动开启系统还原。");
    } else if (shown == 0) {
        state = QStringLiteral("warn");
        summary = QStringLiteral("系统还原已开启，但还没有还原点。Windows 通常会在"
                                 "安装软件或更新前自动创建，也可以现在就手动创建一个。");
    } else {
        summary = QStringLiteral("找到 %1 个还原点（最多显示最近 10 个）。"
                                 "要恢复系统，点「打开系统还原」后在系统窗口里选择。")
                      .arg(shown);
    }

    ui->restoreSummary->setText(summary);
    ui->restoreSummary->setProperty("state", state);
    ui->restoreSummary->style()->unpolish(ui->restoreSummary);
    ui->restoreSummary->style()->polish(ui->restoreSummary);
    ui->restoreScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::runRestorePointCreate()
{
    if (m_restoreBusy || m_task == Task::RestoreCreate)
        return;

    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/restorepoint.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少还原点脚本",
                    "未找到 ps1\\restorepoint.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!confirmAction(
            "创建系统还原点",
            QStringLiteral("会在当前系统状态下创建一个还原点，之后可以用它把系统设置"
                           "和已安装的软件恢复到现在的状态。\n\n"
                           "个人文件（文档、照片等）不会受影响。如果系统还原原本是"
                           "关闭的，程序会自动为你开启。\n\n"
                           "创建时会弹出管理员权限请求，请选「是」，"
                           "通常十几秒到一两分钟。\n\n继续吗？")))
        return;

    if (!claimTask(QStringLiteral("创建系统还原点")))
        return;

    ++m_flow;

    m_restoreBusy = true;
    ui->restoreCreateBtn->setEnabled(false);
    ui->restoreSummary->setText(
        QStringLiteral("正在创建还原点，请稍候……若弹出管理员权限请求，请选「是」。"));
    ui->restoreSummary->setProperty("state", QString());
    ui->restoreSummary->style()->unpolish(ui->restoreSummary);
    ui->restoreSummary->style()->polish(ui->restoreSummary);
    showSystemResultCard(ui->cardRestore);

    appendLog("-------------------");
    appendLog("正在创建系统还原点，请稍候……");

    // 说明文字只用 ASCII：它会经命令行传给 PowerShell，避免编码问题
    const QString description =
        QStringLiteral("WindowsTool ")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("create"),
        QStringLiteral("-Description"), description
    };

    m_task = Task::RestoreCreate;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleRestorePointCreate()
{
    m_restoreBusy = false;
    ui->restoreCreateBtn->setEnabled(true);

    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("RESTOREPOINT_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);

    QString reason;
    QString state = QStringLiteral("risk");
    bool ok = false;
    if (!payload.isEmpty() && parseError.error == QJsonParseError::NoError
        && document.isObject()) {
        const QJsonObject data = document.object();
        ok = data.value(QStringLiteral("ok")).toBool();
        const QString code = data.value(QStringLiteral("code")).toString();

        if (!ok) {
            if (code == QLatin1String("canceled"))
                reason = QStringLiteral("取消了管理员权限请求，还原点没有创建。");
            else if (code == QLatin1String("enable-failed"))
                reason = QStringLiteral("未能开启系统还原功能，还原点没有创建。");
            else if (code == QLatin1String("failed"))
                reason = QStringLiteral("Windows 未能创建还原点，可稍后重试；"
                                        "也请确认 C 盘剩余空间充足。");
            else if (code == QLatin1String("no-result"))
                reason = QStringLiteral("管理员进程没有返回结果，请重试。");
            else
                reason = QStringLiteral("创建失败（%1）。")
                             .arg(code.isEmpty() ? QStringLiteral("未知原因") : code);
        }
    } else {
        reason = QStringLiteral("未能解析脚本输出，请重试。");
    }

    if (ok) {
        state = QStringLiteral("ok");
        ui->restoreSummary->setText(
            QStringLiteral("还原点创建成功，正在刷新列表……"));
        appendLog("-------------------");
        appendLog("系统还原点创建成功。");
        recordOperation(QStringLiteral("系统还原"),
                        QStringLiteral("创建系统还原点"),
                        QStringLiteral("系统盘"), true);

        // 等本次任务状态复位后再读取列表，避免覆盖当前结果
        QTimer::singleShot(0, this, &MainWindow::runRestorePointList);
    } else {
        ui->restoreSummary->setText(
            QStringLiteral("还原点创建失败：%1").arg(reason));
        appendLog("-------------------");
        appendLog(QStringLiteral("创建系统还原点失败：%1").arg(reason));
    }

    ui->restoreSummary->setProperty("state", state);
    ui->restoreSummary->style()->unpolish(ui->restoreSummary);
    ui->restoreSummary->style()->polish(ui->restoreSummary);
}

void MainWindow::addRestoreRow(const QString &time, const QString &description,
                               const QString &detail)
{
    QWidget *row = new QWidget(ui->restoreRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(time, row);
    badge->setObjectName(QStringLiteral("restoreBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(124);

    QLabel *title = new QLabel(description, row);
    title->setObjectName(QStringLiteral("restoreName"));
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("restoreDetail"));
    desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->restoreRowsLayout->addWidget(row);
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

void MainWindow::showBusyHint()
{
    if (m_busyShown)
        return;

    m_busyShown = true;
    ui->logPanel->setVisible(true);

    // 面板刚显示时先滚到最新一行，让用户立刻看到"正在……"的提示
    QScrollBar *bar = ui->textBrowser->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MainWindow::hideBusyHint()
{
    if (!m_busyShown)
        return;

    m_busyShown = false;
    ui->logPanel->setVisible(false);
}

void MainWindow::showMessage(const QString &title, const QString &text,
                             QMessageBox::Icon icon)
{
    // 后台任务完成时用户可能正开着别的对话框；此时直接弹会叠在一起。
    // 先等当前模态框关闭，再显示本条提示。
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(300, this, [this, title, text, icon] {
            showMessage(title, text, icon);
        });
        return;
    }

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

// ---------------------------------------------------------------- 实用工具

namespace {

/** 右键菜单作用范围（注册表路径代码）→ 中文标签 */
QString contextScopeLabel(const QString &scope)
{
    if (scope == QLatin1String("*"))
        return QStringLiteral("所有文件");
    if (scope == QLatin1String("Directory"))
        return QStringLiteral("文件夹");
    if (scope == QLatin1String("Directory\\Background"))
        return QStringLiteral("文件夹空白处");
    if (scope == QLatin1String("Drive"))
        return QStringLiteral("驱动器");
    if (scope == QLatin1String("Folder"))
        return QStringLiteral("文件夹(兼容)");
    if (scope == QLatin1String("AllFilesystemObjects"))
        return QStringLiteral("所有对象");
    if (scope == QLatin1String("DesktopBackground"))
        return QStringLiteral("桌面空白处");
    return scope;
}

/** 占用进程类型（脚本输出英文枚举）→ 中文标签 */
QString lockTypeLabel(const QString &code)
{
    if (code == QLatin1String("window"))
        return QStringLiteral("窗口程序");
    if (code == QLatin1String("service"))
        return QStringLiteral("系统服务");
    if (code == QLatin1String("explorer"))
        return QStringLiteral("资源管理器");
    if (code == QLatin1String("console"))
        return QStringLiteral("控制台程序");
    if (code == QLatin1String("critical"))
        return QStringLiteral("系统关键进程");
    return QStringLiteral("应用程序");
}

/** 过长的命令行截断，避免把整行撑破 */
QString elideCommand(const QString &command, int maxChars = 120)
{
    if (command.size() <= maxChars)
        return command;
    return command.left(maxChars) + QStringLiteral("…");
}

} // namespace

void MainWindow::setupTools()
{
    // 结果行数少于滚动区高度时，保持自然高度而不是被拉高铺满
    ui->contextMenuRowsLayout->setAlignment(Qt::AlignTop);
    ui->bootTimeRowsLayout->setAlignment(Qt::AlignTop);
    ui->fileLockRowsLayout->setAlignment(Qt::AlignTop);

    // C 盘空间图：自绘矩形树图，点击文件夹方块逐层进入
    m_treemap = new TreemapWidget(this);
    ui->treemapLayout->addWidget(m_treemap);
    connect(m_treemap, &TreemapWidget::directoryActivated,
            this, &MainWindow::runTreemap);

    connect(ui->btn_treemap, &QAbstractButton::clicked, this, [this]() {
        runTreemap(QStringLiteral("C:\\"));
    });
    connect(ui->treemapUpBtn, &QPushButton::clicked, this, [this]() {
        QDir dir(m_treemapRoot);
        if (dir.cdUp())
            runTreemap(dir.absolutePath());
    });
    connect(ui->treemapPickBtn, &QPushButton::clicked, this, [this]() {
        const QString root = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择要看占用情况的目录"),
            m_treemapRoot.isEmpty() ? QDir::homePath() : m_treemapRoot);
        if (!root.isEmpty())
            runTreemap(root);
    });

    // 右键菜单管理
    connect(ui->btn_contextMenu, &QAbstractButton::clicked,
            this, &MainWindow::runContextMenuList);

    // 开机耗时分析
    connect(ui->btn_bootTime, &QAbstractButton::clicked,
            this, &MainWindow::runBootTime);

    // 文件占用：先展开卡片，再由用户选择文件
    connect(ui->btn_fileLock, &QAbstractButton::clicked, this, [this]() {
        while (QLayoutItem *item = ui->fileLockRowsLayout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        ui->fileLockSummary->setText(
            QStringLiteral("选择一个文件，查看是哪个进程正在占用它"));
        ui->cardFileLock->setVisible(true);
        QTimer::singleShot(0, this, [this] {
            ui->pageTools->ensureWidgetVisible(ui->cardFileLock);
        });
        ui->fileLockPath->setFocus();
    });

    connect(ui->fileLockBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择要检查的文件"), QString(),
            QStringLiteral("所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        ui->fileLockPath->setText(path);
        runFileLock(path);
    });

    connect(ui->fileLockCheckBtn, &QPushButton::clicked, this, [this]() {
        const QString path = ui->fileLockPath->text().trimmed();
        if (path.isEmpty()) {
            showMessage("请先选择文件",
                        "请点击「选择文件」或直接粘贴一个完整的文件路径。",
                        QMessageBox::Information);
            ui->fileLockPath->setFocus();
            return;
        }
        runFileLock(path);
    });

    // 查伪装软件
    connect(ui->btn_fakeCheck, &QAbstractButton::clicked, this, [this]() {
        while (QLayoutItem *item = ui->fakeCheckRowsLayout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                widget->deleteLater();
            delete item;
        }
        ui->fakeCheckSummary->setText(
            QStringLiteral("检查单个文件，或扫描桌面、下载、启动文件夹里的可执行文件"));
        ui->fakeCheckSummary->setProperty("state", QString());
        ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
        m_verifyPath.clear();
        m_verifyHash.clear();
        ui->fakeCheckHash->setText(QStringLiteral("SHA-256：—"));
        ui->fakeCheckExpected->clear();
        setFakeCheckRunning(false);
        ui->fakeCheckCompare->setText(
            QStringLiteral("哈希用于核对文件是否与官网发布的一致，全程不会上传文件。"));
        ui->fakeCheckCompare->setProperty("state", QString());
        ui->fakeCheckCompare->style()->unpolish(ui->fakeCheckCompare);
        ui->fakeCheckCompare->style()->polish(ui->fakeCheckCompare);
        ui->cardFakeCheck->setVisible(true);
        QTimer::singleShot(0, this, [this] {
            ui->pageTools->ensureWidgetVisible(ui->cardFakeCheck);
        });
    });

    connect(ui->fakeCheckPickBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择要检查的文件"), QString(),
            QStringLiteral("所有文件 (*.*)"));
        if (!path.isEmpty())
            runFakeCheck(path);
    });

    connect(ui->fakeCheckScanBtn, &QPushButton::clicked, this, [this]() {
        runFakeCheck(QString());
    });

    connect(ui->fakeCheckCopyBtn, &QPushButton::clicked,
            this, &MainWindow::copyFakeCheckHash);

    connect(ui->fakeCheckExpected, &QLineEdit::textChanged,
            this, &MainWindow::updateFakeCheckCompare);

    setFakeCheckRunning(false);
}

// ---------------------------------------------------------------- 右键菜单管理

void MainWindow::runContextMenuList()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/contextmenu.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少右键菜单脚本",
                    "未找到 ps1\\contextmenu.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("读取右键菜单项")))
        return;

    ++m_flow;

    clearLog();
    appendLog("正在读取右键菜单项，请稍候……");

    while (QLayoutItem *item = ui->contextMenuRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->contextMenuSummary->setText(QStringLiteral("正在读取……"));
    ui->cardContextMenu->setVisible(true);
    QTimer::singleShot(0, this, [this] {
        ui->pageTools->ensureWidgetVisible(ui->cardContextMenu);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), QStringLiteral("list")
    };

    m_task = Task::ContextMenuList;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleContextMenuList()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("CONTEXTMENU_JSON:");
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
        appendLog("读取右键菜单失败：未能解析脚本输出。");
        ui->contextMenuSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取右键菜单失败",
                    "未能从 PowerShell 获取右键菜单列表。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    QJsonArray items;
    if (document.isArray())
        items = document.array();
    else if (document.isObject())
        items.append(document.object());

    int total = 0;
    int disabledCount = 0;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString scope = object.value(QStringLiteral("scope")).toString();
        const QString entry = object.value(QStringLiteral("entry")).toString();
        if (entry.isEmpty())
            continue;

        QString badge = contextScopeLabel(scope);
        if (object.value(QStringLiteral("admin")).toBool())
            badge += QStringLiteral(" · 系统");

        const QString command = object.value(QStringLiteral("command")).toString();
        const QString detail = command.isEmpty()
                                   ? QStringLiteral("没有注册命令行（可能是系统内置项）")
                                   : elideCommand(command);

        const bool disabled = object.value(QStringLiteral("disabled")).toBool();
        if (disabled)
            ++disabledCount;

        addContextMenuRow(badge,
                          object.value(QStringLiteral("display")).toString(),
                          detail,
                          object.value(QStringLiteral("root")).toString(),
                          scope, entry, disabled);
        ++total;
    }

    QString summary;
    if (total == 0)
        summary = QStringLiteral("未发现可管理的右键菜单项");
    else
        summary = QStringLiteral("共 %1 项 · 已禁用 %2 项 · 带「系统」标记的项需要管理员权限才能切换")
                      .arg(total)
                      .arg(disabledCount);

    ui->contextMenuSummary->setText(summary);
    ui->contextMenuScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addContextMenuRow(const QString &badge, const QString &display,
                                   const QString &detail, const QString &root,
                                   const QString &scope, const QString &entry,
                                   bool disabled)
{
    QWidget *row = new QWidget(ui->contextMenuRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badgeLabel = new QLabel(badge, row);
    badgeLabel->setObjectName(QStringLiteral("cmBadge"));
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setFixedWidth(104);

    QLabel *title = new QLabel(display.isEmpty() ? entry : display, row);
    title->setObjectName(QStringLiteral("cmName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("cmDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    QPushButton *toggle = new QPushButton(disabled ? QStringLiteral("启用")
                                                   : QStringLiteral("禁用"), row);
    toggle->setObjectName(QStringLiteral("cmToggleBtn"));
    toggle->setFixedWidth(64);
    connect(toggle, &QPushButton::clicked, this,
            [this, root, scope, entry, display, disabled] {
                m_cmDisplay = display.isEmpty() ? entry : display;
                runContextMenuToggle(root, scope, entry, !disabled);
            });

    layout->addWidget(badgeLabel, 0, Qt::AlignTop);
    layout->addLayout(text, 1);
    layout->addWidget(toggle, 0, Qt::AlignTop);

    const int insertAt = qMax(0, ui->contextMenuRowsLayout->count() - 1);
    ui->contextMenuRowsLayout->insertWidget(insertAt, row);
}

void MainWindow::runContextMenuToggle(const QString &root, const QString &scope,
                                      const QString &entry, bool disable)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/contextmenu.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少右键菜单脚本",
                    "未找到 ps1\\contextmenu.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("修改右键菜单项")))
        return;

    m_cmRoot = root;
    m_cmScope = scope;
    m_cmEntry = entry;
    m_cmDisable = disable;

    ++m_flow;

    clearLog();
    appendLog(disable ? QStringLiteral("正在禁用右键菜单项：%1").arg(entry)
                      : QStringLiteral("正在启用右键菜单项：%1").arg(entry));

    ui->contextMenuSummary->setText(disable ? QStringLiteral("正在禁用……")
                                            : QStringLiteral("正在启用……"));

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), disable ? QStringLiteral("disable")
                                           : QStringLiteral("enable"),
        QStringLiteral("-Root"), root,
        QStringLiteral("-Scope"), scope,
        QStringLiteral("-Entry"), entry
    };

    m_task = Task::ContextMenuToggle;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleContextMenuToggle()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("CONTEXTMENU_ACTION_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || !document.isObject()) {
        m_undoId.clear();
        m_undoKind.clear();
        ui->contextMenuSummary->setText(QStringLiteral("操作失败"));
        showMessage("操作失败",
                    "未能从 PowerShell 获取操作结果，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject object = document.object();
    const bool ok = object.value(QStringLiteral("ok")).toBool();
    if (!ok) {
        const bool undoMode = !m_undoId.isEmpty();
        m_undoId.clear();
        m_undoKind.clear();
        ui->contextMenuSummary->setText(QStringLiteral("操作失败"));
        showMessage(undoMode ? "撤销失败" : "修改右键菜单失败",
                    "系统返回：" + object.value(QStringLiteral("message")).toString()
                        + "\n\n标记为「系统」的项目要管理员权限才能改，"
                          "请右键用「以管理员身份运行」重新打开本程序。",
                    QMessageBox::Warning);
        return;
    }

    if (!m_undoId.isEmpty()) {
        const QString undoId = m_undoId;
        m_undoId.clear();
        m_undoKind.clear();
        markOperationUndone(undoId);
        refreshOperationPage();
        showMessage("已撤销",
                    QStringLiteral("右键菜单项已恢复到操作前的状态，正在刷新列表。"));
    } else {
        QJsonObject undoArgs;
        undoArgs.insert(QStringLiteral("root"), m_cmRoot);
        undoArgs.insert(QStringLiteral("scope"), m_cmScope);
        undoArgs.insert(QStringLiteral("entry"), m_cmEntry);
        undoArgs.insert(QStringLiteral("display"), m_cmDisplay);
        undoArgs.insert(QStringLiteral("disable"), !m_cmDisable);

        recordOperation(
            QStringLiteral("右键菜单"),
            m_cmDisable ? QStringLiteral("禁用右键菜单项")
                        : QStringLiteral("启用右键菜单项"),
            m_cmDisplay.isEmpty() ? m_cmEntry : m_cmDisplay,
            true, true, QStringLiteral("contextmenu"), undoArgs);
    }

    // 重新读取一遍，让列表里的禁用状态与注册表保持一致。
    // 必须延后到下一轮事件循环：本函数在 onTaskFinished 内被调用，
    // 它返回后会把 m_task 重置为 None。
    QTimer::singleShot(0, this, [this]() { runContextMenuList(); });
}

// ---------------------------------------------------------------- 开机耗时

void MainWindow::runBootTime()
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/boottime.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少开机耗时脚本",
                    "未找到 ps1\\boottime.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!claimTask(QStringLiteral("分析开机耗时")))
        return;

    ++m_flow;

    clearLog();
    appendLog("正在分析开机耗时，请稍候……");

    while (QLayoutItem *item = ui->bootTimeRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->bootTimeSummary->setText(QStringLiteral("正在读取……"));
    ui->cardBootTime->setVisible(true);
    QTimer::singleShot(0, this, [this] {
        ui->pageTools->ensureWidgetVisible(ui->cardBootTime);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script
    };

    m_task = Task::BootTime;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleBootTime()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("BOOTTIME_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || !document.isObject()) {
        appendLog("-------------------");
        appendLog("读取开机耗时失败：未能解析脚本输出。");
        ui->bootTimeSummary->setText(QStringLiteral("读取失败"));
        showMessage("读取开机耗时失败",
                    "未能从 PowerShell 获取开机时间信息。\n\n"
                    "请确认系统允许运行 PowerShell 脚本，或稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject object = document.object();
    const QString bootTime = object.value(QStringLiteral("bootTime")).toString();
    if (bootTime.isEmpty()) {
        ui->bootTimeSummary->setText(QStringLiteral("读取失败：没有拿到开机时间"));
        showMessage("读取开机耗时失败",
                    "系统没有返回有效的开机时间，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const bool reliable = object.value(QStringLiteral("desktopReliable")).toBool();
    const int desktopSeconds = object.value(QStringLiteral("desktopSeconds")).toInt(-1);
    const qint64 uptimeSeconds =
        static_cast<qint64>(object.value(QStringLiteral("uptimeMinutes")).toDouble()) * 60;
    const int startupCount = object.value(QStringLiteral("startupCount")).toInt();

    QString summary;
    if (reliable && desktopSeconds >= 0)
        summary = QStringLiteral("上次开机 %1 · 进入桌面约 %2 秒")
                      .arg(bootTime)
                      .arg(desktopSeconds);
    else
        summary = QStringLiteral("上次开机 %1 · 资源管理器已重启，本次无法估算进入桌面耗时")
                      .arg(bootTime);
    ui->bootTimeSummary->setText(summary);

    addBootRow(QStringLiteral("开机"), bootTime,
               QStringLiteral("系统内核记录的本次启动时间"));

    if (reliable && desktopSeconds >= 0) {
        QString verdict;
        if (desktopSeconds <= 15)
            verdict = QStringLiteral("很快，系统启动顺畅");
        else if (desktopSeconds <= 30)
            verdict = QStringLiteral("正常范围");
        else if (desktopSeconds <= 60)
            verdict = QStringLiteral("偏慢，可考虑精简启动项");
        else
            verdict = QStringLiteral("明显偏慢，建议检查启动项与磁盘状态");
        addBootRow(QStringLiteral("%1 秒").arg(desktopSeconds),
                   QStringLiteral("进入桌面"), verdict);
    } else {
        addBootRow(QStringLiteral("—"), QStringLiteral("进入桌面"),
                   QStringLiteral("资源管理器在开机后重启过，重启电脑后再查看即可获得准确耗时"));
    }

    addBootRow(QStringLiteral("已运行"), formatUptime(uptimeSeconds),
               QStringLiteral("距上次开机已运行时长"));

    addBootRow(QStringLiteral("%1 个").arg(startupCount), QStringLiteral("开机自启动"),
               startupCount > 15
                   ? QStringLiteral("启动项偏多，可在「系统管理 → 启动项管理」里逐项确认")
                   : QStringLiteral("数量正常，可在「系统管理 → 启动项管理」里查看明细"));

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addBootRow(const QString &badge, const QString &title,
                            const QString &detail)
{
    QWidget *row = new QWidget(ui->bootTimeRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badgeLabel = new QLabel(badge, row);
    badgeLabel->setObjectName(QStringLiteral("bootBadge"));
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setFixedWidth(88);

    QLabel *titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("bootName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("bootDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(titleLabel);
    text->addWidget(desc);

    layout->addWidget(badgeLabel, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    const int insertAt = qMax(0, ui->bootTimeRowsLayout->count() - 1);
    ui->bootTimeRowsLayout->insertWidget(insertAt, row);
}

// ---------------------------------------------------------------- 文件占用

void MainWindow::runFileLock(const QString &path)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/filelock.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少占用查询脚本",
                    "未找到 ps1\\filelock.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!QFileInfo::exists(path)) {
        ui->fileLockSummary->setText(QStringLiteral("文件不存在，请重新选择"));
        showMessage("文件不存在",
                    "找不到这个文件，可能已被移动或删除。\n\n" + path,
                    QMessageBox::Warning);
        return;
    }

    if (!claimTask(QStringLiteral("查找文件占用")))
        return;

    ++m_flow;

    clearLog();
    appendLog(QStringLiteral("正在查询占用：%1").arg(path));

    while (QLayoutItem *item = ui->fileLockRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    ui->fileLockSummary->setText(QStringLiteral("正在查询……"));
    ui->cardFileLock->setVisible(true);
    QTimer::singleShot(0, this, [this] {
        ui->pageTools->ensureWidgetVisible(ui->cardFileLock);
    });

    const QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Path"), path
    };

    m_task = Task::FileLock;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleFileLock()
{
    const QString output = m_runner->collectedOutput();
    const QString marker = QStringLiteral("FILELOCK_JSON:");
    const int markerPos = output.indexOf(marker);

    QString payload;
    if (markerPos >= 0) {
        payload = output.mid(markerPos + marker.size())
                      .section(QLatin1Char('\n'), 0, 0)
                      .trimmed();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.isEmpty() || !document.isObject()) {
        appendLog("-------------------");
        appendLog("查询文件占用失败：未能解析脚本输出。");
        ui->fileLockSummary->setText(QStringLiteral("查询失败"));
        showMessage("查询文件占用失败",
                    "未能从 PowerShell 获取查询结果，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject object = document.object();
    const int status = object.value(QStringLiteral("status")).toInt();
    const QString path = object.value(QStringLiteral("path")).toString();

    if (status != 0) {
        ui->fileLockSummary->setText(QStringLiteral("查询失败（错误码 %1）").arg(status));
        showMessage("查询文件占用失败",
                    QStringLiteral("系统接口返回错误码 %1。\n\n"
                                   "如果查询的是系统目录或被系统进程占用的文件，"
                                   "可尝试以管理员身份运行本程序后再查询。")
                        .arg(status),
                    QMessageBox::Warning);
        return;
    }

    const QJsonValue processesValue = object.value(QStringLiteral("processes"));
    QJsonArray processes;
    if (processesValue.isArray())
        processes = processesValue.toArray();
    else if (processesValue.isObject())
        processes.append(processesValue.toObject());

    int total = 0;
    for (const QJsonValue &value : processes) {
        if (!value.isObject())
            continue;
        const QJsonObject process = value.toObject();
        const int pid = process.value(QStringLiteral("pid")).toInt();
        const QString name = process.value(QStringLiteral("name")).toString();
        const QString type = process.value(QStringLiteral("type")).toString();
        const QString app = process.value(QStringLiteral("app")).toString();

        QString detail = QStringLiteral("PID %1").arg(pid);
        if (!app.isEmpty())
            detail += QStringLiteral(" · %1").arg(app);

        addFileLockRow(lockTypeLabel(type),
                       name.isEmpty() ? QStringLiteral("(未知进程)") : name,
                       detail);
        ++total;
    }

    const QString fileName = QFileInfo(path).fileName();
    QString summary;
    if (total == 0)
        summary = QStringLiteral("没有进程占用 %1，可以正常删除或重命名").arg(fileName);
    else
        summary = QStringLiteral("%1 正被 %2 个进程占用 · 关闭对应程序后即可操作")
                      .arg(fileName)
                      .arg(total);

    ui->fileLockSummary->setText(summary);
    ui->fileLockScroll->verticalScrollBar()->setValue(0);

    appendLog("-------------------");
    appendLog(summary);
}

void MainWindow::addFileLockRow(const QString &badge, const QString &name,
                                const QString &detail)
{
    QWidget *row = new QWidget(ui->fileLockRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badgeLabel = new QLabel(badge, row);
    badgeLabel->setObjectName(QStringLiteral("lockBadge"));
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setFixedWidth(104);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("lockName"));

    QLabel *desc = new QLabel(detail, row);
    desc->setObjectName(QStringLiteral("lockDetail"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(desc);

    layout->addWidget(badgeLabel, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    const int insertAt = qMax(0, ui->fileLockRowsLayout->count() - 1);
    ui->fileLockRowsLayout->insertWidget(insertAt, row);
}

// ---------------------------------------------------------------- 查伪装软件

void MainWindow::runFakeCheck(const QString &path)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("ps1/fakecheck.ps1"));

    if (!QFileInfo::exists(script)) {
        showMessage("缺少伪装检查脚本",
                    "未找到 ps1\\fakecheck.ps1。\n\n"
                    "请确认程序目录下的 ps1 文件夹完整，或重新获取程序。",
                    QMessageBox::Critical);
        return;
    }

    if (!path.isEmpty() && !QFileInfo::exists(path)) {
        ui->fakeCheckSummary->setText(QStringLiteral("文件不存在，请重新选择"));
        showMessage("文件不存在",
                    "找不到这个文件，可能已被移动或删除。\n\n" + path,
                    QMessageBox::Warning);
        return;
    }

    if (!claimTask(path.isEmpty() ? QStringLiteral("扫描伪装软件")
                                  : QStringLiteral("检查文件是否被伪装")))
        return;

    ++m_flow;

    m_verifyPath = path;
    m_verifyHash.clear();

    while (QLayoutItem *item = ui->fakeCheckRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    ui->fakeCheckHash->setText(QStringLiteral("SHA-256：—"));
    ui->fakeCheckSummary->setProperty("state", QString());
    ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
    ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);

    setFakeCheckRunning(true);
    updateFakeCheckCompare();

    ui->fakeCheckSummary->setText(
        path.isEmpty() ? QStringLiteral("正在扫描桌面、下载与启动文件夹……")
                       : QStringLiteral("正在检查 %1……").arg(QFileInfo(path).fileName()));
    ui->cardFakeCheck->setVisible(true);
    QTimer::singleShot(0, this, [this] {
        ui->pageTools->ensureWidgetVisible(ui->cardFakeCheck);
    });

    if (path.isEmpty()) {
        appendLog("-------------------");
        appendLog("开始扫描常见位置的伪装软件。");
    } else {
        appendLog("-------------------");
        appendLog(QStringLiteral("正在检查：%1").arg(path));
    }

    m_verifyResultFile =
        QDir::tempPath() + QStringLiteral("/wt-fakecheck-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");

    QStringList args = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        QStringLiteral("-Action"), path.isEmpty() ? QStringLiteral("scan")
                                                 : QStringLiteral("file"),
        QStringLiteral("-Result"), m_verifyResultFile
    };
    if (!path.isEmpty())
        args << QStringLiteral("-Path") << path;

    m_task = Task::FileVerify;
    m_runner->run(QStringLiteral("powershell.exe"), args);
}

void MainWindow::handleFakeCheck()
{
    setFakeCheckRunning(false);

    const QString resultPath = m_verifyResultFile;
    m_verifyResultFile.clear();

    QString payload;
    if (!resultPath.isEmpty()) {
        QFile file(resultPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            payload = QString::fromUtf8(file.readAll());
        file.remove();
    }

    if (payload.trimmed().isEmpty()) {
        const QString output = m_runner->collectedOutput();
        const QString marker = QStringLiteral("FAKECHECK_JSON:");
        const int markerPos = output.indexOf(marker);
        if (markerPos >= 0) {
            payload = output.mid(markerPos + marker.size())
                          .section(QLatin1Char('\n'), 0, 0)
                          .trimmed();
        }
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (payload.trimmed().isEmpty() || !document.isObject()) {
        ui->fakeCheckSummary->setProperty("state", QStringLiteral("fail"));
        ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->setText(QStringLiteral("检查失败"));
        appendLog("检查失败：未能解析脚本输出。");
        showMessage("检查失败",
                    "未能从 PowerShell 获取检查结果，请稍后重试。",
                    QMessageBox::Warning);
        return;
    }

    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        const QString code = object.value(QStringLiteral("code")).toString();
        QString reason;
        if (code == QLatin1String("no-path"))
            reason = QStringLiteral("没有指定要检查的文件");
        else if (code == QLatin1String("not-found"))
            reason = QStringLiteral("文件不存在，可能已被移动或删除");
        else if (code == QLatin1String("read-failed"))
            reason = QStringLiteral("无法读取文件，可能被占用或没有访问权限");
        else
            reason = QStringLiteral("检查失败（%1）").arg(code);

        ui->fakeCheckSummary->setProperty("state", QStringLiteral("fail"));
        ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->setText(reason);
        appendLog(reason);
        showMessage("检查失败", reason, QMessageBox::Warning);
        return;
    }

    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    const QString mode = object.value(QStringLiteral("mode")).toString();

    renderFakeCheck(files);

    int normal = 0;
    int suspicious = 0;
    int high = 0;
    for (const QJsonValue &value : files) {
        const QString risk = value.toObject().value(QStringLiteral("risk")).toString();
        if (risk == QLatin1String("high"))
            ++high;
        else if (risk == QLatin1String("suspicious"))
            ++suspicious;
        else
            ++normal;
    }

    QString summary;
    QString state = QStringLiteral("ok");
    if (files.isEmpty()) {
        summary = mode == QLatin1String("scan")
                      ? QStringLiteral("桌面、下载与启动文件夹里没有可检查的可执行文件")
                      : QStringLiteral("没有可检查的文件");
    } else if (mode == QLatin1String("scan")) {
        summary = QStringLiteral("扫描了 %1 个可执行文件：正常 %2 · 可疑 %3 · 高风险 %4")
                      .arg(files.size()).arg(normal).arg(suspicious).arg(high);
    } else {
        const QJsonObject first = files.first().toObject();
        summary = QStringLiteral("%1：%2")
                      .arg(first.value(QStringLiteral("name")).toString(),
                           fakeCheckRiskLabel(first.value(QStringLiteral("risk")).toString()));
    }

    if (high > 0)
        state = QStringLiteral("fail");
    else if (suspicious > 0)
        state = QStringLiteral("warn");

    ui->fakeCheckSummary->setProperty("state", state);
    ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
    ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
    ui->fakeCheckSummary->setText(summary);
    ui->fakeCheckScroll->verticalScrollBar()->setValue(0);

    m_verifyHash.clear();
    if (files.size() == 1) {
        const QJsonObject first = files.first().toObject();
        m_verifyHash = first.value(QStringLiteral("hash")).toString();
        if (!m_verifyHash.isEmpty())
            ui->fakeCheckHash->setText(QStringLiteral("SHA-256：%1").arg(m_verifyHash));
        else
            ui->fakeCheckHash->setText(QStringLiteral("SHA-256：—"));
    } else {
        ui->fakeCheckHash->setText(QStringLiteral("SHA-256：—"));
    }
    m_verifyPath = files.size() == 1
                       ? files.first().toObject().value(QStringLiteral("path")).toString()
                       : QString();
    setFakeCheckRunning(false);
    updateFakeCheckCompare();

    appendLog("-------------------");
    appendLog(summary);
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QString risk = file.value(QStringLiteral("risk")).toString();
        if (risk == QLatin1String("high") || risk == QLatin1String("suspicious")) {
            const QJsonArray reasons = file.value(QStringLiteral("reasons")).toArray();
            QStringList labels;
            for (const QJsonValue &reason : reasons)
                labels << fakeCheckReasonLabel(reason.toString());
            appendLog(QStringLiteral("%1：%2（%3）")
                          .arg(fakeCheckRiskLabel(risk),
                               file.value(QStringLiteral("path")).toString(),
                               labels.join(QStringLiteral("；"))));
        }
    }
}

void MainWindow::renderFakeCheck(const QJsonArray &files)
{
    if (files.isEmpty()) {
        QLabel *empty = new QLabel(
            QStringLiteral("没有找到可检查的文件。"), ui->fakeCheckRows);
        empty->setObjectName(QStringLiteral("verifyDetail"));
        empty->setWordWrap(true);
        ui->fakeCheckRowsLayout->addWidget(empty);
        return;
    }

    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QJsonArray reasons = file.value(QStringLiteral("reasons")).toArray();
        QStringList labels;
        for (const QJsonValue &reason : reasons)
            labels << fakeCheckReasonLabel(reason.toString());

        QString detail;
        if (!labels.isEmpty()) {
            detail = labels.join(QStringLiteral("；"));
        } else if (file.value(QStringLiteral("sigStatus")).toString()
                       == QLatin1String("Valid")) {
            detail = QStringLiteral("数字签名有效，未发现明显伪装特征");
        } else {
            detail = QStringLiteral("未发现明显伪装特征");
        }

        const QString signer = file.value(QStringLiteral("signer")).toString();
        if (!signer.isEmpty())
            detail += QStringLiteral(" · 签名者：%1").arg(signer);

        addFakeCheckRow(file.value(QStringLiteral("risk")).toString(),
                        file.value(QStringLiteral("name")).toString(),
                        file.value(QStringLiteral("path")).toString(),
                        detail,
                        file.value(QStringLiteral("hash")).toString());
    }
}

void MainWindow::addFakeCheckRow(const QString &risk, const QString &name,
                                 const QString &path, const QString &detail,
                                 const QString &hash)
{
    QWidget *row = new QWidget(ui->fakeCheckRows);
    QHBoxLayout *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *badge = new QLabel(fakeCheckRiskLabel(risk), row);
    badge->setObjectName(QStringLiteral("verifyBadge"));
    badge->setProperty("state", fakeCheckRiskState(risk));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(64);

    QLabel *title = new QLabel(name, row);
    title->setObjectName(QStringLiteral("verifyName"));
    title->setWordWrap(true);

    QLabel *pathLabel = new QLabel(path, row);
    pathLabel->setObjectName(QStringLiteral("verifyDetail"));
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QLabel *detailLabel = new QLabel(detail, row);
    detailLabel->setObjectName(QStringLiteral("verifyDetail"));
    detailLabel->setWordWrap(true);
    detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(title);
    text->addWidget(pathLabel);
    text->addWidget(detailLabel);

    if (!hash.isEmpty()) {
        QLabel *hashLabel = new QLabel(
            QStringLiteral("SHA-256：%1").arg(hash), row);
        hashLabel->setObjectName(QStringLiteral("verifyHashValue"));
        hashLabel->setWordWrap(true);
        hashLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->addWidget(hashLabel);
    }

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(text, 1);

    ui->fakeCheckRowsLayout->addWidget(row);
}

void MainWindow::setFakeCheckRunning(bool running)
{
    ui->fakeCheckPickBtn->setEnabled(!running);
    ui->fakeCheckScanBtn->setEnabled(!running);
    ui->fakeCheckCopyBtn->setEnabled(!running && !m_verifyHash.isEmpty());
    ui->fakeCheckExpected->setEnabled(!running && !m_verifyPath.isEmpty());
}

void MainWindow::updateFakeCheckCompare()
{
    auto setState = [this](const QString &state) {
        ui->fakeCheckCompare->setProperty("state", state);
        ui->fakeCheckCompare->style()->unpolish(ui->fakeCheckCompare);
        ui->fakeCheckCompare->style()->polish(ui->fakeCheckCompare);
    };

    const QString expected = normalizeHashText(ui->fakeCheckExpected->text());
    if (expected.isEmpty()) {
        setState(QString());
        ui->fakeCheckCompare->setText(
            QStringLiteral("哈希用于核对文件是否与官网发布的一致，全程不会上传文件。"));
        return;
    }

    if (m_verifyHash.isEmpty()) {
        setState(QStringLiteral("warn"));
        ui->fakeCheckCompare->setText(
            QStringLiteral("请先检查单个文件，再粘贴哈希进行比对。"));
        return;
    }

    if (expected.size() != m_verifyHash.size()) {
        setState(QStringLiteral("fail"));
        ui->fakeCheckCompare->setText(
            QStringLiteral("粘贴的哈希为 %1 位，与当前 SHA-256（%2 位）不一致，"
                           "请确认复制的是 SHA-256 值。")
                .arg(expected.size())
                .arg(m_verifyHash.size()));
        return;
    }

    if (expected == m_verifyHash) {
        setState(QStringLiteral("ok"));
        ui->fakeCheckCompare->setText(
            QStringLiteral("一致：文件内容与官网公布的值相同，未被替换。"));
    } else {
        setState(QStringLiteral("fail"));
        ui->fakeCheckCompare->setText(
            QStringLiteral("不一致：文件可能下载不完整或已被篡改，请不要运行，"
                           "建议重新下载。"));
    }
}

void MainWindow::copyFakeCheckHash()
{
    if (m_verifyHash.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(m_verifyHash);
    appendLog(QStringLiteral("已复制 SHA-256：%1").arg(m_verifyHash));
}

// ---------------------------------------------------------------- 任务并发控制

QString MainWindow::currentTaskLabel() const
{
    switch (m_task) {
    case Task::PingAutoCheck:   return QStringLiteral("网络连通性检测");
    case Task::DnsCheck:        return QStringLiteral("DNS 解析检查");
    case Task::NetDiagSnapshot: return QStringLiteral("网络分层诊断");
    case Task::FixDnsAuto:      return QStringLiteral("自动修复网络配置");
    case Task::FixDnsManual:    return QStringLiteral("刷新网络配置");
    case Task::NetInfo:         return QStringLiteral("读取网络信息");
    case Task::Cleaner:         return QStringLiteral("清理系统临时文件");
    case Task::WinsockReset:    return QStringLiteral("重置 Winsock");
    case Task::FixWhite:        return QStringLiteral("重建桌面图标缓存");
    case Task::HealthCheck:     return QStringLiteral("系统体检");
    case Task::StartupList:     return QStringLiteral("读取开机启动项");
    case Task::StartupToggle:   return QStringLiteral("修改开机启动项");
    case Task::PortsList:       return QStringLiteral("检测端口占用");
    case Task::LargeFiles:      return QStringLiteral("扫描大文件");
    case Task::DuplicateScan:   return QStringLiteral("扫描重复文件");
    case Task::DuplicateDelete: return QStringLiteral("删除重复文件");
    case Task::DiskHealth:      return QStringLiteral("检测磁盘健康");
    case Task::HwInfo:          return QStringLiteral("读取硬件信息");
    case Task::Battery:         return QStringLiteral("读取电池信息");
    case Task::ProcessList:     return QStringLiteral("读取进程列表");
    case Task::DiskUsage:       return QStringLiteral("统计磁盘占用");
    case Task::Treemap:         return QStringLiteral("统计目录空间");
    case Task::UninstallList:   return QStringLiteral("读取已安装程序");
    case Task::UninstallAction: return QStringLiteral("卸载程序");
    case Task::DriverList:      return QStringLiteral("读取驱动列表");
    case Task::HostsRead:       return QStringLiteral("读取 Hosts");
    case Task::HostsWrite:      return QStringLiteral("修改 Hosts");
    case Task::DnsRead:         return QStringLiteral("读取 DNS 设置");
    case Task::DnsWrite:        return QStringLiteral("切换 DNS");
    case Task::ProxyRead:       return QStringLiteral("读取系统代理");
    case Task::ProxyWrite:      return QStringLiteral("切换系统代理");
    case Task::SfcRepair:       return QStringLiteral("系统文件检查");
    case Task::WUpdateRepair:   return QStringLiteral("重置更新组件");
    case Task::SxsCleanup:      return QStringLiteral("清理更新组件");
    case Task::RestoreList:     return QStringLiteral("读取系统还原点");
    case Task::RestoreCreate:   return QStringLiteral("创建系统还原点");
    case Task::ContextMenuList: return QStringLiteral("读取右键菜单项");
    case Task::ContextMenuToggle: return QStringLiteral("修改右键菜单项");
    case Task::BootTime:        return QStringLiteral("分析开机耗时");
    case Task::FileLock:        return QStringLiteral("查找文件占用");
    case Task::FileVerify:      return QStringLiteral("检查文件是否被伪装");
    default:                    return QStringLiteral("当前任务");
    }
}

bool MainWindow::claimTask(const QString &nextLabel)
{
    // DNS 探测不占进程、也不占用 m_task，但它仍是诊断流程的一部分。
    // 用户这时点了别的功能，就让新任务直接接管：取消探测并让残留回调失效。
    if (m_diagProbing) {
        m_diagProbing = false;
        if (m_dnsRunner)
            m_dnsRunner->cancel();
        if (m_deepProbe)
            m_deepProbe->cancel();
        if (m_targetRunner)
            m_targetRunner->cancel();
        ++m_flow;
        ui->netDiagSummary->setText(QStringLiteral("诊断被新任务打断，本次结果未生成"));
        ui->netDiagSummary->setProperty("state", QString());
        ui->netDiagSummary->style()->unpolish(ui->netDiagSummary);
        ui->netDiagSummary->style()->polish(ui->netDiagSummary);
    }

    if (m_task == Task::None)
        return true;

    // 已经有一个对话框开着时不再叠一个“还有任务正在运行”，避免弹窗互相遮挡。
    if (QApplication::activeModalWidget()) {
        appendLog(QStringLiteral("「%1」还在运行，请先关闭当前对话框再操作。")
                      .arg(currentTaskLabel()));
        return false;
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("还有任务正在运行"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("「%1」还没有结束。").arg(currentTaskLabel()));
    box.setInformativeText(
        QStringLiteral("现在开始「%1」，可以把它放到后台继续运行，"
                       "也可以立即终止它。要怎么做？").arg(nextLabel));

    QPushButton *stopButton = box.addButton(QStringLiteral("终止当前任务"),
                                            QMessageBox::DestructiveRole);
    QPushButton *bgButton = box.addButton(QStringLiteral("放到后台并继续"),
                                          QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(bgButton);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == stopButton) {
        abortForegroundTask();
        return true;
    }
    if (clicked == bgButton) {
        moveForegroundToBackground();
        return true;
    }
    return false;
}

void MainWindow::abortForegroundTask()
{
    ProcessRunner *old = m_runner;
    if (old) {
        // 先断开回调：旧任务的 finished 不应该再动界面
        disconnect(old, nullptr, this, nullptr);
        old->stop();
        old->deleteLater();
    }
    m_runner = createRunner();

    // 让串联流程里尚未触发的定时器失效，避免旧流程继续启动新命令
    ++m_flow;

    // 被中止的任务可能把某些控件留在"运行中"状态，这里统一复位
    if (m_sfcTimer)
        m_sfcTimer->stop();
    setSfcRunning(false);
    m_restoreBusy = false;
    m_sxsLogPath.clear();
    ui->restoreCreateBtn->setEnabled(true);
    ui->btnWUpdateFix->setEnabled(true);

    if (m_task == Task::FileVerify) {
        m_verifyResultFile.clear();
        setFakeCheckRunning(false);
        ui->fakeCheckSummary->setText(QStringLiteral("检查已终止"));
        ui->fakeCheckSummary->setProperty("state", QString());
        ui->fakeCheckSummary->style()->unpolish(ui->fakeCheckSummary);
        ui->fakeCheckSummary->style()->polish(ui->fakeCheckSummary);
    }

    m_task = Task::None;
    appendLog(QStringLiteral("已终止上一个任务。"));
}

void MainWindow::moveForegroundToBackground()
{
    ProcessRunner *runner = m_runner;
    if (!runner)
        return;

    BackgroundTask entry;
    entry.runner = runner;
    entry.label = currentTaskLabel();
    entry.task = m_task;
    m_background.append(entry);
    const Task type = entry.task;

    // 后台任务只关心"结束"这件事；结果留在这个执行器里，稍后可从后台窗口查看
    disconnect(runner, nullptr, this, nullptr);
    connect(runner, &ProcessRunner::finished, this, [this, runner, type](int code) {
        finishBackgroundTask(runner, QStringLiteral("已完成"), code,
                             backgroundResultViewable(type));
    });
    connect(runner, &ProcessRunner::failed, this, [this, runner](const QString &reason) {
        finishBackgroundTask(runner, QStringLiteral("失败：%1").arg(reason));
    });

    // 新任务使用新的执行器，旧进程继续在后台跑
    m_runner = createRunner();

    // 旧任务的串联流程（如自动检测）到此为止
    ++m_flow;

    if (m_sfcTimer)
        m_sfcTimer->stop();
    setSfcRunning(false);
    m_restoreBusy = false;
    ui->restoreCreateBtn->setEnabled(true);
    ui->btnWUpdateFix->setEnabled(true);

    if (type == Task::FileVerify) {
        m_verifyResultFile.clear();
        setFakeCheckRunning(false);
        ui->fakeCheckSummary->setText(QStringLiteral("检查已转到后台，结果不会显示在这里"));
    }

    m_task = Task::None;

    appendLog(QStringLiteral("「%1」已转到后台继续运行。").arg(entry.label));
    updateBackgroundEntry();
    showBackgroundTasks();
}

void MainWindow::finishBackgroundTask(ProcessRunner *runner, const QString &result,
                                      int exitCode, bool hasResult)
{
    int index = -1;
    for (int i = 0; i < m_background.size(); ++i) {
        if (m_background.at(i).runner == runner) {
            index = i;
            break;
        }
    }

    QString label;
    if (index >= 0) {
        label = m_background.at(index).label;
        if (hasResult) {
            // 结果留在列表里等用户查看，执行器暂不销毁（输出还在里面）
            m_background[index].done = true;
            m_background[index].exitCode = exitCode;
            m_background[index].result = result;
        } else {
            m_background.removeAt(index);
            if (runner)
                runner->deleteLater();
        }
    }

    if (!label.isEmpty())
        appendLog(QStringLiteral("后台任务「%1」%2。").arg(label, result));

    updateBackgroundEntry();
    refreshBackgroundDialog();
}

void MainWindow::updateBackgroundEntry()
{
    int ready = 0;
    for (const BackgroundTask &task : m_background) {
        if (task.done)
            ++ready;
    }

    const int count = m_background.size();
    ui->bgTaskButton->setVisible(count > 0);

    QString text = QStringLiteral("后台任务 %1").arg(count);
    if (ready > 0)
        text += QStringLiteral(" · %1 个有结果").arg(ready);
    ui->bgTaskButton->setText(count > 0 ? text : QStringLiteral("后台任务"));
}

bool MainWindow::backgroundResultViewable(Task task) const
{
    // 只读任务的结果都渲染在卡片里，可以把输出重放回界面；
    // 写操作（改配置、卸载、删文件）不重放，避免重复触发副作用
    switch (task) {
    case Task::HealthCheck:
    case Task::StartupList:
    case Task::PortsList:
    case Task::LargeFiles:
    case Task::DuplicateScan:
    case Task::DiskHealth:
    case Task::HwInfo:
    case Task::Battery:
    case Task::ProcessList:
    case Task::DiskUsage:
    case Task::UninstallList:
    case Task::DriverList:
    case Task::HostsRead:
    case Task::DnsRead:
    case Task::ProxyRead:
    case Task::FileLock:
    case Task::FileVerify:
    case Task::BootTime:
    case Task::ContextMenuList:
    case Task::Treemap:
    case Task::RestoreList:
    case Task::SfcRepair:
    case Task::SxsCleanup:
    case Task::WUpdateRepair:
    case Task::NetInfo:
        return true;
    default:
        return false;
    }
}

int MainWindow::pageForTask(Task task) const
{
    switch (task) {
    case Task::PingAutoCheck:
    case Task::DnsCheck:
    case Task::FixDnsAuto:
    case Task::FixDnsManual:
    case Task::NetInfo:
    case Task::PortsList:
    case Task::HostsRead:
    case Task::DnsRead:
    case Task::ProxyRead:
    case Task::ProxyWrite:
        return 1;
    case Task::Cleaner:
    case Task::WinsockReset:
    case Task::FixWhite:
    case Task::HealthCheck:
    case Task::SfcRepair:
    case Task::WUpdateRepair:
    case Task::SxsCleanup:
    case Task::RestoreList:
    case Task::RestoreCreate:
        return 2;
    case Task::DiskHealth:
    case Task::HwInfo:
    case Task::Battery:
        return 3;
    case Task::StartupList:
    case Task::ProcessList:
    case Task::UninstallList:
    case Task::DriverList:
        return 4;
    case Task::LargeFiles:
    case Task::DuplicateScan:
    case Task::DiskUsage:
    case Task::FileLock:
    case Task::FileVerify:
    case Task::BootTime:
    case Task::ContextMenuList:
    case Task::Treemap:
        return 5;
    default:
        return -1;
    }
}

void MainWindow::viewBackgroundResult(ProcessRunner *runner)
{
    int index = -1;
    for (int i = 0; i < m_background.size(); ++i) {
        if (m_background.at(i).runner == runner) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    if (m_task != Task::None) {
        showMessage(QStringLiteral("还有任务正在运行"),
                    QStringLiteral("等「%1」结束，或把它转到后台后再查看这条结果。")
                        .arg(currentTaskLabel()));
        return;
    }

    const BackgroundTask entry = m_background.at(index);
    m_background.removeAt(index);

    // 结果卡片同一时刻只显示一个，后台任务跑完时卡片可能已被后来启动的
    // 任务顶掉；重放前先切回这张卡片，否则结果写进去也看不见
    switch (entry.task) {
    case Task::HealthCheck:      showSystemResultCard(ui->cardHealth); break;
    case Task::StartupList:      showSystemResultCard(ui->cardStartup); break;
    case Task::LargeFiles:       showSystemResultCard(ui->cardLargeFiles); break;
    case Task::DuplicateScan:    showSystemResultCard(ui->cardDup); break;
    case Task::DiskHealth:       showSystemResultCard(ui->cardDiskHealth); break;
    case Task::HwInfo:           showSystemResultCard(ui->cardHwinfo); break;
    case Task::Battery:          showSystemResultCard(ui->cardBattery); break;
    case Task::ProcessList:      showSystemResultCard(ui->cardProcesses); break;
    case Task::DiskUsage:        showSystemResultCard(ui->cardDiskUsage); break;
    case Task::UninstallList:    showSystemResultCard(ui->cardUninstall); break;
    case Task::DriverList:       showSystemResultCard(ui->cardDrivers); break;
    case Task::Treemap:          showSystemResultCard(ui->cardTreemap); break;
    default: break;
    }

    // 临时把后台执行器当作当前执行器，处理函数读的就是它的输出
    ProcessRunner *idleRunner = m_runner;
    m_runner = entry.runner;
    m_task = entry.task;
    dispatchTaskFinished(entry.exitCode);
    m_task = Task::None;
    m_runner = idleRunner;

    entry.runner->deleteLater();

    const int page = pageForTask(entry.task);
    if (page >= 0)
        showPage(page);

    if (m_bgDialog)
        m_bgDialog->hide();

    appendLog(QStringLiteral("已显示后台任务「%1」的结果。").arg(entry.label));
    updateBackgroundEntry();
    refreshBackgroundDialog();
}

void MainWindow::showBackgroundTasks()
{
    refreshBackgroundDialog();
    m_bgDialog->show();
    m_bgDialog->raise();
    m_bgDialog->activateWindow();
}

void MainWindow::refreshBackgroundDialog()
{
    if (!m_bgDialog) {
        m_bgDialog = new QDialog(this);
        m_bgDialog->setObjectName(QStringLiteral("bgDialog"));
        m_bgDialog->setWindowTitle(QStringLiteral("后台任务"));
        m_bgDialog->setMinimumWidth(440);

        QVBoxLayout *outer = new QVBoxLayout(m_bgDialog);
        outer->setContentsMargins(18, 16, 18, 16);
        outer->setSpacing(10);

        QLabel *hint = new QLabel(
            QStringLiteral("这些任务在后台运行，可以随时取消。"
                           "已经跑完的任务可以查看结果。关闭这个窗口不会影响它们。"),
            m_bgDialog);
        hint->setObjectName(QStringLiteral("bgHint"));
        hint->setWordWrap(true);
        outer->addWidget(hint);

        m_bgRowsLayout = new QVBoxLayout;
        m_bgRowsLayout->setSpacing(8);
        m_bgRowsLayout->setAlignment(Qt::AlignTop);
        outer->addLayout(m_bgRowsLayout, 1);

        QHBoxLayout *buttons = new QHBoxLayout;
        buttons->addStretch();
        QPushButton *closeButton = new QPushButton(QStringLiteral("关闭"), m_bgDialog);
        closeButton->setObjectName(QStringLiteral("bgCloseBtn"));
        closeButton->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(closeButton);
        outer->addLayout(buttons);

        connect(closeButton, &QPushButton::clicked, m_bgDialog, &QDialog::hide);
    }

    while (QLayoutItem *item = m_bgRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (m_background.isEmpty()) {
        QLabel *empty = new QLabel(QStringLiteral("目前没有后台任务。"), m_bgDialog);
        empty->setObjectName(QStringLiteral("bgEmpty"));
        m_bgRowsLayout->addWidget(empty);
        return;
    }

    for (const BackgroundTask &task : m_background) {
        QWidget *row = new QWidget(m_bgDialog);
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);

        QLabel *name = new QLabel(task.label, row);
        name->setObjectName(QStringLiteral("bgTaskName"));

        QLabel *status = new QLabel(task.done ? task.result
                                              : QStringLiteral("正在运行"), row);
        status->setObjectName(QStringLiteral("bgTaskStatus"));

        layout->addWidget(name, 1);
        layout->addWidget(status);

        ProcessRunner *runner = task.runner;
        if (task.done) {
            QPushButton *viewButton = new QPushButton(QStringLiteral("查看结果"), row);
            viewButton->setObjectName(QStringLiteral("bgViewBtn"));
            viewButton->setCursor(Qt::PointingHandCursor);
            connect(viewButton, &QPushButton::clicked, this, [this, runner]() {
                viewBackgroundResult(runner);
            });
            layout->addWidget(viewButton);
        } else {
            QPushButton *cancelButton = new QPushButton(QStringLiteral("取消"), row);
            cancelButton->setObjectName(QStringLiteral("bgCancelBtn"));
            cancelButton->setCursor(Qt::PointingHandCursor);
            connect(cancelButton, &QPushButton::clicked, this, [this, runner]() {
                runner->stop();
                finishBackgroundTask(runner, QStringLiteral("已取消"));
            });
            layout->addWidget(cancelButton);
        }

        m_bgRowsLayout->addWidget(row);
    }
}


