#include "hostinfo.h"

#include <QAbstractSocket>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRect>
#include <QRegularExpression>
#include <QScreen>
#include <QSysInfo>

namespace fg::hostinfo {
namespace {

// /etc/os-release 的 PRETTY_NAME。发行版自己的说法最准（deepin 写的是
// 「Deepin 25」，而 QSysInfo::prettyProductName() 只会给个干巴巴的
// 「Debian」或干脆空着）。
QString readPrettyName()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList lines =
        QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!line.startsWith(QLatin1String("PRETTY_NAME="))) {
            continue;
        }
        QString v = line.mid(12).trimmed();
        // 值通常是带引号的，去掉。
        if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
            v = v.mid(1, v.size() - 2);
        }
        return v.trimmed();
    }
    return {};
}

// /proc/net/route 的一行：网卡名 + 网关（十六进制、小端）。
//
// 十六进制是内核**内存里那四个字节**的直接转储，也就是小端：
// 011FA8C0 的字节序是 C0 A8 1F 01 → 192.168.31.1，与 `ip route` 一致。
// 别按大端去解——那会得到 1.31.168.192，然后所有探测都打到一个不存在的地址上。
QString hexToIp(const QString &hex)
{
    bool ok = false;
    const quint32 v = hex.toUInt(&ok, 16);
    if (!ok) {
        return {};
    }
    return QStringLiteral("%1.%2.%3.%4")
        .arg(v & 0xFFu)
        .arg((v >> 8) & 0xFFu)
        .arg((v >> 16) & 0xFFu)
        .arg((v >> 24) & 0xFFu);
}

// 默认路由：目标为 0.0.0.0 的那一行。
// 返回网卡名与网关；没有默认路由时网关为空串。
//
// 注意：/proc 下的文件 size() 报 0，QFile::atEnd() 一上来就是 true——
// 按行读（readLine + atEnd 判循环）会只读到表头就结束，于是「没有默认路由」。
// 所以整块读出来再按行切。
QStringList readDefaultRoute()
{
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    // 第一行是表头。
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList parts =
            lines.at(i).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        // Iface Destination Gateway Flags RefCnt Use Metric Mask ...
        if (parts.size() < 3) {
            continue;
        }
        if (parts.at(1) != QLatin1String("00000000")) {
            continue;   // 不是默认路由
        }
        const QString gw = hexToIp(parts.at(2));
        if (gw.isEmpty() || gw == QLatin1String("0.0.0.0")) {
            continue;
        }
        return {parts.at(0), gw};
    }
    return {};
}

// 按名字找网卡对象。
QNetworkInterface interfaceByName(const QString &name)
{
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &i : all) {
        if (i.name() == name) {
            return i;
        }
    }
    return {};
}

} // namespace

QString hostName()
{
    const QString h = QSysInfo::machineHostName().trimmed();
    return h.isEmpty() ? QStringLiteral("本机") : h;
}

QString osLabel()
{
    const QString pretty = readPrettyName();
    if (!pretty.isEmpty()) {
        return pretty;
    }
    const QString p = QSysInfo::prettyProductName();
    if (!p.isEmpty()) {
        return p;
    }
    return QStringLiteral("未知系统");
}

QString kernelLabel()
{
    const QString type = QSysInfo::kernelType();
    const QString ver = QSysInfo::kernelVersion();
    if (type.isEmpty() && ver.isEmpty()) {
        return {};
    }
    if (ver.isEmpty()) {
        return type;
    }
    return type + QLatin1Char(' ') + ver;
}

QString archLabel()
{
    return QSysInfo::currentCpuArchitecture();
}

QString primaryIface()
{
    const QStringList route = readDefaultRoute();
    if (!route.isEmpty()) {
        return route.at(0);
    }
    // 没有默认路由：退到「在跑、有 IPv4、非回环」的第一块网卡。
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &i : all) {
        if (i.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        if (!i.flags().testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                return i.name();
            }
        }
    }
    return {};
}

QString primaryIPv4()
{
    const QString name = primaryIface();
    if (name.isEmpty()) {
        return {};
    }
    const QNetworkInterface i = interfaceByName(name);
    if (!i.isValid()) {
        return {};
    }
    for (const QNetworkAddressEntry &e : i.addressEntries()) {
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            return e.ip().toString();
        }
    }
    return {};
}

QString primaryMac()
{
    const QString name = primaryIface();
    if (name.isEmpty()) {
        return {};
    }
    const QNetworkInterface i = interfaceByName(name);
    if (!i.isValid()) {
        return {};
    }
    return i.hardwareAddress();
}

QString defaultGateway()
{
    const QStringList route = readDefaultRoute();
    return route.isEmpty() ? QString() : route.at(1);
}

QString screenLabel()
{
    QScreen *s = QGuiApplication::primaryScreen();
    if (s == nullptr) {
        return {};
    }
    const QRect g = s->geometry();
    return QStringLiteral("%1×%2").arg(g.width()).arg(g.height());
}

} // namespace fg::hostinfo
