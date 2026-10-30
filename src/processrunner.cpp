#include "processrunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextCodec>

namespace {

/**
 * 命令行输出使用系统本地代码页（中文 Windows 为 GBK / 936）。
 *
 * 用 QTextDecoder 保留跨块解码状态：多字节字符如果恰好被
 * readAllStandardOutput() 切在两个 chunk 之间，逐块独立解码就会产生乱码。
 * 原实现用 QString::fromLocal8Bit 对每块独立解码，正是"输出内容明明
 * 与 if 条件一致、却走进 else 分支"的根本原因。
 */
QTextCodec *localCodec()
{
    QTextCodec *codec = QTextCodec::codecForLocale();
    return codec ? codec : QTextCodec::codecForName("GBK");
}

/** 取系统的 cmd.exe，避免写死 C: 盘路径 */
QString cmdExecutable()
{
    const QString comspec = qEnvironmentVariable("ComSpec");
    if (!comspec.isEmpty() && QFileInfo::exists(comspec))
        return QDir::toNativeSeparators(comspec);
    return QStringLiteral("C:/Windows/System32/cmd.exe");
}

} // namespace

QString ProcessRunner::cmd()
{
    return cmdExecutable();
}

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    // 合并 stderr，部分命令（如 nslookup）会把提示信息写到 stderr
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ProcessRunner::onReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProcessRunner::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProcessRunner::onError);
}

ProcessRunner::~ProcessRunner()
{
    // 析构期间绝不能再向外发信号：接收方（MainWindow）可能已经销毁了控件。
    // 先断开所有对外连接，再停进程。
    disconnect(this, nullptr, nullptr, nullptr);
    stop();
}

bool ProcessRunner::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void ProcessRunner::stop()
{
    if (!isRunning())
        return;

    // kill() 会让 Qt 把退出状态报成 CrashExit，但这是我们主动中止的，
    // 不是脚本出错——标记一下，onFinished 里就不弹错误提示了。
    //
    // 注意：waitForFinished 可能因进程不响应而超时返回 false，
    // 此时 finished 信号会在之后异步到达，所以 m_aborted 不能立刻复位，
    // 要等到下一次 run() 时才重置。
    m_aborted = true;
    m_process->kill();
    if (!m_process->waitForFinished(1000)) {
        // 进程没在超时内退出，继续等它真正结束（不阻塞 UI）
        return;
    }
}

QStringList ProcessRunner::cmdArguments(const QString &commandLine)
{
    // /c 必不可少：缺了它 cmd.exe 会进入交互式 shell 模式，
    // 只打印版本横幅和提示符然后等 stdin，既不执行命令也会异常退出。
    return { QStringLiteral("/c"), commandLine };
}

void ProcessRunner::runBat(const QString &fileName)
{
    const QString script = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("bat/") + fileName);

    if (!QFileInfo::exists(script)) {
        emit failed(QStringLiteral("找不到脚本文件：%1").arg(fileName));
        return;
    }

    m_output.clear();
    run(cmdExecutable(), { QStringLiteral("/c"), QDir::toNativeSeparators(script) });
}

void ProcessRunner::runCommand(const QString &commandLine)
{
    if (commandLine.trimmed().isEmpty())
        return;

    m_output.clear();
    run(cmdExecutable(), cmdArguments(commandLine));
}

void ProcessRunner::run(const QString &program, const QStringList &arguments)
{
    // 上一次任务尚未结束时，先收尾，避免输出互相串台
    if (isRunning())
        stop();

    m_output.clear();
    m_decoder.reset();   // 每次运行重置解码状态，避免上一次残留影响本次
    m_aborted = false;   // 新任务开始，上一次的"主动中止"标记失效
    m_process->start(program, arguments);

    // 不阻塞等待：waitForStarted 会卡住主线程。
    // 启动失败由 errorOccurred(FailedToStart) → onError 统一上报。
}

void ProcessRunner::onReadyRead()
{
    const QByteArray data = m_process->readAllStandardOutput();
    if (data.isEmpty())
        return;

    if (!m_decoder)
        m_decoder.reset(localCodec()->makeDecoder());

    // 保留解码器状态，正确处理跨 chunk 的多字节字符
    const QString text = m_decoder->toUnicode(data);
    if (text.isEmpty())
        return;

    m_output += text;
    emit outputReceived(text);
}

void ProcessRunner::onFinished(int exitCode, QProcess::ExitStatus status)
{
    // 主动中止：不是错误。
    // 但仍要把残留输出排空丢弃，否则会串到下一次任务里。
    if (m_aborted) {
        m_process->readAllStandardOutput();
        return;
    }

    // 进程结束时可能还有残留输出，先排空再收尾
    const QByteArray tail = m_process->readAllStandardOutput();
    if (!tail.isEmpty()) {
        if (!m_decoder)
            m_decoder.reset(localCodec()->makeDecoder());
        const QString text = m_decoder->toUnicode(tail);
        m_output += text;
        emit outputReceived(text);
    }

    if (status == QProcess::CrashExit) {
        emit failed(QStringLiteral("进程异常终止"));
        return;
    }

    emit finished(exitCode);
}

void ProcessRunner::onError(QProcess::ProcessError error)
{
    // 主动中止导致的 Crashed 不是错误（kill() 必然触发它）
    if (m_aborted)
        return;

    switch (error) {
    case QProcess::FailedToStart:
        emit failed(QStringLiteral("进程启动失败：找不到要执行的命令或脚本"));
        break;
    case QProcess::Crashed:
        emit failed(QStringLiteral("进程异常终止"));
        break;
    case QProcess::Timedout:
        emit failed(QStringLiteral("进程执行超时"));
        break;
    case QProcess::WriteError:
    case QProcess::ReadError:
        emit failed(QStringLiteral("进程读写失败"));
        break;
    default:
        break;
    }
}
