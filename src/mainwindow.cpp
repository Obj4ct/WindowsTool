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
    setupOtherTools();

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
    ui->navToolsBtn->setIcon(navIcon(UiIcons::Icon::Grid));
    for (QPushButton *b : {ui->navNetworkBtn, ui->navSystemBtn, ui->navToolsBtn}) {
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
    ui->btn_prt->setIcon(tileIcon(UiIcons::Icon::Camera));

    // 磁贴图标尺寸（QToolButton 不会自动跟随 QSS 的 icon-size）
    for (QToolButton *b : {ui->btn_autoCheck, ui->btn_showNetWork,
                            ui->btn_netWorkTest, ui->btn_cleaner,
                            ui->btn_fixWhite, ui->btn_prt}) {
        b->setIconSize(QSize(30, 30));
    }

    // 次级按钮图标
    ui->btn_refreshDNS->setIcon(UiIcons::make(UiIcons::Icon::Refresh, QColor("#6B7A93"), 16));
    ui->btn_resetWinSock->setIcon(UiIcons::make(UiIcons::Icon::Reset, QColor("#6B7A93"), 16));
    ui->btn_key->setIcon(UiIcons::make(UiIcons::Icon::Keyboard, QColor("#6B7A93"), 16));

    // 控制台
    ui->textBrowser->setOpenExternalLinks(true);
    ui->shortcutView->setOpenExternalLinks(true);

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
    connect(ui->navToolsBtn, &QAbstractButton::clicked, this, [switchTo]() { switchTo(2); });

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
        if (!confirmAction("提示",
                           "这个操作可以修复网络配置，确保网络配置正常工作。点击确定开始执行。"))
            return;

        clearLog();
        appendLog("请等待，正在刷新网络配置……");
        runFixDns();
    });

    // 刷新 winsock
    connect(ui->btn_resetWinSock, &QPushButton::clicked, this, [this]() {
        if (!confirmAction("警告！",
                           "这个命令可以重新初始化网络环境，以解决由于软件冲突、病毒原因造成的参数错误问题。"
                           "但是会导致部分电脑网速变慢，具体情况取决于电脑网络，除非别无选择，否则请谨慎操作！"
                           "操作完成之后请重启电脑，然后在本软件选择\"刷新网络配置\"，"
                           "使用之后如果出现网速变慢等问题，请联系开发人员"))
            return;

        if (!batExists("winsockReset.bat")) {
            showMessage("错误", "找不到脚本文件 winsockReset.bat", QMessageBox::Critical);
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
    showMessage("执行失败", reason, QMessageBox::Critical);
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
        showMessage("连接错误",
                    "网络连接出现问题，请检查是否连接网络（建议多检测几次）。"
                    "如果已经连接，建议使用自动检测来检查问题",
                    QMessageBox::Critical);
        return;
    }

    if (isPingHostUnknown(output)) {
        showMessage("连接错误",
                    "无法解析主机名，可能是 DNS 配置有问题，建议使用\"刷新网络配置\"修复",
                    QMessageBox::Critical);
        return;
    }

    showMessage("成功", "你可以正常上网！");
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
        showMessage("错误", "找不到脚本文件 fixDNS.bat", QMessageBox::Critical);
        return;
    }

    appendLog("-------------------");
    appendLog("修复网络配置中……");
    appendLog("-------------------");

    m_runner->runBat("fixDNS.bat");

    // 等待修复脚本（会弹 UAC 并重新拉起）执行完毕
    QTimer::singleShot(5000, this, &MainWindow::askNetworkRecovered);
}

void MainWindow::askNetworkRecovered()
{
    appendLog("-------------------");
    appendLog("网络配置修复完成，正在检测是否可以上网……");

    QMessageBox box(QMessageBox::Question, "疑问", "是否可以上网？",
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setWindowIcon(QIcon(":/ico/question.png"));
    box.setButtonText(QMessageBox::Yes, "可以");
    box.setButtonText(QMessageBox::No, "不可以");

    if (box.exec() == QMessageBox::Yes) {
        showMessage("开发者的话", "好的，这对你来说是一个好软件");
        appendLog("成功啦！感谢使用本软件。");
    } else {
        showMessage("开发者的话",
                    "软件为第一个版本，难免会有问题，请点击右上角\"关于\"来联系作者");
        appendLog("失败了！请联系开发者……555（哭了）");
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
            showMessage("错误", "找不到脚本文件 cleaner.bat", QMessageBox::Critical);
            return;
        }

        clearLog();
        appendLog("-------------------");
        appendLog("此操作会清理系统临时文件，不会删除您的个人文档。");
        appendLog("-------------------");
        appendLog("之后会弹出一个窗口，请点击\"是\"，清理时长取决于垃圾数量，"
                  "完成后会显示\"清理系统垃圾完成！\"，请耐心等待……");

        m_runner->runBat("cleaner.bat");
    });

    // 修复桌面图标变白
    connect(ui->btn_fixWhite, &QAbstractButton::clicked, this, [this]() {
        if (!batExists("fixWhite.bat")) {
            showMessage("错误", "找不到脚本文件 fixWhite.bat", QMessageBox::Critical);
            return;
        }

        clearLog();
        appendLog("正在重建桌面图标缓存，屏幕会短暂黑屏，请稍候……");
        m_runner->runBat("fixWhite.bat");
    });

    // 截图工具（仅提示快捷键，不执行任何命令）
    connect(ui->btn_prt, &QAbstractButton::clicked, this, [this]() {
        QMessageBox box(QMessageBox::Question, "别开玩笑",
                        "别傻傻地登录 QQ/微信进行截图啦，你不觉得太麻烦了？"
                        "按下 win(Alt 左边那个和开始菜单一样的图标)+shift+s "
                        "就可以快速截图了(只支持 win10 和 win10 以上系统)，"
                        "然后直接 ctrl+v 粘贴就好",
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setWindowIcon(QIcon(":/ico/what.png"));
        box.setButtonText(QMessageBox::Yes, "知道了");
        box.setButtonText(QMessageBox::No, "你管得着吗？");

        if (box.exec() == QMessageBox::Yes) {
            showMessage("开发者的话", "真是个提高效率的好方法");
            clearLog();
            appendLog("按下 win(Alt 左边那个和开始菜单一样的图标)+shift+s 就可以快速截图了"
                      "(只支持 win10 和 win10 以上系统)，然后直接 ctrl+v 粘贴就好");
        } else {
            showMessage("开发者的话", "555，你开心就好了");
        }
    });
}

