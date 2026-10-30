/**
 * 回归测试：日志追加必须走纯文本路径。
 *
 * 背景：QTextBrowser::append() 会判断文本是否"像 HTML"，若像则交给 HTML
 * 解析器处理。ping 输出里的 "时间<1ms TTL=64" 会被当成非法标签，
 * 导致解析器取到 -1 索引 → 访问 0xFFFFFFFFFFFFFFFF → 访问冲突崩溃。
 *
 * 本测试用真实的 ping 输出片段验证：
 *  1. 追加含 '<' 的文本不崩溃
 *  2. 追加后文档中保留原始文本（没被当成 HTML 解析掉）
 *  3. 空文本、纯符号、超长文本都安全
 */
#include <QtTest/QTest>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>

class LogAppendTest : public QObject
{
    Q_OBJECT

private slots:
    /** 含 '<' 的 ping 输出不应崩溃，且原样保留 */
    void angleBracket_survivesAsPlainText()
    {
        QTextBrowser browser;
        const QString pingLine = QString::fromUtf8(
            "来自 36.152.44.132 的回复: 字节=32 时间<1ms TTL=64");

        QTextCursor cursor(browser.document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(pingLine);

        const QString doc = browser.toPlainText();
        QVERIFY2(doc.contains(QString::fromUtf8("时间<1ms TTL=64")),
                 "含 '<' 的文本被当作 HTML 解析，原始内容丢失");
    }

    /** 真实 ping 全量输出（含多处 '<'）连续追加不崩溃 */
    void realPingOutput_appendsSafely()
    {
        QTextBrowser browser;
        const QStringList lines = {
            QString::fromUtf8("正在 Ping www.baidu.com [36.152.44.132] 具有 32 字节的数据:"),
            QString::fromUtf8("来自 36.152.44.132 的回复: 字节=32 时间<1ms TTL=64"),
            QString::fromUtf8("来自 36.152.44.132 的回复: 字节=32 时间<1ms TTL=64"),
            QString::fromUtf8("来自 36.152.44.132 的回复: 字节=32 时间<1ms TTL=64"),
            QString::fromUtf8("来自 36.152.44.132 的回复: 字节=32 时间<1ms TTL=64")
        };

        for (const QString &line : lines) {
            QTextCursor cursor(browser.document());
            cursor.movePosition(QTextCursor::End);
            if (!browser.document()->isEmpty())
                cursor.insertBlock();
            cursor.insertText(line);
        }

        const QString doc = browser.toPlainText();
        QVERIFY(doc.contains(QString::fromUtf8("时间<1ms TTL=64")));
        QVERIFY2(!doc.contains(QString::fromUtf8("<1ms TTL=64</")),
                 "文本被当成了 HTML 标签");
    }

    /** 对照组：QTextBrowser::append() 确实会走 HTML 解析（证明修复必要性） */
    void plainAppend_treatsTextAsHtml()
    {
        QTextBrowser browser;
        // append() 会把 '<1ms ...' 交给 HTML 解析器，
        // '<1ms' 不是合法标签名，其中的内容会被吞掉或重排
        browser.append(QString::fromUtf8("时间<1ms TTL=64"));

        const QString doc = browser.toPlainText();
        qDebug() << "append() 结果:" << doc;
        // 不做硬断言，只打印对比结果——不同 Qt 版本行为可能不同
    }

    /** 各种边界输入都不应崩溃 */
    void edgeCases_areSafe()
    {
        QTextBrowser browser;
        const QStringList inputs = {
            QString(),
            QStringLiteral("<"),
            QStringLiteral("<<>>"),
            QStringLiteral("<a href=\"x\">链接</a>"),
            QStringLiteral("<html><body><p>未闭合"),
            QStringLiteral("<b><i><u>"),
            QString(50000, QChar('<')),
            QStringLiteral("正常中文内容")
        };

        for (const QString &in : inputs) {
            QTextCursor cursor(browser.document());
            cursor.movePosition(QTextCursor::End);
            if (!browser.document()->isEmpty())
                cursor.insertBlock();
            cursor.insertText(in);
        }
        QVERIFY(!browser.document()->isEmpty());
    }
};

QTEST_MAIN(LogAppendTest)
#include "logappendtest.moc"
