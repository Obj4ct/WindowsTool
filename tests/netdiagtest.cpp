/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <QtTest/QTest>

#include "dnsprobe.h"
#include "netdiag.h"

namespace {

/** 组一个最小的 A 记录响应：1 个问题 + 0/1 个答案 */
QByteArray makeResponse(quint16 id, int rcode, const QString &address)
{
    QByteArray packet;
    auto appendU16 = [&packet](quint16 value) {
        packet.append(static_cast<char>(value >> 8));
        packet.append(static_cast<char>(value & 0xFF));
    };

    appendU16(id);
    appendU16(static_cast<quint16>(0x8180 | (rcode & 0x0F)));
    appendU16(1);                                  // QDCOUNT
    appendU16(address.isEmpty() ? 0 : 1);          // ANCOUNT
    appendU16(0);                                  // NSCOUNT
    appendU16(0);                                  // ARCOUNT

    for (const QString &label : { QStringLiteral("example"), QStringLiteral("com") }) {
        packet.append(static_cast<char>(label.size()));
        packet.append(label.toLatin1());
    }
    packet.append(static_cast<char>(0x00));
    appendU16(1);                                  // QTYPE=A
    appendU16(1);                                  // QCLASS=IN

    if (!address.isEmpty()) {
        packet.append(static_cast<char>(0xC0));    // 压缩指针 → 0x000C
        packet.append(static_cast<char>(0x0C));
        appendU16(1);                              // TYPE=A
        appendU16(1);                              // CLASS=IN
        packet.append(QByteArray(4, '\0'));        // TTL
        appendU16(4);                              // RDLENGTH
        const QStringList parts = address.split(QLatin1Char('.'));
        for (const QString &part : parts)
            packet.append(static_cast<char>(part.toInt()));
    }
    return packet;
}

QByteArray sampleSnapshot()
{
    return QByteArrayLiteral(R"({
        "adapters": [
            {"name":"WLAN","description":"Intel(R) Wi-Fi 6 AX201","status":"Up",
             "ifIndex":12,"ipv4":"192.168.1.5","gateway":"192.168.1.1",
             "dns":["223.5.5.5","8.8.8.8"],"virtual":false,"defaultRoute":true},
            {"name":"VMware Network Adapter VMnet8","description":"VMware Virtual Ethernet Adapter",
             "status":"Up","ifIndex":30,"ipv4":"192.168.56.1","gateway":"",
             "dns":[],"virtual":true,"defaultRoute":false}
        ],
        "defaultAdapter": "WLAN",
        "proxy": {"enabled": false, "server": "", "pac": ""}
    })");
}

QVector<DnsProbe::Result> dnsResults(bool ok)
{
    QVector<DnsProbe::Result> results;
    DnsProbe::Result first;
    first.server = QStringLiteral("223.5.5.5");
    first.elapsedMs = ok ? 23 : 2000;
    first.status = ok ? DnsProbe::Status::Ok : DnsProbe::Status::NoResponse;
    if (ok)
        first.addresses.append(QStringLiteral("1.2.3.4"));
    results.append(first);

    DnsProbe::Result second;
    second.server = QStringLiteral("8.8.8.8");
    second.elapsedMs = ok ? 95 : 2000;
    second.status = ok ? DnsProbe::Status::Ok : DnsProbe::Status::NoResponse;
    if (ok)
        second.addresses.append(QStringLiteral("5.6.7.8"));
    results.append(second);
    return results;
}

} // namespace

class NetDiagTest : public QObject
{
    Q_OBJECT

private slots:
    void buildQuery_hasHeaderAndQuestion();
    void parseResponse_aRecord();
    void parseResponse_highOctets();
    void parseResponse_nxdomain();
    void parseResponse_servfail();
    void parseResponse_idMismatch();
    void parseResponse_truncated();
    void parseResponse_noAnswer();

    void rankBySpeed_ordersByTimeAndFailuresLast();
    void fastestServer_picksFastest();
    void fastestServer_noSuccess();

    void evaluateSites_allPass();
    void evaluateSites_partialWarns();
    void evaluateSites_allFail();
    void evaluateSites_emptySkips();

    void parseSnapshot_fields();
    void parseSnapshot_malformed();

