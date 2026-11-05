/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef NETDIAG_H
#define NETDIAG_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "dnsprobe.h"
#include "netprobe.h"

/**
 * 分层网络诊断引擎。
 *
 * 设计原则（见 docs/重构方案.md 第四章）：
 *  - 本机状态由 ps1/netdiag.ps1 一次性输出 JSON，Qt 只解析数据，不解析本地化文本；
 *  - 解析与判定是纯函数，不碰 UI、不启动进程，便于单元测试；
 *  - 从 L1 往上检查，报告第一个失败的层。
 *
 * L1–L3 由快照与 DNS 探测判定；L4/L6 由 NetProbe::Runner 的 TCP 探测判定，
 * L5 的 HTTPS 结果由 netdiag.ps1 直接写进快照；L7 暂未接入浏览器级检测。
 */
namespace NetDiag {

enum class Layer { Link = 1, Route, Dns, Tcp, Http, Proxy, App };
enum class Status { Pass, Fail, Warn, Skip };

struct AdapterInfo {
    QString name;
    QString description;
    QString status;          // Get-NetAdapter.Status 英文枚举：Up / Disconnected ...
    QString ipv4;
    QString gateway;
    QStringList dns;
    bool virtualAdapter = false;
    bool defaultRoute = false;
    int interfaceIndex = 0;
};

struct ProxyInfo {
    bool enabled = false;
    QString server;
    QString pacUrl;
};

struct Snapshot {
    QVector<AdapterInfo> adapters;
    ProxyInfo proxy;
    NetProbe::HttpResult https;  // 脚本里 Invoke-WebRequest 的结果
    QString defaultAdapter;
    QString error;           // 快照不可用时的原因（脚本缺失 / JSON 解析失败）
};

struct CheckItem {
    Layer layer = Layer::Link;
    QString name;
    Status status = Status::Skip;
    QString detail;
    int elapsedMs = -1;
};

struct DiagResult {
    QVector<CheckItem> items;
    bool hasFailure = false;
    Layer firstFailed = Layer::App;
    QString summary;
    QString suggestion;
};

/** 层的中文名，用于展示与报告 */
QString layerLabel(Layer layer);

/** 解析 netdiag.ps1 输出的 JSON 快照；结构不合法时返回带 error 的空快照 */
Snapshot parseSnapshot(const QByteArray &json, QString *error = nullptr);

CheckItem evaluateLink(const Snapshot &snapshot);
CheckItem evaluateRoute(const Snapshot &snapshot);
CheckItem evaluateDns(const Snapshot &snapshot,
                      const QVector<DnsProbe::Result> &results);

/** 系统代理的监听地址；PAC 或地址格式异常时 valid() 为 false */
struct ProxyEndpoint {
    QString host;
    quint16 port = 0;
    bool valid() const { return !host.isEmpty() && port != 0; }
};

/** 解析 WinINET 的 ProxyServer 值（支持 `host:port` 与 `http=...;https=...`） */
ProxyEndpoint parseProxyServer(const QString &server);

CheckItem evaluateProxy(const Snapshot &snapshot, const NetProbe::TcpResult &probe);
CheckItem evaluateTcp(const QVector<DnsProbe::Result> &results,
                      const NetProbe::TcpResult &probe);
CheckItem evaluateHttp(const NetProbe::HttpResult &probe);

/** 按 L1→L7 顺序汇总，报告首个失败层；失败层之后的检查项标记为 Skip */
DiagResult diagnose(const Snapshot &snapshot,
                    const QVector<DnsProbe::Result> &dnsResults,
                    const NetProbe::Outcome &probe = NetProbe::Outcome());

} // namespace NetDiag

#endif // NETDIAG_H
