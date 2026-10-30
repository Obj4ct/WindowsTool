#ifndef NETCHECK_H
#define NETCHECK_H

#include <QString>
#include <QStringList>

/**
 * 网络检测结果判断逻辑。
 *
 * 独立成头文件，便于单元测试，也避免主程序里散落多处字符串匹配。
 */
namespace NetCheck {

/**
 * 判断 nslookup 的输出是否代表"域名解析失败"。
 *
 * 注意：不能匹配 "服务器:  UnKnown" —— 该行在 DNS 完全正常时也会出现
 * （nslookup 无法反向解析 DNS 服务器自身的主机名时会打印它）。
 * 原实现正是因此误判为"DNS 故障"并触发无谓的修复。
 */
inline bool isDnsLookupFailed(const QString &output)
{
    static const QStringList failMarkers = {
        QStringLiteral("Non-existent domain"),     // DNS 递归解析失败
        QStringLiteral("can't find"),               // can't find baidu.com
        QStringLiteral("No response from server"),  // DNS 服务器无响应
        QStringLiteral("timed out"),                // 查询超时
        QStringLiteral("SERVFAIL"),
        QStringLiteral("找不到主机"),
        QStringLiteral("没有找到主机")
    };
    for (const QString &marker : failMarkers) {
        if (output.contains(marker, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/** 判断 ping 输出是否为"请求超时" */
inline bool isPingTimeout(const QString &output)
{
    return output.contains(QStringLiteral("请求超时"))
        || output.contains(QStringLiteral("Request timed out"), Qt::CaseInsensitive);
}

/** 判断 ping 输出是否为"找不到主机"（DNS 解析失败） */
inline bool isPingHostUnknown(const QString &output)
{
    return output.contains(QStringLiteral("找不到主机"))
        || output.contains(QStringLiteral("could not find host"), Qt::CaseInsensitive);
}

} // namespace NetCheck

#endif // NETCHECK_H
