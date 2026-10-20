// 本机的真值。
//
// 界面上「当前设备」「设备与连接」「网络延迟」三处，早先摆的是设计稿里的
// 示例设备（deepin 工作站 / Windows 11 Desktop / KDE Plasma 6）与写死的
// 延迟读数（2.4 / 3.1 / 11.6ms）。那些东西在这台机器上一条都不成立——
// 用户看到的是一台自己根本不认识的电脑，一个自己从没测过的延迟。
//
// 这一层只做一件事：把本机**真实存在**的事实取出来。界面拿这些显示，
// 取不到就写「无数据」，不编。
//
// 数据来源都是本机可读的事实，没有一处是猜的：
//   · 主机名      QSysInfo::machineHostName()
//   · 发行版      /etc/os-release 的 PRETTY_NAME
//   · 网卡 / IP   QNetworkInterface（QHostAddress 只认真正的地址）
//   · 默认网关    /proc/net/route 的默认路由那一行
#ifndef FLOWGRID_HOSTINFO_H
#define FLOWGRID_HOSTINFO_H

#include <QString>

namespace fg::hostinfo {

// 主机名。取不到时给「本机」而不是空串——空串会让界面上那一行整条塌掉。
QString hostName();

// 发行版 + 版本，例如「Deepin 25」。读不到 /etc/os-release 时退回
// QSysInfo 的 prettyProductName。
QString osLabel();

// 内核，例如「Linux 6.18.48-amd64」。
QString kernelLabel();

// CPU 架构，例如「x86_64」。
QString archLabel();

// 主网卡名（例如 wlp3s0）。判定依据是**默认路由用的那块网卡**——
// 它才是真正承载对外流量的那块；取不到时退回「在跑且有 IPv4 的第一块」。
// 没有可用网卡时返回空串。
QString primaryIface();

// 主网卡的 IPv4（点分十进制）。没有则空串。
QString primaryIPv4();

// 主网卡的 MAC。没有则空串。
QString primaryMac();

// 默认网关 IP。没有默认路由时空串。
QString defaultGateway();

// 主屏分辨率，例如「1920×1080」。
QString screenLabel();

} // namespace fg::hostinfo

#endif // FLOWGRID_HOSTINFO_H
