#include "linkprobe.h"
#include "hostinfo.h"

#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

Q_LOGGING_CATEGORY(lcLink, "flowgrid.link")

namespace fg {
namespace {

// ping 输出里的往返时间：「time=1.53 ms」。有的版本写「time<1ms」（亚毫秒），
// 那档直接当成 0.5ms 记——比丢掉样本强。
const QRegularExpression &rttRe()
{
    static const QRegularExpression re(QStringLiteral("time[=<]([0-9.]+)\\s*ms"));
    return re;
}

// 丢包的标志行：「Request timeout for icmp_seq 3」。
const QRegularExpression &timeoutRe()
{
    static const QRegularExpression re(QStringLiteral("timeout"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

LinkProbe *LinkProbe::instance()
{
    static LinkProbe p;
    return &p;
}

LinkProbe::LinkProbe(QObject *parent) : QObject(parent)
{
    target_ = hostinfo::defaultGateway();
    if (target_.isEmpty()) {
        // 没有默认路由就没有可测的链路目标。这种机器上界面应当显示「无数据」，
        // 而不是退而 ping 公网地址——那是别的机器的延迟，与本应用无关。
        qCInfo(lcLink) << "没有默认路由，链路延迟探测不可用";
        return;
    }

    retry_ = new QTimer(this);
    retry_->setSingleShot(true);
    retry_->setInterval(10000);
    connect(retry_, &QTimer::timeout, this, &LinkProbe::start);

    start();
}

LinkProbe::~LinkProbe()
{
    stop();
}

void LinkProbe::start()
{
    if (target_.isEmpty()) {
        return;
    }
    stop();

    proc_ = new QProcess(this);
    proc_->setProgram(QStringLiteral("ping"));
    // -n 不做反向解析（否则每次都要等 DNS），-i 2 两秒一次与采样周期一致，
    // -W 1 一秒超时。LC_ALL=C 是为了让输出恒为英文格式——解析的是
    // "time=1.53 ms" 里的数字，输出语言一变就抓不到了。
    proc_->setArguments({QStringLiteral("-n"), QStringLiteral("-i"), QStringLiteral("2"),
                          QStringLiteral("-W"), QStringLiteral("1"), target_});
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc_->setProcessEnvironment(env);

    connect(proc_, &QProcess::readyReadStandardOutput, this, &LinkProbe::onReadyRead);
    connect(proc_, &QProcess::errorOccurred, this, &LinkProbe::onProcessError);
    connect(proc_, &QProcess::finished, this, &LinkProbe::onProcessFinished);

    proc_->start();
    if (!proc_->waitForStarted(2000)) {
        qCWarning(lcLink) << "ping 启动失败，链路延迟不可用：" << proc_->errorString();
        available_ = false;
        if (retry_ != nullptr) {
            retry_->start();
        }
        return;
    }
    available_ = true;
    qCInfo(lcLink) << "链路延迟探测已启动，目标" << target_;
}

void LinkProbe::stop()
{
    if (proc_ != nullptr) {
        proc_->disconnect(this);
        if (proc_->state() != QProcess::NotRunning) {
            proc_->kill();
            proc_->waitForFinished(1000);
        }
        proc_->deleteLater();
        proc_ = nullptr;
    }
    available_ = false;
}

void LinkProbe::onReadyRead()
{
    if (proc_ == nullptr) {
        return;
    }
    pending_ += QString::fromUtf8(proc_->readAllStandardOutput());
    while (true) {
        const int nl = pending_.indexOf(QLatin1Char('\n'));
        if (nl < 0) {
            break;
        }
        const QString line = pending_.left(nl);
        pending_.remove(0, nl + 1);

        const auto m = rttRe().match(line);
        if (m.hasMatch()) {
            bool ok = false;
            const double ms = m.captured(1).toDouble(&ok);
            if (ok) {
                ++received_;
                lastRttUs_ = static_cast<qint64>(qRound(ms * 1000.0));
                continue;
            }
        }
        if (timeoutRe().match(line).hasMatch()) {
            ++lost_;
        }
    }
}

void LinkProbe::onProcessError(QProcess::ProcessError err)
{
    if (err == QProcess::FailedToStart) {
        qCWarning(lcLink) << "找不到 ping，链路延迟探测停摆（界面将显示无数据）";
        available_ = false;
        if (retry_ != nullptr) {
            retry_->start();
        }
    }
}

void LinkProbe::onProcessFinished(int code, QProcess::ExitStatus status)
{
    // ping 正常情况下是长驻的，被杀掉才会结束。走到这里就是断了：
    // 标成不可用，10 秒后重连。
    qCInfo(lcLink) << "ping 进程结束" << code << status << "，10 秒后重试";
    available_ = false;
    if (retry_ != nullptr) {
        retry_->start();
    }
}

double LinkProbe::lossPercent() const
{
    const int total = received_ + lost_;
    if (total <= 0) {
        return -1.0;
    }
    return 100.0 * lost_ / total;
}

} // namespace fg
