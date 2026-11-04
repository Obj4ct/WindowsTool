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
#include <QString>
#include <QStringList>

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

} // namespace DnsProbe

#endif // DNSPROBE_H
