/*
 * Copyright (c) 2023 素曜
 * SPDX-License-Identifier: MIT
 *
 * This file is part of WindowsTool and is distributed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "dnsprobe.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QUdpSocket>

namespace {

constexpr int HeaderSize = 12;
constexpr quint16 TypeA = 1;
constexpr quint16 ClassIn = 1;

quint16 readU16(const QByteArray &data, int offset)
{
    return static_cast<quint16>((data.at(offset) << 8) | data.at(offset + 1));
}

quint32 readU32(const QByteArray &data, int offset)
{
    return (static_cast<quint32>(data.at(offset)) << 24)
         | (static_cast<quint32>(data.at(offset + 1)) << 16)
         | (static_cast<quint32>(data.at(offset + 2)) << 8)
         | static_cast<quint32>(data.at(offset + 3));
}

/** 跳过一个域名（支持 0xC0 压缩指针），offset 指向下一个字段 */
bool skipName(const QByteArray &data, int &offset)
{
    int guard = 0;
    while (offset < data.size()) {
        const quint8 length = static_cast<quint8>(data.at(offset));
        if (length == 0) {
            ++offset;
            return true;
        }
        if ((length & 0xC0) == 0xC0) {
            offset += 2;
            return offset <= data.size();
        }
        if ((length & 0xC0) != 0)
            return false;
        offset += 1 + length;
        if (++guard > 128)
            return false;
    }
    return false;
}

QString rcodeErrorText(int rcode)
{
    switch (rcode) {
    case 2: return QStringLiteral("SERVFAIL");
    case 3: return QStringLiteral("NXDOMAIN");
    case 5: return QStringLiteral("REFUSED");
    default: return QStringLiteral("RCODE %1").arg(rcode);
    }
}

} // namespace

namespace DnsProbe {

QByteArray buildQuery(const QString &domain, quint16 id)
{
    QByteArray packet;
    packet.reserve(HeaderSize + domain.size() + 6);

    packet.append(static_cast<char>(id >> 8));
    packet.append(static_cast<char>(id & 0xFF));
    packet.append(static_cast<char>(0x01)); // RD=1
    packet.append(static_cast<char>(0x00));
    packet.append(static_cast<char>(0x00)); // QDCOUNT=1
    packet.append(static_cast<char>(0x01));
    packet.append(static_cast<char>(0x00)); // ANCOUNT=0
    packet.append(static_cast<char>(0x00));
    packet.append(static_cast<char>(0x00)); // NSCOUNT=0
    packet.append(static_cast<char>(0x00));
    packet.append(static_cast<char>(0x00)); // ARCOUNT=0
    packet.append(static_cast<char>(0x00));

    const QStringList labels = domain.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &label : labels) {
        if (label.size() > 63) {
            packet.clear();
            return packet;
        }
        packet.append(static_cast<char>(label.size()));
        packet.append(label.toLatin1());
    }
    packet.append(static_cast<char>(0x00));
    packet.append(static_cast<char>(0x00)); // QTYPE=A
    packet.append(static_cast<char>(0x01));
    packet.append(static_cast<char>(0x00)); // QCLASS=IN
    packet.append(static_cast<char>(0x01));
    return packet;
}

bool parseResponse(const QByteArray &packet, quint16 expectedId,
                   Result *result, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (!result)
        return fail(QStringLiteral("结果指针为空"));
    if (packet.size() < HeaderSize)
        return fail(QStringLiteral("响应报文过短"));

    const quint16 id = readU16(packet, 0);
    if (id != expectedId)
        return fail(QStringLiteral("响应 ID 不匹配"));

    const quint16 flags = readU16(packet, 2);
    const int rcode = flags & 0x000F;
    const int questions = static_cast<int>(readU16(packet, 4));
    const int answers = static_cast<int>(readU16(packet, 6));

    result->rcode = rcode;
    result->addresses.clear();

    switch (rcode) {
    case 0:
        break;
    case 2:
        result->status = Status::ServFail;
        return true;
    case 3:
        result->status = Status::NxDomain;
        return true;
    case 5:
        result->status = Status::Refused;
        return true;
    default:
        result->status = Status::Error;
        result->error = rcodeErrorText(rcode);
        return true;
    }

    int offset = HeaderSize;
    for (int i = 0; i < questions; ++i) {
        if (!skipName(packet, offset) || offset + 4 > packet.size())
            return fail(QStringLiteral("问题区格式错误"));
        offset += 4;
    }

    for (int i = 0; i < answers; ++i) {
        if (!skipName(packet, offset) || offset + 10 > packet.size())
            return fail(QStringLiteral("答案区格式错误"));

        const quint16 type = readU16(packet, offset);
        const quint16 klass = readU16(packet, offset + 2);
        const quint32 ttl = readU32(packet, offset + 4); // 保留语义：A 记录缓存时间
        Q_UNUSED(ttl);
        const quint16 rdLength = readU16(packet, offset + 8);
        offset += 10;

        if (offset + rdLength > packet.size())
            return fail(QStringLiteral("答案数据越界"));

        if (type == TypeA && klass == ClassIn && rdLength == 4) {
            result->addresses.append(QStringLiteral("%1.%2.%3.%4")
                .arg(static_cast<int>(packet.at(offset)))
                .arg(static_cast<int>(packet.at(offset + 1)))
                .arg(static_cast<int>(packet.at(offset + 2)))
                .arg(static_cast<int>(packet.at(offset + 3))));
        }
        offset += rdLength;
    }

    result->status = result->addresses.isEmpty() ? Status::NoAnswer : Status::Ok;
    return true;
}

Result query(const QString &server, const QString &domain,
             int timeoutMs, quint16 port)
{
    Result result;
    result.server = server;

    QHostAddress address(server);
    if (address.isNull() || address.protocol() != QAbstractSocket::IPv4Protocol) {
        result.status = Status::Error;
        result.error = QStringLiteral("DNS 服务器地址无效");
        return result;
    }

    QUdpSocket socket;
    if (!socket.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        result.status = Status::Error;
        result.error = QStringLiteral("无法创建本地 UDP 套接字");
        return result;
    }

    const quint16 id = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
    const QByteArray payload = buildQuery(domain, id);
    if (payload.isEmpty()) {
        result.status = Status::Error;
        result.error = QStringLiteral("域名格式无效");
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    socket.writeDatagram(payload, address, port);

    while (timer.elapsed() < timeoutMs) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        if (!socket.waitForReadyRead(remaining))
            break;

        char buffer[512];
        const qint64 size = socket.readDatagram(buffer, sizeof(buffer));
        if (size < HeaderSize)
            continue;

        QString error;
        QByteArray packet = QByteArray::fromRawData(buffer, static_cast<int>(size));
        if (parseResponse(packet, id, &result, &error)) {
            result.elapsedMs = static_cast<int>(timer.elapsed());
            return result;
        }
    }

    result.elapsedMs = static_cast<int>(timer.elapsed());
    result.status = Status::NoResponse;
    return result;
}

} // namespace DnsProbe