    void evaluateLink_pass();
    void evaluateLink_noOnlineAdapter();
    void evaluateLink_noIp();
    void evaluateLink_virtualDefaultAdapterWarns();
    void evaluateLink_emptySnapshot();

    void evaluateRoute_pass();
    void evaluateRoute_noDefaultRoute();

    void evaluateDns_pass();
    void evaluateDns_allFailed();
    void evaluateDns_noServers();

    void diagnose_firstFailureStopsDownstream();
    void diagnose_allPass();
    void diagnose_proxyFailureStopsDownstream();

    void parseProxyServer_plain();
    void parseProxyServer_schemeListPrefersHttps();
    void parseProxyServer_defaultPort();
    void parseProxyServer_invalid();

    void evaluateProxy_disabled();
    void evaluateProxy_pacWarns();
    void evaluateProxy_unreachable();
    void evaluateProxy_notProbed();

    void evaluateTcp_pass();
    void evaluateTcp_failed();
    void evaluateTcp_notRun();

    void evaluateHttp_pass();
    void evaluateHttp_failed();
};

void NetDiagTest::buildQuery_hasHeaderAndQuestion()
{
    const QByteArray packet = DnsProbe::buildQuery(QStringLiteral("example.com"), 0x1234);
    QCOMPARE(packet.size(), 12 + 13 + 4);
    QCOMPARE(packet.at(0), static_cast<char>(0x12));
    QCOMPARE(packet.at(1), static_cast<char>(0x34));
    QCOMPARE(packet.at(4), static_cast<char>(0));
    QCOMPARE(packet.at(5), static_cast<char>(1));
    QCOMPARE(packet.at(12), static_cast<char>(7));
}

void NetDiagTest::parseResponse_aRecord()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(DnsProbe::parseResponse(makeResponse(0x1234, 0, QStringLiteral("1.2.3.4")),
                                   0x1234, &result, &error));
    QCOMPARE(result.rcode, 0);
    QCOMPARE(result.status, DnsProbe::Status::Ok);
    QCOMPARE(result.addresses, QStringList{QStringLiteral("1.2.3.4")});
}

void NetDiagTest::parseResponse_highOctets()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(DnsProbe::parseResponse(
        makeResponse(0x1234, 0, QStringLiteral("220.181.38.168")),
        0x1234, &result, &error));
    QCOMPARE(result.status, DnsProbe::Status::Ok);
    QCOMPARE(result.addresses, QStringList{QStringLiteral("220.181.38.168")});
}

void NetDiagTest::parseResponse_nxdomain()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(DnsProbe::parseResponse(makeResponse(0x1234, 3, QString()), 0x1234, &result, &error));
    QCOMPARE(result.status, DnsProbe::Status::NxDomain);
    QCOMPARE(result.rcode, 3);
}

void NetDiagTest::parseResponse_servfail()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(DnsProbe::parseResponse(makeResponse(0x1234, 2, QString()), 0x1234, &result, &error));
    QCOMPARE(result.status, DnsProbe::Status::ServFail);
}

void NetDiagTest::parseResponse_idMismatch()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(!DnsProbe::parseResponse(makeResponse(0x1234, 0, QStringLiteral("1.2.3.4")),
                                    0x5678, &result, &error));
    QVERIFY(!error.isEmpty());
}

void NetDiagTest::parseResponse_truncated()
{
    DnsProbe::Result result;
    QString error;
    QByteArray packet = makeResponse(0x1234, 0, QStringLiteral("1.2.3.4"));
    packet.truncate(10);
    QVERIFY(!DnsProbe::parseResponse(packet, 0x1234, &result, &error));
    QVERIFY(!error.isEmpty());
}

void NetDiagTest::parseResponse_noAnswer()
{
    DnsProbe::Result result;
    QString error;
    QVERIFY(DnsProbe::parseResponse(makeResponse(0x1234, 0, QString()), 0x1234, &result, &error));
    QCOMPARE(result.status, DnsProbe::Status::NoAnswer);
}