// ---------------------------------------------------------------- 其他工具

void MainWindow::setupOtherTools()
{
    // 快捷键速查表：以 HTML 渲染在专用视图里，比往控制台灌文本更易读
    struct Shortcut {
        const char *keys;
        const char *desc;
    };
    static const Shortcut shortcuts[] = {
        {"Ctrl + C / V", "复制 / 粘贴"},
        {"Win + Shift + S", "快速截图（Win10 及以上），随后 Ctrl+V 粘贴"},
        {"Win + Ctrl + Shift + B", "刷新显卡驱动，解决画面卡顿但声音正常的问题"},
        {"Win + V", "打开剪贴板历史记录，直接粘贴上一次的复制内容"},
        {"Win + D", "显示桌面"},
        {"Win + G", "打开游戏模式"},
        {"Win + L", "锁屏"},
        {"Win + E", "打开文件资源管理器"},
        {"Ctrl + F", "搜索当前页面内容"},
        {"Ctrl + S", "及时保存，避免意外造成损失"}
    };

    QString html = QStringLiteral(
        "<table cellspacing='0' cellpadding='7'>");
    for (const Shortcut &s : shortcuts) {
        html += QStringLiteral(
                    "<tr>"
                    "<td width='210'><b style='color:#1A5FD0;'>%1</b></td>"
                    "<td style='color:#4A576F;'>%2</td>"
                    "</tr>")
                    .arg(QString::fromUtf8(s.keys), QString::fromUtf8(s.desc));
    }
    html += QStringLiteral("</table>");
    ui->shortcutView->setHtml(html);

    // 在控制台中查看（保留原行为）
    connect(ui->btn_key, &QPushButton::clicked, this, &MainWindow::showShortcutList);
}

void MainWindow::showShortcutList()
{
    clearLog();
    const QStringList shortcuts = {
        "ctrl+C/V：这么简单你应该不会不知道",
        "win(Alt 左边那个和开始菜单一样的图标)+shift+s：非常强大的快速截图工具，"
        "支持多方式截图，只支持 win10 和 win10 以上系统",
        "win+ctrl+shift+B：刷新显卡驱动，如果你玩游戏遇到游戏声音还在但是画面卡了，"
        "就可以用这个刷新显卡驱动，只支持 win10 和 win10 以上系统",
        "win+V：打开剪贴板历史记录，选中内容之后直接 ctrl+v，只支持 win10 和 win10 以上系统",
        "win+D：显示桌面",
        "win+G：打开游戏模式，只支持 win10 和 win10 以上系统",
        "win+L：锁屏",
        "ctrl+f：搜索当前页面内容",
        "ctrl+s：虽然都知道是\"保存\"，但是希望在工作的时候经常使用，"
        "为了避免未知的情况，造成损失"
    };
    for (const QString &line : shortcuts) {
        appendLog(line);
        appendLog("                   ");
    }
    appendLog("-------------------");
    appendLog("更多内容请点击下方链接");

    // 链接需要真正的 HTML 渲染，单独走 insertHtml（内容是程序内写死的常量，安全）
    QTextCursor cursor(ui->textBrowser->document());
    cursor.movePosition(QTextCursor::End);
    if (!ui->textBrowser->document()->isEmpty()) {
        cursor.insertBlock();
    }
    cursor.insertHtml(QStringLiteral(
        "<a href=\"https://support.microsoft.com/zh-cn/windows/windows-%E7%9A%84%E9%94%AE%E7%9B%98%E5%BF%AB%E6%8D%B7%E6%96%B9%E5%BC%8F-dcc61a57-8ff0-cffe-9796-cb9706c75eec\" "
        "style=\"color:#6FA8FF;\">快捷键大全</a>"));
    ui->textBrowser->setTextCursor(cursor);
    ui->textBrowser->ensureCursorVisible();
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
