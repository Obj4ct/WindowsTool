#include "sysmonitor.h"

#include <QDateTime>
#include <QTimer>

#include <windows.h>
#include <iphlpapi.h>

#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace {

constexpr int kIntervalMs = 1000;

/** 系统盘路径（Windows 目录所在分区的根） */
QString systemDrive()
{
    wchar_t buffer[MAX_PATH] = {};
    if (::GetWindowsDirectoryW(buffer, MAX_PATH) == 0)
        return QStringLiteral("C:");
    const QString windowsDir = QString::fromWCharArray(buffer);
    return windowsDir.left(2).toUpper();
}

/** 汇总所有"已连接且非回环"接口的收发计数（字节，32 位计数器） */
void readNetworkTotals(quint64 *inBytes, quint64 *outBytes)
{
    *inBytes = 0;
    *outBytes = 0;

    DWORD size = 0;
    if (::GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0)
        return;

    std::vector<unsigned char> buffer(size);
    PMIB_IFTABLE table = reinterpret_cast<PMIB_IFTABLE>(buffer.data());
    if (::GetIfTable(table, &size, FALSE) != NO_ERROR)
        return;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW &row = table->table[i];
        if (row.dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL)
            continue;
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        *inBytes += row.dwInOctets;
        *outBytes += row.dwOutOctets;
    }
}

} // namespace

SysMonitor::SysMonitor(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_diskName(systemDrive())
{
    m_timer->setInterval(kIntervalMs);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &SysMonitor::sample);
}

void SysMonitor::start()
{
    if (m_timer->isActive())
        return;
    m_hasPrev = false; // 重新开始计时，让 CPU 百分比以新基线计算
    m_timer->start();
    sample();
}

void SysMonitor::stop()
{
    m_timer->stop();
}

bool SysMonitor::isRunning() const
{
    return m_timer->isActive();
}

void SysMonitor::sample()
{
    // ---------------- CPU：两次采样的时间差
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    double cpuPercent = -1.0;

    if (::GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        auto toU64 = [](const FILETIME &ft) -> quint64 {
            ULARGE_INTEGER value;
            value.LowPart = ft.dwLowDateTime;
            value.HighPart = ft.dwHighDateTime;
            return value.QuadPart;
        };

        const quint64 idle = toU64(idleTime);
        const quint64 kernel = toU64(kernelTime); // 包含 idle
        const quint64 user = toU64(userTime);

        if (m_hasPrev) {
            const quint64 idleDelta = idle - m_prevIdle;
            const quint64 totalDelta = (kernel - m_prevKernel) + (user - m_prevUser);
            if (totalDelta > 0) {
                const double busy = static_cast<double>(totalDelta - idleDelta);
                cpuPercent = qBound(0.0, busy * 100.0 / static_cast<double>(totalDelta), 100.0);
            } else {
                cpuPercent = 0.0;
            }
        }

        m_prevIdle = idle;
        m_prevKernel = kernel;
        m_prevUser = user;
    }

    // ---------------- 内存
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    double memUsedMb = 0.0;
    double memTotalMb = 0.0;
    if (::GlobalMemoryStatusEx(&memory)) {
        memTotalMb = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0);
        memUsedMb = memTotalMb - static_cast<double>(memory.ullAvailPhys) / (1024.0 * 1024.0);
    }

    // ---------------- 系统盘
    double diskPercent = 0.0;
    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};
    if (::GetDiskFreeSpaceExW((m_diskName + QLatin1String("\\")).toStdWString().c_str(),
                              &freeBytes, &totalBytes, &totalFreeBytes)
        && totalBytes.QuadPart > 0) {
        const double used = static_cast<double>(totalBytes.QuadPart - totalFreeBytes.QuadPart);
        diskPercent = used * 100.0 / static_cast<double>(totalBytes.QuadPart);
    }

    // ---------------- 网络吞吐
    quint64 netIn = 0;
    quint64 netOut = 0;
    readNetworkTotals(&netIn, &netOut);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    double netInBps = 0.0;
    double netOutBps = 0.0;
    if (m_hasPrev && m_prevTickMs > 0) {
        const double seconds = static_cast<double>(nowMs - m_prevTickMs) / 1000.0;
        if (seconds > 0.2) {
            if (netIn >= m_prevNetIn)
                netInBps = static_cast<double>(netIn - m_prevNetIn) / seconds;
            if (netOut >= m_prevNetOut)
                netOutBps = static_cast<double>(netOut - m_prevNetOut) / seconds;
        }
    }
    m_prevNetIn = netIn;
    m_prevNetOut = netOut;
    m_prevTickMs = nowMs;

    const bool first = !m_hasPrev;
    m_hasPrev = true;

    emit updated(first ? -1.0 : cpuPercent, memUsedMb, memTotalMb,
                 diskPercent, m_diskName, netInBps, netOutBps);
}
