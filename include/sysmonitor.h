#ifndef SYSMONITOR_H
#define SYSMONITOR_H

#include <QObject>
#include <QString>

class QTimer;

/**
 * 轻量实时监控：每秒采样一次 CPU / 内存 / 系统盘 / 网络吞吐。
 *
 * 全部走 Win32 API（GetSystemTimes / GlobalMemoryStatusEx /
 * GetDiskFreeSpaceEx / GetIfTable2），不起子进程、不依赖 PowerShell，
 * 对系统开销很小。
 */
class SysMonitor : public QObject
{
    Q_OBJECT

public:
    explicit SysMonitor(QObject *parent = nullptr);

    /** 开始/恢复每秒采样（重复调用无副作用） */
    void start();

    /** 暂停采样（卡片仍然保留最后一次的数据） */
    void stop();

    bool isRunning() const;

signals:
    /**
     * cpuPercent      0~100，首次采样为 -1（无法算出瞬时值）
     * memUsedMb       已用物理内存（MB）
     * memTotalMb      物理内存总量（MB）
     * diskPercent     系统盘已用占比 0~100
     * diskName        系统盘盘符，如 "C:"
     * netInBps/netOutBps 每秒接收/发送字节数
     */
    void updated(double cpuPercent, double memUsedMb, double memTotalMb,
                 double diskPercent, const QString &diskName,
                 double netInBps, double netOutBps);

private:
    void sample();

    QTimer *m_timer = nullptr;

    quint64 m_prevIdle = 0;
    quint64 m_prevKernel = 0;
    quint64 m_prevUser = 0;
    quint64 m_prevNetIn = 0;
    quint64 m_prevNetOut = 0;
    qint64 m_prevTickMs = 0;

    QString m_diskName;
    bool m_hasPrev = false;
};

#endif // SYSMONITOR_H
