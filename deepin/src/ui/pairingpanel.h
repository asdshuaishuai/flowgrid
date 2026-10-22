// 连接设备浮板（对齐 mac 端 PairingView.swift）。
//
// 布局与 mac 同构，三块卡片 + 两条横幅：
//
//   横幅（在两台设备上使用相同的配对码即可建立 DirectLink 链路）
//   卡片一「附近的设备」   —— 发现到的对端，一行一个，带「连接」按钮
//   卡片二「连接到对端」   —— 手填地址 + 配对码，连接 / 断开
//   卡片三「本机配对码」   —— 自己的码，给对端输入
//   横幅（局域网自动发现；跨网段手填地址）
//   页脚只有一个「关闭」
//
// 之前这一屏是照 design/unified/screens/pairing.html 做的三步向导
// （发现 → 配对 → 完成 + 手动添加折叠区）。那是设计稿的原型交互，
// mac 端实现时换成了上面这套「附近设备 + 连接到对端 + 本机码」。
// 三端对齐以 mac 的实现为准，本屏按它重排。
//
// 「附近的设备」的数据源是 LanIdDiscovery（LAN 私有链发现，§3.1），
// 不是假数据：列表里出现的每一台都是同一网段里真的在跑 FlowGrid 的机器。
#ifndef FLOWGRID_PAIRINGPANEL_H
#define FLOWGRID_PAIRINGPANEL_H

#include "ui/panel.h"

class QLineEdit;
class QLabel;
class QVBoxLayout;

namespace fg {

class Badge;

class PairingPanel : public Panel {
    Q_OBJECT
public:
    // 配对码位数。设计稿 .paircode 是四个数字槽，mac 的 FGPairCode 也是四位。
    static constexpr int kPairCodeDigits = 4;

    explicit PairingPanel(QWidget *parent = nullptr);

Q_SIGNALS:
    // 向某台发现的设备发起连接。传输层没落地前，main.cpp 那边会明确提示
    // 「尚未接通」，不会静悄悄地什么都不发生。
    void pairRequested(const QString &deviceName);

private:
    QWidget *buildNearbyCard();
    QWidget *buildConnectCard();
    QWidget *buildLocalCodeCard();
    QWidget *buildFooter();

    // 卡片一里的对端列表（空态文案 + 真正的对端行）。
    QWidget *buildPeerList();

    // 一个带标题的标签 + 下面一个输入框（mac 的 labeledField）。
    QWidget *buildLabeledField(const QString &label, QLineEdit *edit);

    // 卡片底：surface2 底 + line2 描边 + 圆角（mac 的 FGSection 同款）。
    QWidget *makeCard();

    void rebuildPeers();

    // 把一组配对码填进四个数字槽（首屏与「刷新」都走它）。
    void setPairCode(const QString &code);

    QVBoxLayout *peerLayout_ = nullptr;
    QLabel *peerEmptyLabel_ = nullptr;
    QLabel *peerStatusLabel_ = nullptr;
    QLineEdit *addressEdit_ = nullptr;
    QLineEdit *codeEdit_ = nullptr;
    QLabel *connectStatusLabel_ = nullptr;
    Badge *nearbyBadge_ = nullptr;
    QList<QLabel *> codeSlots_;
    QString pairCode_;
};

} // namespace fg

#endif // FLOWGRID_PAIRINGPANEL_H
