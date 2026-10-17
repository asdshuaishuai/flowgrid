#include "traypopup.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "panel.h"
#include "widgets.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace fg {
namespace {

QFont uiFont(int px, int weight = QFont::Normal)
{
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

// 必须带 alpha：柔和底色（ok-soft / accent-soft）就是靠 12%~18% 透明度
// 叠出来的。用 HexRgb 丢 alpha 会把它们渲染成实色，整个设计稿的浅底全废。
QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

namespace {

// 快速切换按钮：设计稿里它就是一个「图标方块 + 快捷键」的可点块。
// 做成类而不是裸 QWidget，是因为 QWidget::mouseReleaseEvent 是 protected，
// 没法用 connect 直接挂——与其用事件过滤器绕，不如把点击语义收进类型里。
class QuickButton : public QWidget {
    Q_OBJECT
public:
    QuickButton(const QString &abbrev, int hotkey, const StatusColor &sc, QWidget *parent)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        auto *vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);

        auto *icon = new QLabel(abbrev, this);
        icon->setFixedSize(tokens::kSwitchBtnSize, tokens::kSwitchBtnSize);
        icon->setAlignment(Qt::AlignCenter);
        icon->setFont(uiFont(tokens::kFont12, QFont::Bold));
        icon->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                 .arg(css(sc.soft), css(sc.text))
                                 .arg(tokens::kRadiusCtl));

        auto *kbd = new QLabel(QStringLiteral("Ctrl+%1").arg(hotkey), this);
        kbd->setFont(uiFont(tokens::kFont11));
        kbd->setAlignment(Qt::AlignCenter);
        kbd->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));

        vl->addWidget(icon);
        vl->addWidget(kbd);
    }

Q_SIGNALS:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (rect().contains(e->position().toPoint())) {
            Q_EMIT clicked();
        }
        QWidget::mouseReleaseEvent(e);
    }
};

} // namespace

