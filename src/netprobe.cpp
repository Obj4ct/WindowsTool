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

} // namespace NetProbe