void NetDiagTest::rankBySpeed_ordersByTimeAndFailuresLast()
{
    QVector<DnsProbe::Result> results;
    auto add = [&results](const QString &server, DnsProbe::Status status, int ms) {
        DnsProbe::Result result;
        result.server = server;
        result.status = status;
        result.elapsedMs = ms;
        results.append(result);
    };

    add(QStringLiteral("8.8.8.8"), DnsProbe::Status::Ok, 120);
    add(QStringLiteral("1.1.1.1"), DnsProbe::Status::NoResponse, 1500);
    add(QStringLiteral("223.5.5.5"), DnsProbe::Status::Ok, 18);
    add(QStringLiteral("114.114.114.114"), DnsProbe::Status::ServFail, 30);

    const QVector<DnsProbe::Result> ranked = DnsProbe::rankBySpeed(results);
    QCOMPARE(ranked.size(), 4);
    QCOMPARE(ranked.at(0).server, QStringLiteral("223.5.5.5"));
    QCOMPARE(ranked.at(1).server, QStringLiteral("8.8.8.8"));
    // 失败项保持输入顺序，排在成功项之后
    QCOMPARE(ranked.at(2).server, QStringLiteral("1.1.1.1"));
    QCOMPARE(ranked.at(3).server, QStringLiteral("114.114.114.114"));
}

void NetDiagTest::fastestServer_picksFastest()
{
    QVector<DnsProbe::Result> results;
    DnsProbe::Result slow;
    slow.server = QStringLiteral("8.8.8.8");
    slow.status = DnsProbe::Status::Ok;
    slow.elapsedMs = 120;
    results.append(slow);

    DnsProbe::Result fast;
    fast.server = QStringLiteral("223.5.5.5");
    fast.status = DnsProbe::Status::Ok;
    fast.elapsedMs = 18;
    results.append(fast);

    QCOMPARE(DnsProbe::fastestServer(results), QStringLiteral("223.5.5.5"));
}

void NetDiagTest::fastestServer_noSuccess()
{
    DnsProbe::Result failed;
    failed.server = QStringLiteral("1.1.1.1");
    failed.status = DnsProbe::Status::NoResponse;
    failed.elapsedMs = 1500;
    QVERIFY(DnsProbe::fastestServer(QVector<DnsProbe::Result>{failed}).isEmpty());
    QVERIFY(DnsProbe::fastestServer(QVector<DnsProbe::Result>()).isEmpty());
}

void NetDiagTest::evaluateSites_allPass()
{
    NetProbe::SiteResult baidu;
    baidu.label = QStringLiteral("百度");
    baidu.resolved = true;
    baidu.ok = true;
    baidu.elapsedMs = 20;

    NetProbe::SiteResult tencent;
    tencent.label = QStringLiteral("腾讯");
    tencent.resolved = true;
    tencent.ok = true;
    tencent.elapsedMs = 35;

    const NetDiag::CheckItem item =
        NetDiag::evaluateSites({baidu, tencent});
    QCOMPARE(item.layer, NetDiag::Layer::Targets);
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QCOMPARE(item.elapsedMs, 20);
    QVERIFY(item.detail.contains(QStringLiteral("百度")));
    QVERIFY(item.detail.contains(QStringLiteral("腾讯")));
}

void NetDiagTest::evaluateSites_partialWarns()
{
    NetProbe::SiteResult baidu;
    baidu.label = QStringLiteral("百度");
    baidu.resolved = true;
    baidu.ok = true;
    baidu.elapsedMs = 20;

    NetProbe::SiteResult tencent;
    tencent.label = QStringLiteral("腾讯");
    tencent.resolved = true;
    tencent.ok = false;
    tencent.error = QStringLiteral("连接超时");

    NetProbe::SiteResult microsoft;
    microsoft.label = QStringLiteral("微软");
    microsoft.resolved = false;

    const NetDiag::CheckItem item =
        NetDiag::evaluateSites({baidu, tencent, microsoft});
    QCOMPARE(item.status, NetDiag::Status::Warn);
    QVERIFY(item.detail.contains(QStringLiteral("腾讯")));
    QVERIFY(item.detail.contains(QStringLiteral("微软")));
}

