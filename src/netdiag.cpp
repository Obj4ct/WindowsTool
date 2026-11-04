/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "netdiag.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

QString jsonString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

int jsonInt(const QJsonObject &object, const QString &key, int fallback = 0)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt() : fallback;
}

bool jsonBool(const QJsonObject &object, const QString &key, bool fallback = false)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        if (item.isString())
            list.append(item.toString());
    }
    return list;
}

/** 已连接的网卡（Get-NetAdapter.Status == Up）且拿到了 IPv4 */
bool isOnlineAdapter(const NetDiag::AdapterInfo &adapter)
{
    return adapter.status.compare(QLatin1String("Up"), Qt::CaseInsensitive) == 0
        && !adapter.ipv4.isEmpty();
}

const NetDiag::CheckItem *findItem(const QVector<NetDiag::CheckItem> &items,
                                   NetDiag::Layer layer)
{
    for (const auto &item : items) {
        if (item.layer == layer)
            return &item;
    }
    return nullptr;
}

} // namespace

namespace NetDiag {

QString layerLabel(Layer layer)
{
    switch (layer) {
    case Layer::Link:  return QStringLiteral("链路层");
    case Layer::Route: return QStringLiteral("路由层");
    case Layer::Dns:   return QStringLiteral("DNS 解析");
    case Layer::Tcp:   return QStringLiteral("TCP 连接");
    case Layer::Http:  return QStringLiteral("HTTPS 请求");
    case Layer::Proxy: return QStringLiteral("系统代理");
    case Layer::App:   return QStringLiteral("浏览器访问");
    }
    return QStringLiteral("未知层");
}

Snapshot parseSnapshot(const QByteArray &json, QString *error)
{
    Snapshot snapshot;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        snapshot.error = QStringLiteral("诊断快照 JSON 解析失败：%1").arg(parseError.errorString());
        if (error)
            *error = snapshot.error;
        return snapshot;
    }
    if (!document.isObject()) {
        snapshot.error = QStringLiteral("诊断快照不是 JSON 对象");
        if (error)
            *error = snapshot.error;
        return snapshot;
    }

    const QJsonObject root = document.object();
    const QJsonValue adaptersValue = root.value(QStringLiteral("adapters"));
    if (!adaptersValue.isArray()) {
        snapshot.error = QStringLiteral("诊断快照缺少 adapters 数组");
        if (error)
            *error = snapshot.error;
        return snapshot;
    }

    for (const QJsonValue &value : adaptersValue.toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        AdapterInfo adapter;
        adapter.name = jsonString(object, QStringLiteral("name"));
        adapter.description = jsonString(object, QStringLiteral("description"));
        adapter.status = jsonString(object, QStringLiteral("status"));
        adapter.ipv4 = jsonString(object, QStringLiteral("ipv4"));
        adapter.gateway = jsonString(object, QStringLiteral("gateway"));
        adapter.dns = jsonStringList(object.value(QStringLiteral("dns")));
        adapter.virtualAdapter = jsonBool(object, QStringLiteral("virtual"));
        adapter.defaultRoute = jsonBool(object, QStringLiteral("defaultRoute"));
        adapter.interfaceIndex = jsonInt(object, QStringLiteral("ifIndex"));
        snapshot.adapters.append(adapter);
    }

    snapshot.defaultAdapter = jsonString(root, QStringLiteral("defaultAdapter"));

    const QJsonObject proxy = root.value(QStringLiteral("proxy")).toObject();
    snapshot.proxy.enabled = jsonBool(proxy, QStringLiteral("enabled"));
    snapshot.proxy.server = jsonString(proxy, QStringLiteral("server"));
    snapshot.proxy.pacUrl = jsonString(proxy, QStringLiteral("pac"));

    return snapshot;
}

CheckItem evaluateLink(const Snapshot &snapshot)
{
    CheckItem item;
    item.layer = Layer::Link;
    item.name = QStringLiteral("网卡与 IP");

    if (!snapshot.error.isEmpty()) {
        item.status = Status::Skip;
        item.detail = snapshot.error;
        return item;
    }

    QStringList online;
    QStringList noIp;
    for (const AdapterInfo &adapter : snapshot.adapters) {
        const bool up = adapter.status.compare(QLatin1String("Up"), Qt::CaseInsensitive) == 0;
        if (!up)
            continue;
        if (adapter.ipv4.isEmpty())
            noIp.append(adapter.name);
        else
            online.append(adapter.name);
    }

    if (online.isEmpty() && noIp.isEmpty()) {
        item.status = Status::Fail;
        item.detail = QStringLiteral("没有处于连接状态的网卡");
        return item;
    }
    if (online.isEmpty()) {
        item.status = Status::Fail;
        item.detail = QStringLiteral("网卡已连接但未获取到 IPv4 地址（%1）")
                          .arg(noIp.join(QStringLiteral("、")));
        return item;
    }

    item.status = Status::Pass;
    item.detail = QStringLiteral("%1 个网卡在线（%2）")
                      .arg(online.size())
                      .arg(online.join(QStringLiteral("、")));

    if (!snapshot.defaultAdapter.isEmpty()) {
        for (const AdapterInfo &adapter : snapshot.adapters) {
            if (adapter.name != snapshot.defaultAdapter)
                continue;
            if (adapter.virtualAdapter) {
                item.status = Status::Warn;
                item.detail += QStringLiteral("；流量实际经由虚拟/代理网卡「%1」")
                                   .arg(adapter.name);
            }
            break;
        }
    }
    return item;
}

