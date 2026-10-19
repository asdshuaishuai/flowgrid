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
#include <QKeyEvent>
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

// 设备切换的修饰键：设置屏「切换热键」1=Ctrl / 2=Alt。托盘里三处显示的是同一个
// 键——两条导航行的快捷键、快速切换按钮上的键位、当前设备那行的「切换回来」。
// 早先只有导航行跟着设置走，另两处写死 Ctrl，用户把热键改成 Alt 后，界面上
// 那两处就指着一组并不存在的快捷键。
QString switchModifier()
{
    return switchModifierName(Settings::instance().switchHotkey());
}

// 「修饰键 + 数字」的完整键位串（数字是设备的热键序号）。
QString deviceHotkeyLabel(int index)
{
    return QStringLiteral("%1+%2").arg(switchModifier()).arg(index);
}

// 快速切换按钮：设计稿里它就是一个「图标方块 + 快捷键」的可点块。
// 做成类而不是裸 QWidget，是因为 QWidget::mouseReleaseEvent 是 protected，
// 没法用 connect 直接挂——与其用事件过滤器绕，不如把点击语义收进类型里。
// .tray__kbd：11px / text-3 / tabular-nums。托盘是键盘用户的主入口，
// 每个浮板右上角摆着它的快捷键才有用。
QWidget *kbdHint(const QString &key)
{
    const Palette &p = Theme::instance().palette();
    auto *w = new QWidget;
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS1);
    // 修饰键名跟着设置屏的「切换热键」走：默认 1=Ctrl，改成 Alt 时这里也显示 Alt。
    for (const QString &part : {switchModifier(), key}) {
        auto *cap = new QLabel(part, w);
        cap->setFont(uiFont(tokens::kFont11));
        cap->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        hl->addWidget(cap);
    }
    hl->addStretch(1);
    return w;
}

class QuickButton : public QWidget {
    Q_OBJECT
public:
    QuickButton(const QString &abbrev, int hotkey, const StatusColor &sc, bool active,
                QWidget *parent)
        : QWidget(parent), active_(active)
    {
        setCursor(Qt::PointingHandCursor);
        // 描边与底色要画在「自己」身上，而普通 QWidget 子类默认不参与样式表绘制，
        // 必须显式打开——DropZone 当初就是漏了这一句，虚线框画不出来。
        setAttribute(Qt::WA_StyledBackground, true);
        auto *vl = new QVBoxLayout(this);
        // ui.css .switch-btn：padding 6 / gap 4 / 1px line-2 描边 / r-ctl。
        vl->setContentsMargins(6, 6, 6, 6);
        vl->setSpacing(4);

        // .switch-btn__icon：24×24 实色块 + 落在实色上的字色。实色取状态色 solid
        // 一档，不用 soft——soft 底 + 同族字色在 24px 的小块上几乎分不出档位。
        auto *icon = new QLabel(abbrev, this);
        icon->setFixedSize(tokens::kSwitchBtnIconSize, tokens::kSwitchBtnIconSize);
        icon->setAlignment(Qt::AlignCenter);
        icon->setFont(uiFont(tokens::kFont11, QFont::Bold));
        icon->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                 .arg(css(sc.solid))
                                 .arg(css(Theme::instance().palette().onSolid))
                                 .arg(tokens::kRadiusCtl));