void NetDiagTest::evaluateSites_allFail()
{
    NetProbe::SiteResult baidu;
    baidu.label = QStringLiteral("百度");
    baidu.resolved = true;
    baidu.ok = false;

    NetProbe::SiteResult tencent;
    tencent.label = QStringLiteral("腾讯");
    tencent.resolved = false;

    const NetDiag::CheckItem item =
        NetDiag::evaluateSites({baidu, tencent});
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("均无法访问")));
}

void NetDiagTest::evaluateSites_emptySkips()
{
    const NetDiag::CheckItem item = NetDiag::evaluateSites({});
    QCOMPARE(item.status, NetDiag::Status::Skip);
    QCOMPARE(item.layer, NetDiag::Layer::Targets);
}

void NetDiagTest::parseSnapshot_fields()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    QVERIFY(snapshot.error.isEmpty());
    QCOMPARE(snapshot.adapters.size(), 2);
    QCOMPARE(snapshot.defaultAdapter, QStringLiteral("WLAN"));
    QCOMPARE(snapshot.adapters.at(0).dns, QStringList({QStringLiteral("223.5.5.5"),
                                                      QStringLiteral("8.8.8.8")}));
    QCOMPARE(snapshot.adapters.at(0).interfaceIndex, 12);
    QVERIFY(snapshot.adapters.at(1).virtualAdapter);
    QVERIFY(!snapshot.proxy.enabled);
}

void NetDiagTest::parseSnapshot_malformed()
{
    QString error;
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(QByteArrayLiteral("{oops"), &error);
    QVERIFY(!snapshot.error.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(snapshot.adapters.isEmpty());
}

void NetDiagTest::evaluateLink_pass()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::CheckItem item = NetDiag::evaluateLink(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QVERIFY(item.detail.contains(QStringLiteral("WLAN")));
}

void NetDiagTest::evaluateLink_noOnlineAdapter()
{
    NetDiag::Snapshot snapshot;
    NetDiag::AdapterInfo adapter;
    adapter.name = QStringLiteral("WLAN");
    adapter.status = QStringLiteral("Disconnected");
    snapshot.adapters.append(adapter);

    const NetDiag::CheckItem item = NetDiag::evaluateLink(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Fail);
}

void NetDiagTest::evaluateLink_noIp()
{
    NetDiag::Snapshot snapshot;
    NetDiag::AdapterInfo adapter;
    adapter.name = QStringLiteral("WLAN");
    adapter.status = QStringLiteral("Up");
    snapshot.adapters.append(adapter);

    const NetDiag::CheckItem item = NetDiag::evaluateLink(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("IPv4")));
}

void NetDiagTest::evaluateLink_virtualDefaultAdapterWarns()
{
    NetDiag::Snapshot snapshot;
    NetDiag::AdapterInfo adapter;
    adapter.name = QStringLiteral("xray_tun");
    adapter.status = QStringLiteral("Up");
    adapter.ipv4 = QStringLiteral("10.0.0.2");
    adapter.virtualAdapter = true;
    snapshot.adapters.append(adapter);
    snapshot.defaultAdapter = QStringLiteral("xray_tun");

    const NetDiag::CheckItem item = NetDiag::evaluateLink(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Warn);
    QVERIFY(item.detail.contains(QStringLiteral("xray_tun")));
}

void NetDiagTest::evaluateLink_emptySnapshot()
{
    NetDiag::Snapshot snapshot;
    snapshot.error = QStringLiteral("diagnostic snapshot unavailable");
    const NetDiag::CheckItem item = NetDiag::evaluateLink(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Skip);
}

void NetDiagTest::evaluateRoute_pass()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::CheckItem item = NetDiag::evaluateRoute(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QVERIFY(item.detail.contains(QStringLiteral("WLAN")));
}

void NetDiagTest::evaluateRoute_noDefaultRoute()
{
    NetDiag::Snapshot snapshot;
    snapshot.defaultAdapter.clear();
    const NetDiag::CheckItem item = NetDiag::evaluateRoute(snapshot);
    QCOMPARE(item.status, NetDiag::Status::Fail);
}

void NetDiagTest::evaluateDns_pass()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::CheckItem item = NetDiag::evaluateDns(snapshot, dnsResults(true));
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QCOMPARE(item.elapsedMs, 23);
    QVERIFY(item.detail.contains(QStringLiteral("223.5.5.5")));
}