CheckItem evaluateRoute(const Snapshot &snapshot)
{
    CheckItem item;
    item.layer = Layer::Route;
    item.name = QStringLiteral("默认路由");

    if (!snapshot.error.isEmpty()) {
        item.status = Status::Skip;
        item.detail = snapshot.error;
        return item;
    }

    if (snapshot.defaultAdapter.isEmpty()) {
        item.status = Status::Fail;
        item.detail = QStringLiteral("未找到默认路由（0.0.0.0/0）");
        return item;
    }

    item.status = Status::Pass;
    item.detail = QStringLiteral("默认路由经由「%1」").arg(snapshot.defaultAdapter);
    return item;
}

CheckItem evaluateDns(const Snapshot &snapshot,
                      const QVector<DnsProbe::Result> &results)
{
    CheckItem item;
    item.layer = Layer::Dns;
    item.name = QStringLiteral("域名解析");

    if (!snapshot.error.isEmpty()) {
        item.status = Status::Skip;
        item.detail = snapshot.error;
        return item;
    }

    QStringList servers;
    for (const AdapterInfo &adapter : snapshot.adapters) {
        for (const QString &server : adapter.dns) {
            if (!servers.contains(server))
                servers.append(server);
        }
    }

    if (servers.isEmpty()) {
        item.status = Status::Fail;
        item.detail = QStringLiteral("所有在线网卡都没有配置 DNS 服务器");
        return item;
    }
    if (results.isEmpty()) {
        item.status = Status::Skip;
        item.detail = QStringLiteral("未执行 DNS 探测");
        return item;
    }

    int bestMs = -1;
    QString bestServer;
    QStringList failed;
    for (const DnsProbe::Result &result : results) {
        if (result.status == DnsProbe::Status::Ok) {
            if (bestMs < 0 || (result.elapsedMs >= 0 && result.elapsedMs < bestMs)) {
                bestMs = result.elapsedMs;
                bestServer = result.server;
            }
        } else {
            failed.append(result.server);
        }
    }

    if (bestMs >= 0) {
        item.status = Status::Pass;
        item.elapsedMs = bestMs;
        item.detail = QStringLiteral("最快响应 %1 ms（%2）").arg(bestMs).arg(bestServer);
        if (!failed.isEmpty()) {
            item.detail += QStringLiteral("；%1 个 DNS 未响应").arg(failed.size());
            item.status = Status::Warn;
        }
        return item;
    }

    item.status = Status::Fail;
    item.detail = QStringLiteral("%1 个 DNS 服务器均解析失败").arg(failed.size());
    return item;
}

DiagResult diagnose(const Snapshot &snapshot,
                    const QVector<DnsProbe::Result> &dnsResults)
{
    DiagResult result;
    result.items.append(evaluateLink(snapshot));
    result.items.append(evaluateRoute(snapshot));
    result.items.append(evaluateDns(snapshot, dnsResults));

    const struct { Layer layer; const char *name; } pending[] = {
        { Layer::Tcp,   "目标端口连接" },
        { Layer::Http,  "HTTPS 响应" },
        { Layer::Proxy, "系统代理" },
        { Layer::App,   "浏览器访问" }
    };
    for (const auto &entry : pending) {
        CheckItem item;
        item.layer = entry.layer;
        item.name = QString::fromUtf8(entry.name);
        item.status = Status::Skip;
        item.detail = QStringLiteral("尚未检测");
        result.items.append(item);
    }

    bool blocked = false;
    for (CheckItem &item : result.items) {
        if (blocked) {
            if (item.status != Status::Skip)
                item.status = Status::Skip;
            item.detail = QStringLiteral("前一层未通过，已跳过");
            continue;
        }
        if (item.status == Status::Fail) {
            blocked = true;
            if (!result.hasFailure) {
                result.hasFailure = true;
                result.firstFailed = item.layer;
            }
        }
    }

    if (!result.hasFailure) {
        for (const auto &item : result.items) {
            if (item.status == Status::Skip && item.layer >= Layer::Tcp)
                continue;
            if (item.status == Status::Warn) {
                result.summary = item.detail;
                result.suggestion = QStringLiteral("网络可用，但存在需要留意的配置项。");
                return result;
            }
        }
        result.summary = QStringLiteral("链路、路由与 DNS 检查均通过。");
        result.suggestion = QStringLiteral("网络各层检查通过，无需处理。");
        return result;
    }

    const CheckItem *failedItem = findItem(result.items, result.firstFailed);
    if (failedItem) {
        result.summary = QStringLiteral("%1未通过：%2")
                             .arg(layerLabel(result.firstFailed), failedItem->detail);
    }

    switch (result.firstFailed) {
    case Layer::Link:
        result.suggestion = QStringLiteral("检查网线或 Wi-Fi 是否连接，确认网卡已获取 IP 地址。");
        break;
    case Layer::Route:
        result.suggestion = QStringLiteral("未找到默认路由：检查是否启用了 VPN / 代理接管流量，或手动添加默认路由。");
        break;
    case Layer::Dns:
        result.suggestion = QStringLiteral("尝试更换 DNS（如 223.5.5.5）或刷新 DNS 缓存。");
        break;
    default:
        result.suggestion = QStringLiteral("按诊断结论逐层排查。");
        break;
    }
    return result;
}

} // namespace NetDiag
