// 本机链路的真实 RTT 探测。
//
// 传输层（L3：DTLS/UDP）还没落地，所以界面上「到对端的延迟」根本无从谈起——
// 早先那一版是拿随机数当延迟造的（每 2 秒摇一个 2.4 / 3.1 / 11.6ms），
// 数字在动，但没有一个是量出来的。
//
// 能真测的是「本机到默认网关的 ICMP 往返」：ping 一次就是一次真实往返，
// 丢包也是真的丢包。用系统的 ping 而不是自己拼 ICMP 包——原始套接字要 root，
// 而发行版的 /bin/ping 带 cap_net_raw，普通用户就能用（实测本机可用）。
//
// 测得的值照旧交给 Go 的 LatencyMonitor 统计（采样窗口、抖动、连续劣化计数
// 都还是协议库算的），口径与协议一致：读数是**半 RTT**（heartbeat.go:
// latencyMs = rttUs / 2000），即单程时延。
//
// 目标只有默认网关，不去 ping 公网地址：那是别的机器的延迟，与本应用无关，
// 还会把流量发到工作区之外。
#ifndef FLOWGRID_LINKPROBE_H
#define FLOWGRID_LINKPROBE_H

#include <QObject>
#include <QProcess>
#include <QString>

class QTimer;

namespace fg {

class LinkProbe : public QObject {
    Q_OBJECT
public:
    static LinkProbe *instance();

    // 探测目标（默认网关的 IP）。取不到时是空串。
    QString target() const { return target_; }

    // 探测是否可用：有目标、且 ping 进程还在跑。
    bool available() const { return available_; }

    // 最近一次真实 RTT（微秒）。-1 表示还没测到——界面据此显示「--」，
    // 不用 0 冒充「延迟为零」。
    qint64 lastRttUs() const { return lastRttUs_; }

    // 累计收到的回包 / 丢掉的回包（按 ping 的输出数）。
    int received() const { return received_; }
    int lost() const { return lost_; }

    // 丢包率（百分比）。一个包都没收到过返回 -1。
    double lossPercent() const;

Q_SIGNALS:
    // 每拿到一个新的真实 RTT 就发一次。
    void measured();

private:
    explicit LinkProbe(QObject *parent = nullptr);
    ~LinkProbe() override;

    void start();
    void stop();
    void onReadyRead();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int code, QProcess::ExitStatus status);

    QString target_;
    QProcess *proc_ = nullptr;
    QTimer *retry_ = nullptr;
    bool available_ = false;
    qint64 lastRttUs_ = -1;
    int received_ = 0;
    int lost_ = 0;
    // 半行缓存：ping 的输出不一定按行断，readLine() 会拿到半截。
    QString pending_;
};

} // namespace fg

#endif // FLOWGRID_LINKPROBE_H
