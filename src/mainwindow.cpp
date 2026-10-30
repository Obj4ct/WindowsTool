#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processrunner.h"
#include "netcheck.h"
#include "uiicons.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
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
            this, [this](const QString &text) { appendLog(text); });
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

    // 磁贴图标尺寸（QToolButton 不会自动跟随 QSS 的 icon-size）
    for (QToolButton *b : {ui->btn_autoCheck, ui->btn_showNetWork,
                            ui->btn_netWorkTest, ui->btn_cleaner,
                            ui->btn_fixWhite}) {
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
                           "此操作将重置全部活动网卡的 DNS 配置，并清除本机 DNS 解析缓存。"
                           "执行期间网络可能短暂不可用。\n\n是否继续？"))
            return;

        clearLog();
        appendLog("正在刷新网络配置，请稍候……");
        runFixDns();
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

        clearLog();
        appendLog("正在重置 Winsock，请稍候……");
        m_runner->runBat("winsockReset.bat");
    });

    // 网络测试
    connect(ui->btn_netWorkTest, &QAbstractButton::clicked, this, [this]() {
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
    clearLog();
    appendLog("请等待，正在检测网络，电脑可能会出现短暂卡顿，此时请勿进行任何操作……");

    // 启动 ping 检测；结果在 handleAutoCheckPing() 中处理
    m_task = Task::PingAutoCheck;
    m_runner->runCommand(QStringLiteral("ping www.baidu.com"));
}

void MainWindow::handleAutoCheckPing()
{
    const QString output = m_runner->collectedOutput();

    if (isPingHostUnknown(output)) {
        appendLog("-------------------");
        appendLog("ping 错误：找不到主机，正在尝试修复网络配置……");

        // 稍作停顿让用户看清提示，再进入 DNS 检查
        QTimer::singleShot(600, this, [this]() {
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

    QTimer::singleShot(600, this, [this]() {
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

    if (isDnsLookupFailed(output)) {
        appendLog("-------------------");
        appendLog("网络配置出现问题了（无法找到 DNS 服务器），正在尝试修复……");

        QTimer::singleShot(600, this, &MainWindow::runFixDns);
        return;
    }

    appendLog("-------------------");
    appendLog("网络连接正常，自动检测完成");
}

/**
 * 执行 DNS 修复脚本。
 * fixDNS.bat 会自行申请管理员权限并重新拉起进程，因此本进程会很快返回，
 * 这里在脚本结束后等待一段时间，再询问用户网络是否恢复。
 */
void MainWindow::runFixDns()
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

    m_runner->runBat("fixDNS.bat");

    // 等待修复脚本（会弹 UAC 并重新拉起）执行完毕
    QTimer::singleShot(5000, this, &MainWindow::askNetworkRecovered);
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

        clearLog();
        appendLog("-------------------");
        appendLog("即将清理系统临时文件。此操作不会影响个人文档。");
        appendLog("-------------------");
        appendLog("系统将弹出权限请求窗口，请选择「是」以继续。"
                  "清理时长取决于临时文件数量，请耐心等待。");

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

        clearLog();
        appendLog("正在重建桌面图标缓存，期间屏幕会短暂黑屏，请稍候……");
        m_runner->runBat("fixWhite.bat");
    });
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