TrayPopup::TrayPopup(QWidget *parent) : QWidget(parent), manager_(DeviceManager::instance())
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Popup | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFixedWidth(tokens::kTrayWidth);

    // 设计稿：托盘浮层阴影 0 16px 32px rgba(15,23,42,.16)
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(tokens::kShadowTrayR);
    shadow->setOffset(0, tokens::kShadowTrayY);
    shadow->setColor(Theme::instance().palette().shadowTray);
    setGraphicsEffect(shadow);

    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);

    root_->addWidget(buildHead());
    root_->addWidget(buildSeparator());
    root_->addWidget(buildCurrentDevice());
    root_->addWidget(buildSeparator());
    root_->addWidget(buildQuickSwitch());
    root_->addWidget(buildSeparator());

    // ── 连接 ──
    {
        auto *lat = new LatencyChip();
        trayLatency_ = lat;
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("devices"), tr("设备与连接"),
                        new Badge(QStringLiteral("—"), Badge::Variant::Neutral),
                        QStringLiteral("connections")),
            buildNavRow(QStringLiteral("activity"), tr("网络延迟"), lat,
                        QStringLiteral("latency")),
            buildNavRow(QStringLiteral("plus"), tr("添加设备"),
                        new Badge(tr("向导"), Badge::Variant::Neutral),
                        QStringLiteral("pairing")),
        };
        root_->addWidget(buildGroup(tr("连接"), rows));
    }
    root_->addWidget(buildSeparator());

    // ── 数据与同步 ──
    {
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("clipboard"), tr("剪贴板"),
                        new Badge(QStringLiteral("—"), Badge::Variant::Neutral),
                        QStringLiteral("clipboard")),
            buildNavRow(QStringLiteral("file"), tr("文件传输"),
                        new Badge(QStringLiteral("—"), Badge::Variant::Neutral),
                        QStringLiteral("files")),
        };
        root_->addWidget(buildGroup(tr("数据与同步"), rows));
    }
    root_->addWidget(buildSeparator());

    // ── 快捷开关 ──
    //
    // 设计稿 tray.html 这四行就是「设置」屏同名的四个开关。早先这里是写死的
    // buildToggleRow(label, 常量)，点了什么都不发生、也不落 DConfig——两处开关
    // 各说各话。现在统一读写 Settings，配置只留一份。
    {
        auto &cfg = Settings::instance();
        QList<QWidget *> rows{
            buildToggleRow(tr("键位映射"), cfg.keymapEnabled(),
                           [](bool v) { Settings::instance().setKeymapEnabled(v); }),
            buildToggleRow(tr("剪贴板同步"), cfg.clipboardSync(),
                           [](bool v) { Settings::instance().setClipboardSync(v); }),
            buildToggleRow(tr("鼠标平滑"), cfg.mouseSmoothing(),
                           [](bool v) { Settings::instance().setMouseSmoothing(v); }),
            buildToggleRow(tr("自动路由"), cfg.autoRouting(),
                           [](bool v) { Settings::instance().setAutoRouting(v); }),
        };
        root_->addWidget(buildGroup(tr("快捷开关"), rows));
    }
    root_->addWidget(buildSeparator());

    // ── 底部入口 + 退出 ──
    {
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("keyboard"), tr("键位映射"), nullptr,
                        QStringLiteral("keymap")),
            buildNavRow(QStringLiteral("settings"), tr("设置"), nullptr,
                        QStringLiteral("settings")),
        };
        root_->addWidget(buildGroup(QString(), rows));
        root_->addWidget(buildSeparator());

        auto *quit = buildNavRow(QStringLiteral("power"), tr("退出 FlowGrid"), nullptr,
                                 QStringLiteral("quit"));
        qobject_cast<NavRow *>(quit)->setDanger(true);
        root_->addWidget(buildGroup(QString(), {quit}));
    }

    // 设置屏改动（或「恢复默认」）后，托盘的快捷开关要跟着变。
    QObject::connect(&Settings::instance(), &Settings::changed, this, &TrayPopup::refresh);
    refresh();
}

QWidget *TrayPopup::buildSeparator()
{
    auto *sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background:%1;margin:3px %2;")
                           .arg(css(Theme::instance().palette().hairline))
                           .arg(tokens::kS3));
    return sep;
}

QWidget *TrayPopup::buildHead()
{
    const Palette &p = Theme::instance().palette();
    auto *head = new QWidget(this);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(tokens::kS3, 10, tokens::kS3, 10);
    hl->setSpacing(tokens::kS3);

    auto *icon = new QLabel(head);
    icon->setFixedSize(16, 16);
    icon->setPixmap(icons::icon(QStringLiteral("link"), 16, p.text1).pixmap(16, 16));

    auto *title = new QLabel(QStringLiteral("FlowGrid"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));

    hl->addWidget(icon);
    hl->addWidget(title);
    hl->addStretch(1);

    auto *pill = new QWidget(head);
    auto *pl = new QHBoxLayout(pill);
    pl->setContentsMargins(8, 0, 8, 0);
    pl->setSpacing(6);
    auto *dot = new StatusDot(pill);
    headLatency_ = new QLabel(QStringLiteral("--"), pill);
    headLatency_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    pl->addWidget(dot);
    pl->addWidget(headLatency_);
    pill->setStyleSheet(
        QStringLiteral("background:%1;border-radius:10px").arg(css(p.ok.soft)));
    hl->addWidget(pill);
    return head;
}

QWidget *TrayPopup::buildGroup(const QString &label, const QList<QWidget *> &rows)
{
    auto *group = new QWidget(this);
    auto *vl = new QVBoxLayout(group);
    vl->setContentsMargins(0, tokens::kS1, 0, tokens::kS1);
    vl->setSpacing(0);

    if (!label.isEmpty()) {
        auto *lab = new QLabel(label, group);
        lab->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        lab->setContentsMargins(tokens::kS3, 6, tokens::kS3, 6);
        // 设计稿 .tray__label：全大写 + 0.04em 字距 + text-3
        lab->setStyleSheet(
            QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px")
                .arg(css(Theme::instance().palette().text3)));
        vl->addWidget(lab);
    }
    for (QWidget *row : rows) {
        vl->addWidget(row);
    }
    return group;
}

