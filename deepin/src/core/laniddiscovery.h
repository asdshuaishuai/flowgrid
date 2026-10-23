// 局域网智能连接发现（PROTOCOL.md §3.1，deepin 侧）。
//
// 分层跟另外两端一样：
//
//   protocol/（Go）      纯逻辑：Announcer 产出签名通告字节、Listener 解析
//                        收到的通告、TrustChain 判信任。不碰 socket。
//   lanidlink.{h,cpp}    UDP 多播管道：QUdpSocket 收发 + 定时发送/清理。
//   laniddiscovery.{h,cpp}（本文件）把上面两块拼成客户端能直接用的「发现器」：
//                        持久化身份密钥、管好三者的生命周期、把对端整理成
//                        UI 能直接渲染的 Peer 列表。
//
// 为什么要单独一层：Announcer / Listener / TrustChain 都持有 cgo 句柄，
// 且 LanIDLink 在构造时**按引用**持有 Announcer 与 Listener——谁先析构、
// 谁后析构，顺序错了就是野指针。所以这几个对象必须由同一个 owner 一起管，
// 不能散落在各个面板里各自 new。
//
// 身份密钥必须跨进程持久：私钥一换，对端看到的「同一台机器」就变成另一台，
// 信任链随之断裂。所以第一次生成后落盘到 ~/.config/flowgrid/identity.key。
#ifndef FLOWGRID_LANID_DISCOVERY_H
#define FLOWGRID_LANID_DISCOVERY_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace fg {

namespace proto {
class Announcer;
class Identity;
class Listener;
class TrustChain;
class LanIDLink;
} // namespace proto

// 进程级单例：发现是**全局**状态，不属于任何一个面板。
//
// 跟 DeviceManager / Settings / Theme 一样走单例，理由也相同：托盘浮层、
// 连接屏、添加设备屏可能同时要看同一份对端列表，各自建一份 UDP 收包
// socket 只会让同一份通告被收两遍、还会互相抢端口。
class LanIdDiscovery : public QObject {
    Q_OBJECT
public:
    // 发现到的一台对端。字段直接对应 mac 的 DiscoveredPeer，UI 不用再翻译。
    struct Peer {
        QString name;        // 对端自报的名字（一般是主机名）
        QString platform;    // 「macOS」/「Windows」/「Linux」…
        // 平台**字节**（协议 §3.1.2）：键鼠重映射要按「本机平台 → 对端平台」
        // 换算修饰键，只有人读的字符串不够用。
        quint8 platformId = 0;
        QString address;     // 「192.168.1.50:34801」，点「连接」时用
        QString fingerprint; // 32 hex 公钥指纹，信任链的键
        quint16 port = 0;
        bool trusted = false;
    };

    static LanIdDiscovery &instance();

    // 开始发现。已经启动过就直接返回（幂等）。
    //
    // 失败时（比如 udp 端口被人独占、没权限发多播）不会抛异常，
    // 只是 peers() 一直为空——UI 那边自然显示「正在搜索…」。
    void start();

    // 停止发现并释放 socket。再次 start() 可以重新开始。
    void stop();

    bool isRunning() const;

    // 当前发现到的对端（已滤掉本机自己）。
    QList<Peer> peers() const;

    // 本机身份公钥指纹（32 hex）。信任链的键。
    QString selfFingerprint() const;

    // 发现器本身出错时的原因（空串 = 没问题）。UI 用来在列表上方说明白，
    // 而不是让人对着一个永远空的列表猜。
    QString lastError() const;

Q_SIGNALS:
    // 对端列表变了（新增/超时移除）。UI 据此刷新列表。
    void peersChanged();

private:
    LanIdDiscovery();
    ~LanIdDiscovery() override;

    // 读回上次的身份私钥；读不到或读坏了返回空 QByteArray。
    QByteArray loadIdentityKey() const;

    // 把身份私钥落盘（0600）。失败返回 false。
    bool saveIdentityKey(const QByteArray &key) const;

    // 身份/通告器/监听器/信任链/多播链路这一组。
    std::unique_ptr<proto::Identity> identity_;
    std::unique_ptr<proto::Listener> listener_;
    std::unique_ptr<proto::Announcer> announcer_;
    std::unique_ptr<proto::TrustChain> trust_;
    std::unique_ptr<proto::LanIDLink> link_;

    QString lastError_;
    bool running_ = false;
};

} // namespace fg

#endif // FLOWGRID_LANID_DISCOVERY_H