void NetDiagTest::evaluateDns_allFailed()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::CheckItem item = NetDiag::evaluateDns(snapshot, dnsResults(false));
    QCOMPARE(item.status, NetDiag::Status::Fail);
}

void NetDiagTest::evaluateDns_noServers()
{
    NetDiag::Snapshot snapshot;
    NetDiag::AdapterInfo adapter;
    adapter.name = QStringLiteral("WLAN");
    adapter.status = QStringLiteral("Up");
    adapter.ipv4 = QStringLiteral("192.168.1.5");
    snapshot.adapters.append(adapter);

    const NetDiag::CheckItem item = NetDiag::evaluateDns(snapshot, dnsResults(true));
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("DNS")));
}

void NetDiagTest::diagnose_firstFailureStopsDownstream()
{
    NetDiag::Snapshot snapshot;
    NetDiag::AdapterInfo adapter;
    adapter.name = QStringLiteral("WLAN");
    adapter.status = QStringLiteral("Disconnected");
    snapshot.adapters.append(adapter);

    const NetDiag::DiagResult result = NetDiag::diagnose(snapshot, dnsResults(true));
    QVERIFY(result.hasFailure);
    QCOMPARE(result.firstFailed, NetDiag::Layer::Link);

    for (const NetDiag::CheckItem &item : result.items) {
        if (item.layer > NetDiag::Layer::Link)
            QCOMPARE(item.status, NetDiag::Status::Skip);
    }
}

void NetDiagTest::diagnose_allPass()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::DiagResult result = NetDiag::diagnose(snapshot, dnsResults(true));
    QVERIFY(!result.hasFailure);
    QVERIFY(result.summary.contains(QStringLiteral("通过")));
    QCOMPARE(result.items.at(0).status, NetDiag::Status::Pass);
}

void NetDiagTest::diagnose_proxyFailureStopsDownstream()
{
    NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    snapshot.proxy.enabled = true;
    snapshot.proxy.server = QStringLiteral("127.0.0.1:10808");

    NetProbe::Outcome outcome;
    outcome.proxy.ran = true;
    outcome.proxy.ok = false;
    outcome.proxy.error = QStringLiteral("Connection refused");
    outcome.target.ran = true;
    outcome.target.ok = true;
    outcome.target.host = QStringLiteral("1.2.3.4");
    outcome.target.port = 443;
    outcome.target.elapsedMs = 12;
    outcome.https.ran = true;
    outcome.https.ok = true;
    outcome.https.status = 200;

    const NetDiag::DiagResult result =
        NetDiag::diagnose(snapshot, dnsResults(true), outcome);
    QVERIFY(result.hasFailure);
    QCOMPARE(result.firstFailed, NetDiag::Layer::Proxy);
    QVERIFY(result.suggestion.contains(QStringLiteral("代理")));

    for (const NetDiag::CheckItem &item : result.items) {
        if (item.layer == NetDiag::Layer::Tcp || item.layer == NetDiag::Layer::Http)
            QCOMPARE(item.status, NetDiag::Status::Skip);
    }
}