QWidget *TrayPopup::buildNavRow(const QString &iconName, const QString &label, QWidget *trailing,
                                const QString &kind)
{
    auto *row = new NavRow(iconName, label, trailing, this);
    connect(row, &NavRow::clicked, this, [this, kind] {
        if (kind == QLatin1String("connections")) {
            Q_EMIT openConnections();
        } else if (kind == QLatin1String("latency")) {
            Q_EMIT openLatency();
        } else if (kind == QLatin1String("pairing")) {
            Q_EMIT openPairing();
        } else if (kind == QLatin1String("keymap")) {
            Q_EMIT openKeymap();
        } else if (kind == QLatin1String("settings")) {
            Q_EMIT openSettings();
        } else if (kind == QLatin1String("clipboard")) {
            Q_EMIT openClipboard();
        } else if (kind == QLatin1String("files")) {
            Q_EMIT openFiles();
        } else if (kind == QLatin1String("quit")) {
            Q_EMIT quitRequested();
        }
    });
    return row;
}

QWidget *TrayPopup::buildCurrentDevice()
{
    auto *group = buildGroup(tr("当前设备"), {});
    const Palette &p = Theme::instance().palette();

    auto *row = new QWidget(group);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, 7, tokens::kS3, 7);
    hl->setSpacing(tokens::kS2);

    deviceIcon_ = new QLabel(row);
    deviceIcon_->setFixedSize(tokens::kSwitchBtnSize, tokens::kSwitchBtnSize);
    deviceIcon_->setAlignment(Qt::AlignCenter);
    deviceIcon_->setFont(uiFont(tokens::kFont12, QFont::Bold));
    deviceIcon_->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                    .arg(css(p.ok.soft), css(p.ok.text))
                                    .arg(tokens::kRadiusCtl));

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    deviceName_ = new QLabel(QStringLiteral("—"), row);
    deviceName_->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    deviceName_->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
    deviceMeta_ = new QLabel(QStringLiteral("—"), row);
    deviceMeta_->setFont(uiFont(tokens::kFont11));
    deviceMeta_->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    col->addWidget(deviceName_);
    col->addWidget(deviceMeta_);

    hl->addWidget(deviceIcon_);
    hl->addLayout(col);
    hl->addStretch(1);
    hl->addWidget(new Badge(tr("本机"), Badge::Variant::Neutral, row));

    group->layout()->addWidget(row);
    return group;
}

QWidget *TrayPopup::buildQuickSwitch()
{
    auto *group = buildGroup(tr("快速切换"), {});
    auto *row = new QWidget(group);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, 4, tokens::kS3, 7);
    hl->setSpacing(tokens::kS2);
    hl->addStretch(1);
    quickRow_ = hl;
    group->layout()->addWidget(row);
    return group;
}

QWidget *TrayPopup::buildToggleRow(const QString &label, bool on,
                                   const std::function<void(bool)> &onWrite)
{
    auto *row = new QWidget(this);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, 7, tokens::kS3, 7);
    hl->setSpacing(tokens::kS2);
    auto *text = new QLabel(label, row);
    text->setFont(uiFont(tokens::kFont12));
    text->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text1)));
    hl->addWidget(text);
    hl->addStretch(1);
    auto *toggle = new ToggleSwitch(row);
    toggle->setChecked(on);
    QObject::connect(toggle, &ToggleSwitch::clicked, row,
                     [toggle, onWrite] { onWrite(toggle->isChecked()); });
    quickToggles_.append(toggle);   // refresh() 用它跟随「恢复默认」等外部改动
    hl->addWidget(toggle);
    return row;
}

