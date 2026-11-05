/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef DNSPROBE_H
#define DNSPROBE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;
class QUdpSocket;

/**
 * 极简 UDP DNS 探测器（只查 A 记录）。
 *
 * 不调用 nslookup / Resolve-DnsName，也不解析任何本地化文本：
 * 自己组装查询报文、自己解析响应头与 RCODE，因此结论与系统语言无关，
 * 也能拿到真实 RTT、区分超时 / NXDOMAIN / SERVFAIL。
 */
namespace DnsProbe {

enum class Status {
    Ok,         // 有 A 记录
    NoResponse, // 超时或网络不可达
    NoAnswer,   // 响应正常但没有任何 A 记录
    NxDomain,   // RCODE 3：域名不存在
    ServFail,   // RCODE 2：服务器失败
    Refused,    // RCODE 5：服务器拒绝
    Error       // 报文构造 / 服务器地址非法
};

struct Result {
    Status status = Status::NoResponse;
    int rcode = -1;          // DNS 响应码，超时时为 -1
    int elapsedMs = -1;      // 查询耗时
    QStringList addresses;   // 解析到的 IPv4 地址
    QString server;          // 本次查询使用的 DNS 服务器
    QString error;           // Error 状态时的原因
};

/** 组包：标准查询报文，QDCOUNT=1，QTYPE=A，QCLASS=IN，RD 置位 */
QByteArray buildQuery(const QString &domain, quint16 id);

/**
 * 解析响应报文。
 *
 * @param packet      收到的原始报文
 * @param expectedId  查询时使用的 ID，用于过滤迟到的旧响应
 * @param result      解析结果（rcode / addresses）
 * @param error       解析失败时的原因，可为 nullptr
 * @return 报文合法且 ID 匹配时返回 true
 */
bool parseResponse(const QByteArray &packet, quint16 expectedId,
                   Result *result, QString *error = nullptr);

/**
 * 同步查询，最长等待 timeoutMs。
 *
 * 仅支持 IPv4 的 DNS 服务器地址（A 记录探测的常见场景）。
 */
Result query(const QString &server, const QString &domain,
             int timeoutMs = 2000, quint16 port = 53);

/**
 * 按响应速度排序：解析成功且拿到耗时的服务器排在前，耗时升序；
 * 失败的服务器保持原顺序排在后面。
 */
QVector<Result> rankBySpeed(const QVector<Result> &results);

/** 响应最快的可用服务器；没有成功结果时返回空字符串 */
QString fastestServer(const QVector<Result> &results);

/** 状态的中文短标签，用于测速结果展示 */
QString statusLabel(Status status);

/**
 * 非阻塞的多服务器 DNS 探测器（供 UI 使用）。
 *
 * 一次并发查询多个服务器，总预算一个 timeout，事件驱动，不占线程、
 * 不阻塞界面；超时未响应的服务器标记为 NoResponse。
 */
class AsyncRunner : public QObject
{
    Q_OBJECT

public:
    explicit AsyncRunner(QObject *parent = nullptr);

    /** 开始探测；servers 去重，最多取前 maxServers 个（默认 4） */
    void start(const QStringList &servers, const QString &domain,
               int timeoutMs = 1500, int maxServers = 4);

    void cancel();
    bool isRunning() const { return m_active; }

signals:
    void completed(const QVector<DnsProbe::Result> &results);

private:
    void onReadyRead();
    void finishAll();

    struct Pending {
        QString server;
        quint16 id = 0;
    };

    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QVector<Pending> m_pending;
    QVector<Result> m_results;
    int m_timeoutMs = 0;
    bool m_active = false;
};

} // namespace DnsProbe

#endif // DNSPROBE_H
