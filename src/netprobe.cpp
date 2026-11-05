/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "netprobe.h"

#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>

namespace NetProbe {

Runner::Runner(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_clock(new QElapsedTimer)
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Runner::onTimeout);
}

Runner::~Runner()
{
    delete m_clock;
}

void Runner::start(const QString &targetHost, quint16 targetPort,
                   const QString &proxyHost, quint16 proxyPort, int timeoutMs)
{
    cancel();

    m_outcome = Outcome();
    m_timedOut = false;
    m_pending = 0;
    m_active = true;
    m_clock->start();

    if (!targetHost.isEmpty()) {
        m_outcome.target.ran = true;
        m_outcome.target.host = targetHost;
        m_outcome.target.port = targetPort;
        m_targetSocket = new QTcpSocket(this);
        startTcp(m_targetSocket, targetHost, targetPort, false);
        ++m_pending;
    }

    if (!proxyHost.isEmpty()) {
        m_outcome.proxy.ran = true;
        m_outcome.proxy.host = proxyHost;
        m_outcome.proxy.port = proxyPort;
        m_proxySocket = new QTcpSocket(this);
        startTcp(m_proxySocket, proxyHost, proxyPort, true);
        ++m_pending;
    }

    if (m_pending == 0) {
        m_active = false;
        // 同步回调会打断调用方刚发出的流程，改成事件循环下一轮再通知
        QTimer::singleShot(0, this, [this]() { emit finished(m_outcome); });
        return;
    }

    m_timer->start(timeoutMs > 0 ? timeoutMs : 1500);
}

void Runner::startTcp(QTcpSocket *socket, const QString &host, quint16 port,
                      bool proxy)
{
    socket->setProperty("done", false);
    connect(socket, &QTcpSocket::connected, this,
            [this, socket, proxy]() { onConnected(socket, proxy); });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket, proxy](QAbstractSocket::SocketError) {
                onSocketError(socket, proxy);
            });
    socket->connectToHost(host, port);
}

void Runner::onConnected(QTcpSocket *socket, bool proxy)
{
    if (socket->property("done").toBool())
        return;
    markTcpDone(socket, proxy, true, QString());
}

void Runner::onSocketError(QTcpSocket *socket, bool proxy)
{
    if (socket->property("done").toBool())
        return;

    QString error = socket->errorString();
    if (m_timedOut)
        error = QStringLiteral("连接超时（%1 ms）").arg(m_clock->elapsed());
    markTcpDone(socket, proxy, false, error);
}

void Runner::markTcpDone(QTcpSocket *socket, bool proxy, bool ok,
                         const QString &error)
{
    socket->setProperty("done", true);

    TcpResult *result = proxy ? &m_outcome.proxy : &m_outcome.target;
    result->ok = ok;
    result->elapsedMs = static_cast<int>(m_clock->elapsed());
    result->error = error;

    if (proxy) {
        if (m_proxySocket == socket)
            m_proxySocket = nullptr;
    } else if (m_targetSocket == socket) {
        m_targetSocket = nullptr;
    }

    socket->abort();
    socket->deleteLater();

    --m_pending;
    maybeFinish();
}

void Runner::onTimeout()
{
    if (!m_active)
        return;

    m_timedOut = true;

    if (m_targetSocket)
        markTcpDone(m_targetSocket, false, false, QStringLiteral("连接超时"));

    if (m_proxySocket)
        markTcpDone(m_proxySocket, true, false, QStringLiteral("连接超时"));
}

void Runner::maybeFinish()
{
    if (!m_active || m_pending > 0)
        return;

    m_active = false;
    m_timer->stop();
    emit finished(m_outcome);
}

void Runner::cancel()
{
    m_active = false;
    m_timer->stop();
    m_pending = 0;

    if (m_targetSocket) {
        m_targetSocket->setProperty("done", true);
        m_targetSocket->abort();
        m_targetSocket->deleteLater();
        m_targetSocket = nullptr;
    }
    if (m_proxySocket) {
        m_proxySocket->setProperty("done", true);
        m_proxySocket->abort();
        m_proxySocket->deleteLater();
        m_proxySocket = nullptr;
    }
}

TargetRunner::TargetRunner(QObject *parent)
    : QObject(parent)
{
}

TargetRunner::~TargetRunner() = default;

