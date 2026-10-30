#include <QtTest/QTest>
#include "netcheck.h"
#include "processrunner.h"

/**
 * 网络检测判断逻辑的单元测试。
 *
 * 重点覆盖原实现的误判：
 * nslookup 在 DNS 正常时也会输出 "服务器:  UnKnown"，
 * 若把它当作失败特征，正常的网络会被误判为故障并触发无谓修复。
 */
class NetCheckTest : public QObject
{
    Q_OBJECT

private slots:
    void dnsNormalWithUnknownServer_notFailure();
    void dnsNonExistentDomain_isFailure();
    void dnsNoResponse_isFailure();
    void dnsTimedOut_isFailure();
    void dnsServFail_isFailure();
    void pingTimeout_detected();
    void pingHostUnknown_detected();
    void pingNormal_notTimeoutNorUnknown();
    void cmdArguments_includeSlashC();
    void cmdArguments_alwaysHasSlashC();
};

void NetCheckTest::dnsNormalWithUnknownServer_notFailure()
{
    // 实测本机 nslookup baidu.com 的真实输出（DNS 完全正常）
    const QString output = QString::fromUtf8(
        "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8:  UnKnown\n"   // 服务器:  UnKnown
        "Address:  1.1.1.1\n\n"
        "\xE5\x90\x8D\xE7\xA7\xB0:    baidu.com\n"              // 名称:    baidu.com
        "Addresses:  111.63.65.103\n");
    QVERIFY(!NetCheck::isDnsLookupFailed(output));
}

void NetCheckTest::dnsNonExistentDomain_isFailure()
{
    QVERIFY(NetCheck::isDnsLookupFailed(
        "Server:  UnKnown\nAddress:  1.1.1.1\n\n"
        "*** nosuchdomain-xyz123.com can't find: Non-existent domain\n"));
}

void NetCheckTest::dnsNoResponse_isFailure()
{
    QVERIFY(NetCheck::isDnsLookupFailed(
        "Server:  UnKnown\nAddress:  1.1.1.1\n\n"
        "*** baidu.com: No response from server\n"));
}

void NetCheckTest::dnsTimedOut_isFailure()
{
    QVERIFY(NetCheck::isDnsLookupFailed(
        "Server:  UnKnown\nAddress:  1.1.1.1\n\n"
        "*** baidu.com: timed out\n"));
}

void NetCheckTest::dnsServFail_isFailure()
{
    QVERIFY(NetCheck::isDnsLookupFailed(
        "Server:  UnKnown\nAddress:  1.1.1.1\n\n"
        "*** baidu.com: SERVFAIL\n"));
}

void NetCheckTest::pingTimeout_detected()
{
    QVERIFY(NetCheck::isPingTimeout(
        QString::fromUtf8("\xE8\xAF\xB7\xE6\xB1\x82\xE8\xB6\x85\xE6\x97\xB6\xE3\x80\x82")));
    QVERIFY(NetCheck::isPingTimeout("Request timed out."));
}

void NetCheckTest::pingHostUnknown_detected()
{
    QVERIFY(NetCheck::isPingHostUnknown(
        QString::fromUtf8("\xE6\x89\xBE\xE4\xB8\x8D\xE5\x88\xB0\xE4\xB8\xBB\xE6\x9C\xBA")));
    QVERIFY(NetCheck::isPingHostUnknown(
        "Ping request could not find host www.baidu.com."));
}

void NetCheckTest::pingNormal_notTimeoutNorUnknown()
{
    const QString output = QString::fromUtf8(
        "\xE6\xAD\xA3\xE5\x9C\xA8 Ping www.baidu.com [110.242.74.102] \xE5\x85\xB7\xE6\x9C\x89 32 \xE5\xAD\x97\xE8\x8A\x82\xE7\x9A\x84\xE6\x95\xB0\xE6\x8D\xAE\xE3\x80\x82\n");
    QVERIFY(!NetCheck::isPingTimeout(output));
    QVERIFY(!NetCheck::isPingHostUnknown(output));
}

/**
 * cmd.exe 必须带 /c。
 *
 * 少了 /c，cmd.exe 会进入交互式 shell 模式：只打印版本横幅和提示符，
 * 然后等待 stdin 输入——被请求的命令根本不会执行，进程还会以异常状态退出。
 * 这个 bug 在界面上表现为"控制台只有第一句话"（其实是 cmd 的横幅）。
 */
void NetCheckTest::cmdArguments_includeSlashC()
{
    const QStringList args = ProcessRunner::cmdArguments("ping www.baidu.com");

    QCOMPARE(args.size(), 2);
    QCOMPARE(args.at(0), QStringLiteral("/c"));
    QCOMPARE(args.at(1), QStringLiteral("ping www.baidu.com"));
}

/** 传入空命令行时也应保持 /c 结构合法 */
void NetCheckTest::cmdArguments_alwaysHasSlashC()
{
    const QStringList args = ProcessRunner::cmdArguments(QString());

    QVERIFY(!args.isEmpty());
    QCOMPARE(args.at(0), QStringLiteral("/c"));
}

QTEST_MAIN(NetCheckTest)
#include "netchecktest.moc"
