#ifndef PROCESSRUNNER_H
#define PROCESSRUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>

class QTextDecoder;

/**
 * @brief 异步执行外部命令 / bat 脚本。
 *
 * 设计要点：
 *  - 事件驱动：进程输出通过信号实时回传，主线程不阻塞，
 *    脚本执行期间界面保持可交互（原实现用 waitForFinished 轮询 + repaint，
 *    配合 QTest::qWait 最长会让界面卡住 20 秒以上）；
 *  - 输出按本地代码页解码（中文 Windows 为 GBK），兼容中英文命令输出；
 *  - bat 脚本统一从 exe 同级的 bat/ 目录查找，与程序打包方式保持一致。
 */
class ProcessRunner : public QObject
{
    Q_OBJECT

public:
    explicit ProcessRunner(QObject *parent = nullptr);
    ~ProcessRunner() override;

    /** 系统 cmd.exe 的完整路径 */
    static QString cmd();

    /**
     * 拼装"通过 cmd.exe 执行一条命令行"的参数列表。
     *
     * 抽成公开的纯函数是为了能写单元测试——`/c` 少一个就会让
     * cmd.exe 进入交互式 shell 模式（只打印横幅、不执行命令），
     * 这种错误在运行时才暴露，代价很大。
     */
    static QStringList cmdArguments(const QString &commandLine);

    /** 执行 bat/ 目录下的脚本，例如 "dnsCheck.bat" */
    void runBat(const QString &fileName);

    /**
     * 执行 bat/ 目录下的脚本并传入参数。
     *
     * 参数按 QProcess 的规则逐个转义，脚本里用 %1、%2 接收；
     * 不需要调用方自行拼加引号。
     */
    void runBat(const QString &fileName, const QStringList &arguments);

    /**
     * 通过 cmd.exe 执行一条命令行，例如 "ping www.baidu.com"。
     *
     * 内部自动补上 /c 参数。**不要直接用 run(cmd(), {...}) 去跑命令行**：
     * cmd.exe 缺少 /c 会进入交互式 shell 模式——只打印版本横幅和提示符，
     * 然后等待 stdin 输入，既不会执行命令也会以异常状态退出。
     */
    void runCommand(const QString &commandLine);

    /** 执行任意程序 */
    void run(const QString &program, const QStringList &arguments);

    /** 本次运行累计的全部输出（已解码） */
    QString collectedOutput() const { return m_output; }

    /** 进程是否仍在运行 */
    bool isRunning() const;

    /** 终止当前进程 */
    void stop();

signals:
    /** 命令已经开始执行（用于展示"正在处理"提示） */
    void started();
    /** 收到一段新输出 */
    void outputReceived(const QString &text);
    /** 进程正常结束，exitCode 为退出码 */
    void finished(int exitCode);
    /** 启动失败（例如脚本文件不存在） */
    void failed(const QString &reason);

private:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

    QProcess *m_process = nullptr;
    QString m_output;
    std::unique_ptr<QTextDecoder> m_decoder;

    /** 是否为主动中止（stop() 杀掉上一个进程），此时不视为异常 */
    bool m_aborted = false;
};

#endif // PROCESSRUNNER_H
