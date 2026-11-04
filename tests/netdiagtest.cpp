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
    void parseResponse_nxdomain();
    void parseResponse_servfail();
    void parseResponse_idMismatch();
    void parseResponse_truncated();
    void parseResponse_noAnswer();

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

QTEST_MAIN(NetDiagTest)
#include "netdiagtest.moc"
