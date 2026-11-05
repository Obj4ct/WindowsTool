/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef NETPROBE_H
#define NETPROBE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "dnsprobe.h"

class QTcpSocket;
class QTimer;
class QElapsedTimer;

/**
 * 分层诊断的 L4/L6 探测器。
 *
 * Qt 只负责 TCP 连接（QTcpSocket），不依赖 TLS 后端：
 *  - L4：对 DNS 解析出的目标地址发起 TCP 连接，判断端口是否可达；
 *  - L6：系统代理启用时，单独探测代理监听端口是否可连接。
 *
 * L5 的 HTTPS 请求由 ps1/netdiag.ps1 里的 Invoke-WebRequest 完成，
 * 因为本机 Qt 安装可能不带 TLS 后端插件（QNetworkAccessManager 会报
 * “TLS initialization failed”）。脚本把状态码写进快照，Qt 只解析数据。
 *
 * 两项并发执行，总预算一个 timeout，事件驱动、不阻塞界面。
 */
namespace NetProbe {

struct TcpResult {
    bool ran = false;        // 是否真的发起了探测
    bool ok = false;         // 是否连接成功
    QString host;
    quint16 port = 0;
    int elapsedMs = -1;
    QString error;
};

struct HttpResult {
    bool ran = false;
    bool ok = false;         // 拿到 2xx/3xx 响应
    int status = 0;          // HTTP 状态码，网络错误时为 0
    int elapsedMs = -1;
    QString error;
};

struct Outcome {
    TcpResult target;        // L4 目标端口
    HttpResult https;        // L5 HTTPS 响应（由脚本快照填充）
    TcpResult proxy;         // L6 系统代理端口
};

class Runner : public QObject
{
    Q_OBJECT

public:
    explicit Runner(QObject *parent = nullptr);
    ~Runner() override;

    /**
     * 开始探测；TCP 目标与代理端口并行。
     *
     * @param targetHost  TCP 目标地址，为空则跳过 L4
     * @param targetPort  TCP 目标端口
     * @param proxyHost   代理地址，为空则跳过 L6
     * @param proxyPort   代理端口
     * @param timeoutMs   总预算
     */
    void start(const QString &targetHost, quint16 targetPort,
               const QString &proxyHost, quint16 proxyPort,
               int timeoutMs = 1500);

    /** 放弃本次探测：清理套接字，不再发出 finished */
    void cancel();

    bool isRunning() const { return m_active; }

signals:
    void finished(const NetProbe::Outcome &outcome);

private:
    void startTcp(QTcpSocket *socket, const QString &host, quint16 port,
                  bool proxy);
    void onConnected(QTcpSocket *socket, bool proxy);
    void onSocketError(QTcpSocket *socket, bool proxy);
    void onTimeout();
    void markTcpDone(QTcpSocket *socket, bool proxy, bool ok,
                     const QString &error);
    void maybeFinish();

    QTcpSocket *m_targetSocket = nullptr;
    QTcpSocket *m_proxySocket = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer *m_clock = nullptr;

    Outcome m_outcome;
    int m_pending = 0;
    bool m_active = false;
    bool m_timedOut = false;
};

/** 多目标连通性检测的目标站点 */
struct SiteTarget {
    QString label;
    QString domain;
    quint16 port = 443;
};

/** 单个目标站点的检测结果：先 DNS 解析，再连接端口 */
struct SiteResult {
    QString label;
    QString domain;
    QString address;
    bool resolved = false;
    bool ok = false;
    int dnsMs = -1;
    int elapsedMs = -1;
    QString error;
};

/**
 * 并发检测多个目标站点。
 *
 * 每个目标独立完成「DNS 解析 → TCP 连接」，互不影响：单个站点失败
 * 不会中断其它站点，从而区分「全局断网」与「单站点故障」。
 * 两段探测都由各自的计时器兜底，总耗时不超过约两个 timeout。
 */
class TargetRunner : public QObject
{
    Q_OBJECT

public:
    explicit TargetRunner(QObject *parent = nullptr);
    ~TargetRunner() override;

    void start(const QVector<SiteTarget> &targets, const QStringList &dnsServers,
               int timeoutMs = 1200);

    /** 放弃本次检测：清理子探测器，不再发出 finished */
    void cancel();

    bool isRunning() const { return m_active; }

signals:
    void finished(const QVector<NetProbe::SiteResult> &results);

private:
    void onDnsDone(int index, const QVector<DnsProbe::Result> &results);
    void startTcpPhase();
    void onTcpDone(int index, const NetProbe::Outcome &outcome);
    void finish();

    QVector<SiteTarget> m_targets;
    QVector<SiteResult> m_results;
    QVector<DnsProbe::AsyncRunner *> m_dnsRunners;
    QVector<Runner *> m_tcpRunners;
    QStringList m_dnsServers;
    int m_timeoutMs = 1200;
    int m_dnsPending = 0;
    int m_tcpPending = 0;
    bool m_active = false;
    bool m_starting = false;
};

} // namespace NetProbe

#endif // NETPROBE_H