        // .switch-btn__key
        auto *kbd = new QLabel(deviceHotkeyLabel(hotkey), this);
        kbd->setFont(uiFont(tokens::kFont11));
        kbd->setAlignment(Qt::AlignCenter);
        kbd->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));

        vl->addWidget(icon);
        vl->addWidget(kbd);

        connect(&Theme::instance(), &Theme::changed, this, [this] { restyle(); });
        restyle();
    }

    void setActive(bool on)
    {
        active_ = on;
        restyle();
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
    void enterEvent(QEnterEvent *e) override
    {
        hovered_ = true;
        restyle();
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        hovered_ = false;
        restyle();
        QWidget::leaveEvent(e);
    }

private:
    // .switch-btn:hover 与 [aria-pressed="true"] 都是 accent 描边 + accent-soft 底。
    void restyle()
    {
        const Palette &p = Theme::instance().palette();
        const bool on = hovered_ || active_;
        applyBoxStyle(this,
                      QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                          .arg(css(on ? p.accent : p.line2))
                          .arg(tokens::kRadiusCtl)
                          .arg(css(on ? p.accentSoft : p.card)));
    }

    bool active_ = false;
    bool hovered_ = false;
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
                        // 设备数是这一行最有用的信息，写死一个数会让人以为托盘
                        // 知道设备列表——它其实有 DeviceManager。记下指针由
                        // refresh() 跟着真实在线数更新。
                        deviceCount_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral,
                                                 this),
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
        clipboardBadge_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral, this);
        filesBadge_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral, this);
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("clipboard"), tr("剪贴板"), clipboardBadge_,
                        QStringLiteral("clipboard")),
            buildNavRow(QStringLiteral("file"), tr("文件传输"), filesBadge_,
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
        // 设计稿这两行右侧是 .tray__kbd 的快捷键提示（Ctrl K / Ctrl ,）。
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("keyboard"), tr("键位映射"), kbdHint("K"),
                        QStringLiteral("keymap")),
            buildNavRow(QStringLiteral("settings"), tr("设置"), kbdHint(","),
                        QStringLiteral("settings")),
        };
        root_->addWidget(buildGroup(QString(), rows));
        root_->addWidget(buildSeparator());

        // 设计稿这行右侧也有 .tray__kbd（Ctrl Q），早先传了 nullptr 空着。
        auto *quit = buildNavRow(QStringLiteral("power"), tr("退出 FlowGrid"), kbdHint("Q"),
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
    icon->setFixedSize(tokens::kIconMd, tokens::kIconMd);
    icon->setPixmap(icons::icon(QStringLiteral("link"), tokens::kIconMd, p.text1).pixmap(tokens::kIconMd, tokens::kIconMd));

    auto *title = new QLabel(QStringLiteral("FlowGrid"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));

    hl->addWidget(icon);
    hl->addWidget(title);
    hl->addStretch(1);

    auto *pill = new QWidget(head);
    pill->setFixedHeight(tokens::kStatusPillHeight);  // 圆角取高的一半
    auto *pl = new QHBoxLayout(pill);
    pl->setContentsMargins(8, 0, 8, 0);
    pl->setSpacing(6);
    auto *dot = new StatusDot(pill);
    headLatency_ = new QLabel(QStringLiteral("--"), pill);
    headLatency_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    pl->addWidget(dot);
    pl->addWidget(headLatency_);
    pill->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                        .arg(css(p.ok.soft), QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
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
    // 设计稿 ui.css .tray__active：margin 4px 8px / padding 8px 12px /
    // 1px accent-line 描边 + r-card 圆角 + accent-soft 底。早先这里只排了内边距，
    // 卡片那一圈描边与底色整块没画，「当前设备」看起来跟下面几行一样是浮在
    // 浮层底上的普通文字行，用户一眼看不出这是当前正在用的那台。
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
    hl->setSpacing(tokens::kS2);
    row->setAttribute(Qt::WA_StyledBackground, true);
    const auto styleActiveRow = [row] {
        const Palette &pal = Theme::instance().palette();
        // 三个占位符要三个实参：此前只 arg 了两个，且把 accent-soft 填进了
        // `border-radius:%2px` 的位置、`background:%3` 原样留着——Qt 解析不了
        // 这条声明（报 parseHexColor: Unknown color name '#...ffpx'），整条规则
        // 连同描边一起被丢掉，上面刚补的「当前设备」卡片其实一直没画出来。
        applyBoxStyle(row,
                      QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                          .arg(css(pal.accentLine))
                          .arg(QString::number(tokens::kRadiusCard))
                          .arg(css(pal.accentSoft)));
    };
    connect(&Theme::instance(), &Theme::changed, row, styleActiveRow);
    styleActiveRow();

    deviceIcon_ = new QLabel(row);
    deviceIcon_->setFixedSize(tokens::kTrayActiveIconSize, tokens::kTrayActiveIconSize);
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

    // margin 4px 8px：卡片自身要内缩，托盘浮层才有一圈呼吸。
    static_cast<QVBoxLayout *>(group->layout())->setContentsMargins(
        tokens::kS2, tokens::kS1, tokens::kS2, tokens::kS1);
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

    if (deviceCount_ != nullptr) {
        // 在线设备数（不含离线）。设计稿写的是「3 台」，而这台机器上没有对端，
        // 如实显示真实在线数——托盘常驻，给一个假的数字比显示 0 更糟。
        int online = 0;
        for (const Device &d : manager_.devices()) {
            if (d.online) {
                ++online;
            }
        }
        deviceCount_->setText(online == 0 ? tr("0 台在线") : tr("%1 台在线").arg(online));
        deviceCount_->setVariant(online > 0 ? Badge::Variant::Ok : Badge::Variant::Idle);
    }

    if (deviceName_ != nullptr) {
        if (active != nullptr) {
            deviceIcon_->setText(active->abbrev.isEmpty()
                                     ? active->name.left(2).toUpper()
                                     : active->abbrev);
            deviceName_->setText(active->name);
            deviceMeta_->setText(active->hotkeyIndex > 0
                                     ? tr("%1 切换回来").arg(
                                           deviceHotkeyLabel(active->hotkeyIndex))
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
        clearLayout(quickRow_);
        for (const Device &d : manager_.devices()) {
            if (!d.online || d.hotkeyIndex <= 0) {
                continue;
            }
            // 设计稿给三台设备的是 accent / ok / warn 三种实色，而不是随手挑的：
            // 当前控制端取 ok 实色，其余按各自的延迟档取——良好给主色、偏慢给
            // warn、告警给 danger、离线给 idle。这样「哪个慢了」在托盘上一眼可辨，
            // 规则由读数推出，不随排版或设备顺序漂。
            const Palette &pal = Theme::instance().palette();
            const Quality q = Theme::instance().qualityOf(d.latencyMs, d.online);
            StatusColor sc = d.isActive
                                 ? pal.ok
                                 : (q == Quality::Good
                                        ? StatusColor{pal.accent, pal.accentSoft, pal.accentText}
                                        : Theme::instance().color(q));
            const QString abbrev = d.abbrev.isEmpty()
                                       ? d.name.section(QLatin1Char(' '), 0, 0).left(2).toUpper()
                                       : d.abbrev;
            auto *btn = new QuickButton(abbrev, d.hotkeyIndex, sc, d.isActive, this);
            QObject::connect(btn, &QuickButton::clicked, this, [this, id = d.id] {
                manager_.setActive(id);
            });
            quickRow_->addWidget(btn);
        }
        quickRow_->addStretch(1);
    }
}

void TrayPopup::setCounts(int clipEntries, int activeTransfers)
{
    if (clipboardBadge_ != nullptr) {
        clipboardBadge_->setText(clipEntries > 0 ? tr("%1 项").arg(clipEntries) : tr("空闲"));
        clipboardBadge_->setVariant(clipEntries > 0 ? Badge::Variant::Neutral
                                                    : Badge::Variant::Idle);
    }
    if (filesBadge_ != nullptr) {
        filesBadge_->setText(activeTransfers > 0 ? tr("%1 项").arg(activeTransfers)
                                                 : tr("空闲"));
        filesBadge_->setVariant(activeTransfers > 0 ? Badge::Variant::Ok
                                                    : Badge::Variant::Idle);
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
    pt.setBrush(p.page);
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

void TrayPopup::keyPressEvent(QKeyEvent *e)
{
    // Esc 关闭托盘浮层。与 520px 浮板同一个理由：托盘是这应用唯一的常驻
    // 界面，「失焦即走」只照顾得到鼠标，纯键盘用户需要一个明确的收起动作。
    if (e->key() == Qt::Key_Escape) {
        hide();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace fg

// QuickButton 的 moc：Q_OBJECT 写在 .cpp 里时必须显式包含。
#include "traypopup.moc"