void TrayPopup::popupAt(const QPoint &anchor)
{
    placePopup(this, anchor);
    animatePopupIn(this);
}

void TrayPopup::refresh()
{
    const Device *active = manager_.activeDevice();
    const bool online = active != nullptr && active->online;
    const double ms = manager_.activeLatencyMs();

    // 快捷开关跟随配置：「恢复默认」或设置屏改动后，托盘这几个不能还摆着旧值。
    {
        auto &cfg = Settings::instance();
        const bool values[4] = {cfg.keymapEnabled(), cfg.clipboardSync(),
                                cfg.mouseSmoothing(), cfg.autoRouting()};
        for (int i = 0; i < quickToggles_.size() && i < 4; ++i) {
            if (quickToggles_.at(i)->isChecked() != values[i]) {
                quickToggles_.at(i)->setChecked(values[i]);
            }
        }
    }

    if (headLatency_ != nullptr) {
        headLatency_->setText(online ? QStringLiteral("%1ms").arg(ms, 0, 'f', 1)
                                     : QStringLiteral("--"));
        headLatency_->setStyleSheet(QStringLiteral("color:%1").arg(
            css(Theme::instance().color(Theme::qualityOf(ms, online)).text)));
    }
    if (trayLatency_ != nullptr) {
        trayLatency_->setLatency(ms, online);
    }

    if (deviceName_ != nullptr) {
        if (active != nullptr) {
            deviceIcon_->setText(active->abbrev.isEmpty()
                                     ? active->name.left(2).toUpper()
                                     : active->abbrev);
            deviceName_->setText(active->name);
            deviceMeta_->setText(active->hotkeyIndex > 0
                                     ? tr("Ctrl+%1 切换回来").arg(active->hotkeyIndex)
                                     : tr("未分配快捷键"));
        } else {
            deviceIcon_->setText(QStringLiteral("--"));
            deviceName_->setText(tr("无活动设备"));
            deviceMeta_->setText(tr("从托盘进入设备与连接"));
        }
    }

    // 快速切换按钮：每次重建而不是做增量 diff——设备是个位数，重建成本可忽略，
    // 而增量同步要处理的边界（上线、下线、热键变更）远比重建麻烦。
    if (quickRow_ != nullptr) {
        while (quickRow_->count() > 0) {
            QLayoutItem *item = quickRow_->takeAt(0);
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }
        for (const Device &d : manager_.devices()) {
            if (!d.online || d.hotkeyIndex <= 0) {
                continue;
            }
            const StatusColor sc = Theme::instance().color(
                d.isActive ? Quality::Good : Quality::Idle);
            const QString abbrev = d.abbrev.isEmpty()
                                       ? d.name.section(QLatin1Char(' '), 0, 0).left(2).toUpper()
                                       : d.abbrev;
            auto *btn = new QuickButton(abbrev, d.hotkeyIndex, sc, this);
            QObject::connect(btn, &QuickButton::clicked, this, [this, id = d.id] {
                manager_.setActive(id);
            });
            quickRow_->addWidget(btn);
        }
        quickRow_->addStretch(1);
    }
}

void TrayPopup::paintEvent(QPaintEvent *e)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                        tokens::kRadiusSurface, tokens::kRadiusSurface);
    pt.setPen(QPen(p.line2, 1));
    pt.setBrush(p.surface);
    pt.drawPath(path);

    pt.save();
    pt.setClipPath(path);
    QLinearGradient edge(0, 0, 0, 2);
    edge.setColorAt(0.0, p.edgeHi);
    edge.setColorAt(1.0, QColor(0, 0, 0, 0));
    pt.fillRect(QRectF(1, 1, width() - 2, 2), edge);
    pt.restore();

    QWidget::paintEvent(e);
}

} // namespace fg

// QuickButton 的 moc：Q_OBJECT 写在 .cpp 里时必须显式包含。
#include "traypopup.moc"