void TargetRunner::start(const QVector<SiteTarget> &targets,
                         const QStringList &dnsServers, int timeoutMs)
{
    cancel();

    m_targets = targets;
    m_dnsServers = dnsServers;
    m_timeoutMs = timeoutMs > 0 ? timeoutMs : 1200;

    m_results.clear();
    m_results.resize(m_targets.size());
    for (int i = 0; i < m_targets.size(); ++i) {
        m_results[i].label = m_targets.at(i).label;
        m_results[i].domain = m_targets.at(i).domain;
    }

    m_active = true;
    m_starting = true;
    m_dnsPending = 0;
    m_tcpPending = 0;

    for (int i = 0; i < m_targets.size(); ++i) {
        if (m_dnsServers.isEmpty() || m_targets.at(i).domain.isEmpty()) {
            m_results[i].error = QStringLiteral("没有可用的 DNS 服务器");
            continue;
        }

        DnsProbe::AsyncRunner *runner = new DnsProbe::AsyncRunner(this);
        m_dnsRunners.append(runner);
        connect(runner, &DnsProbe::AsyncRunner::completed, this,
                [this, i](const QVector<DnsProbe::Result> &results) {
                    onDnsDone(i, results);
                });
        ++m_dnsPending;
        runner->start(m_dnsServers, m_targets.at(i).domain, m_timeoutMs, 4);
    }

    m_starting = false;
    if (m_dnsPending == 0)
        startTcpPhase();
}

void TargetRunner::onDnsDone(int index,
                             const QVector<DnsProbe::Result> &results)
{
    if (!m_active || index < 0 || index >= m_results.size())
        return;

    SiteResult &site = m_results[index];
    const DnsProbe::Result *best = nullptr;
    for (const DnsProbe::Result &candidate : results) {
        if (candidate.status != DnsProbe::Status::Ok || candidate.elapsedMs < 0)
            continue;
        if (!best || candidate.elapsedMs < best->elapsedMs)
            best = &candidate;
    }

    if (best && !best->addresses.isEmpty()) {
        site.resolved = true;
        site.address = best->addresses.first();
        site.dnsMs = best->elapsedMs;
    } else {
        site.error = QStringLiteral("域名解析失败");
    }

    --m_dnsPending;
    if (!m_starting && m_dnsPending == 0)
        startTcpPhase();
}

void TargetRunner::startTcpPhase()
{
    if (!m_active)
        return;

    m_starting = true;
    m_tcpPending = 0;

    for (int i = 0; i < m_results.size(); ++i) {
        if (!m_results.at(i).resolved)
            continue;

        Runner *runner = new Runner(this);
        m_tcpRunners.append(runner);
        connect(runner, &Runner::finished, this,
                [this, i](const NetProbe::Outcome &outcome) {
                    onTcpDone(i, outcome);
                });
        ++m_tcpPending;
        runner->start(m_results.at(i).address, m_targets.at(i).port,
                      QString(), 0, m_timeoutMs);
    }

    m_starting = false;

    for (DnsProbe::AsyncRunner *runner : m_dnsRunners)
        runner->deleteLater();
    m_dnsRunners.clear();

    if (m_tcpPending == 0)
        finish();
}

void TargetRunner::onTcpDone(int index, const NetProbe::Outcome &outcome)
{
    if (!m_active || index < 0 || index >= m_results.size())
        return;

    SiteResult &site = m_results[index];
    site.ok = outcome.target.ok;
    site.elapsedMs = outcome.target.elapsedMs;
    if (!site.ok) {
        site.error = outcome.target.error.isEmpty()
                         ? QStringLiteral("连接失败")
                         : outcome.target.error;
    }

    --m_tcpPending;
    if (!m_starting && m_tcpPending == 0)
        finish();
}

void TargetRunner::finish()
{
    if (!m_active)
        return;

    m_active = false;
    m_starting = false;

    for (Runner *runner : m_tcpRunners)
        runner->deleteLater();
    m_tcpRunners.clear();

    emit finished(m_results);
}

void TargetRunner::cancel()
{
    m_active = false;
    m_starting = false;
    m_dnsPending = 0;
    m_tcpPending = 0;

    for (DnsProbe::AsyncRunner *runner : m_dnsRunners) {
        runner->cancel();
        runner->deleteLater();
    }
    m_dnsRunners.clear();

    for (Runner *runner : m_tcpRunners) {
        runner->cancel();
        runner->deleteLater();
    }
    m_tcpRunners.clear();

    m_results.clear();
    m_targets.clear();
}

} // namespace NetProbe