void NetDiagTest::parseProxyServer_plain()
{
    const NetDiag::ProxyEndpoint endpoint =
        NetDiag::parseProxyServer(QStringLiteral("127.0.0.1:10808"));
    QVERIFY(endpoint.valid());
    QCOMPARE(endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(endpoint.port, static_cast<quint16>(10808));
}

void NetDiagTest::parseProxyServer_schemeListPrefersHttps()
{
    const NetDiag::ProxyEndpoint endpoint = NetDiag::parseProxyServer(
        QStringLiteral("http=127.0.0.1:10809;https=127.0.0.1:10808"));
    QCOMPARE(endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(endpoint.port, static_cast<quint16>(10808));
}

void NetDiagTest::parseProxyServer_defaultPort()
{
    const NetDiag::ProxyEndpoint endpoint =
        NetDiag::parseProxyServer(QStringLiteral("proxy.local"));
    QVERIFY(endpoint.valid());
    QCOMPARE(endpoint.host, QStringLiteral("proxy.local"));
    QCOMPARE(endpoint.port, static_cast<quint16>(80));
}

void NetDiagTest::parseProxyServer_invalid()
{
    QVERIFY(!NetDiag::parseProxyServer(QString()).valid());
    QVERIFY(!NetDiag::parseProxyServer(QStringLiteral("   ")).valid());
}

void NetDiagTest::evaluateProxy_disabled()
{
    const NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    const NetDiag::CheckItem item =
        NetDiag::evaluateProxy(snapshot, NetProbe::TcpResult());
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QVERIFY(item.detail.contains(QStringLiteral("直连")));
}

void NetDiagTest::evaluateProxy_pacWarns()
{
    NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    snapshot.proxy.enabled = true;
    snapshot.proxy.pacUrl = QStringLiteral("http://pac.local/proxy.pac");

    const NetDiag::CheckItem item =
        NetDiag::evaluateProxy(snapshot, NetProbe::TcpResult());
    QCOMPARE(item.status, NetDiag::Status::Warn);
    QVERIFY(item.detail.contains(QStringLiteral("PAC")));
}

void NetDiagTest::evaluateProxy_unreachable()
{
    NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    snapshot.proxy.enabled = true;
    snapshot.proxy.server = QStringLiteral("127.0.0.1:10808");

    NetProbe::TcpResult probe;
    probe.ran = true;
    probe.ok = false;
    probe.error = QStringLiteral("Connection refused");

    const NetDiag::CheckItem item = NetDiag::evaluateProxy(snapshot, probe);
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("10808")));
    QVERIFY(item.detail.contains(QStringLiteral("Connection refused")));
}

void NetDiagTest::evaluateProxy_notProbed()
{
    NetDiag::Snapshot snapshot = NetDiag::parseSnapshot(sampleSnapshot());
    snapshot.proxy.enabled = true;
    snapshot.proxy.server = QStringLiteral("127.0.0.1:10808");

    const NetDiag::CheckItem item =
        NetDiag::evaluateProxy(snapshot, NetProbe::TcpResult());
    QCOMPARE(item.status, NetDiag::Status::Skip);
}

void NetDiagTest::evaluateTcp_pass()
{
    NetProbe::TcpResult probe;
    probe.ran = true;
    probe.ok = true;
    probe.host = QStringLiteral("1.2.3.4");
    probe.port = 443;
    probe.elapsedMs = 18;

    const NetDiag::CheckItem item = NetDiag::evaluateTcp(dnsResults(true), probe);
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QCOMPARE(item.elapsedMs, 18);
    QVERIFY(item.detail.contains(QStringLiteral("1.2.3.4:443")));
}

void NetDiagTest::evaluateTcp_failed()
{
    NetProbe::TcpResult probe;
    probe.ran = true;
    probe.ok = false;
    probe.host = QStringLiteral("1.2.3.4");
    probe.port = 443;
    probe.error = QStringLiteral("Connection timed out");

    const NetDiag::CheckItem item = NetDiag::evaluateTcp(dnsResults(true), probe);
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("Connection timed out")));
}

void NetDiagTest::evaluateTcp_notRun()
{
    const NetDiag::CheckItem item =
        NetDiag::evaluateTcp(dnsResults(true), NetProbe::TcpResult());
    QCOMPARE(item.status, NetDiag::Status::Skip);
}

void NetDiagTest::evaluateHttp_pass()
{
    NetProbe::HttpResult probe;
    probe.ran = true;
    probe.ok = true;
    probe.status = 200;
    probe.elapsedMs = 56;

    const NetDiag::CheckItem item = NetDiag::evaluateHttp(probe);
    QCOMPARE(item.status, NetDiag::Status::Pass);
    QCOMPARE(item.elapsedMs, 56);
    QVERIFY(item.detail.contains(QStringLiteral("200")));
}

void NetDiagTest::evaluateHttp_failed()
{
    NetProbe::HttpResult probe;
    probe.ran = true;
    probe.ok = false;
    probe.error = QStringLiteral("SSL handshake failed");

    const NetDiag::CheckItem item = NetDiag::evaluateHttp(probe);
    QCOMPARE(item.status, NetDiag::Status::Fail);
    QVERIFY(item.detail.contains(QStringLiteral("SSL handshake failed")));
}

QTEST_MAIN(NetDiagTest)
#include "netdiagtest.moc"
