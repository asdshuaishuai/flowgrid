#include "traypopup.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "panel.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace fg {
namespace {

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

// 「修饰键 + 键」的纯文本键位串，给 tooltip 用——图标按钮没有行尾位置，
// 快捷键提示只能写进 tooltip。
QString kbdText(const QString &key)
{
    return QStringLiteral("%1+%2").arg(switchModifier()).arg(key);
}

// 快速切换按钮：设计稿里它就是一个「图标方块 + 快捷键」的可点块。
// 做成类而不是裸 QWidget，是因为 QWidget::mouseReleaseEvent 是 protected，
// 没法用 connect 直接挂——与其用事件过滤器绕，不如把点击语义收进类型里。
// .tray__kbd：11px / text-3 / tabular-nums。托盘是键盘用户的主入口，
// 每个浮板右上角摆着它的快捷键才有用。
class QuickButton : public QWidget {
    Q_OBJECT
public:
    QuickButton(const QString &abbrev, int hotkey, const StatusColor &sc, bool active,
                QWidget *parent)
        : QWidget(parent), status_(sc), active_(active)
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
        iconLabel_ = icon;

        // .switch-btn__key
        auto *kbd = new QLabel(deviceHotkeyLabel(hotkey), this);
        kbd->setFont(uiFont(tokens::kFont11));
        kbd->setAlignment(Qt::AlignCenter);
        kbdLabel_ = kbd;

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
                          .arg(cssColor(on ? p.accent : p.line2))
                          .arg(tokens::kRadiusCtl)
                          .arg(cssColor(on ? p.accentSoft : p.card)));
        // 上面只换了按钮自己的框；里面那枚实色方块与下面的键位串还有各自的
        // 颜色，不跟着重刷的话，明暗切换后它们就停在旧值上。
        iconLabel_->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                      .arg(cssColor(status_.solid))
                                      .arg(cssColor(p.onSolid))
                                      .arg(tokens::kRadiusCtl));
        kbdLabel_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    }

    StatusColor status_;
    QLabel *iconLabel_ = nullptr;
    QLabel *kbdLabel_ = nullptr;
    bool active_ = false;
    bool hovered_ = false;
};

} // namespace

TrayPopup::TrayPopup(QWidget *parent) : QWidget(parent), manager_(DeviceManager::instance())
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Popup | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    // 「失焦即走」的兜底，与 520px 浮板同一套做法：装在自己身上，
    // 失活事件（WindowDeactivate）正是发给这个顶层 popup 窗的。
    // 没有它，X11 上靠 Qt::Popup 自动关，Wayland 上就收不回去了。
    installEventFilter(this);

    // 外壳与 520px 浮板完全同一套：窗口留阴影边距 → 阴影板 → 圆角卡片。
    // 托盘早先是「裸 QWidget 自己 paintEvent 画圆角+描边」，和浮板那套各画各的，
    // 边框粗细、圆角、高光位置都不一样——两个浮层同屏出现就是「内外不一致」。
    //
    // 基准余白见 designtokens.h；真正用的时候贴任务栏那一侧会被去掉
    // （edgeAdjustedMargin），否则托盘浮层会悬在任务栏上方 32px，而 520px
    // 浮板悬 39px——两个浮层同屏时高度对不齐，又是「内外不一致」。
    baseMargin_ = {tokens::kShadowTrayMarginX, tokens::kShadowTrayMarginTop,
                    tokens::kShadowTrayMarginX, tokens::kShadowTrayMarginBot};
    margin_ = baseMargin_;
    setFixedWidth(tokens::kTrayWidth + margin_.left + margin_.right);

    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(margin_.left, margin_.top, margin_.right, margin_.bottom);
    root_->setSpacing(0);

    card_ = new Surface(this);
    cardLayout_ = new QVBoxLayout(card_);
    cardLayout_->setContentsMargins(0, 0, 0, 0);
    cardLayout_->setSpacing(0);
    root_->addWidget(card_);

    // 设计稿：托盘浮层阴影 0 16px 32px rgba(15,23,42,.16)
    plate_ = new ShadowPlate(this);
    plate_->setRadius(tokens::kRadiusSurface);
    plate_->setShadow(Theme::instance().palette().shadowTray, tokens::kShadowTrayR,
                      0, tokens::kShadowTrayY);

    QWidget *head = buildHead();
    cardLayout_->addWidget(head);
    cardLayout_->addWidget(buildSeparator());
    cardLayout_->addWidget(buildCurrentDevice());
    cardLayout_->addWidget(buildSeparator());
    cardLayout_->addWidget(buildQuickSwitch());
    cardLayout_->addWidget(buildSeparator());

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
        cardLayout_->addWidget(buildGroup(tr("连接"), rows));
    }
    cardLayout_->addWidget(buildSeparator());

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
        cardLayout_->addWidget(buildGroup(tr("数据与同步"), rows));
    }
    cardLayout_->addWidget(buildSeparator());

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
        cardLayout_->addWidget(buildGroup(tr("快捷开关"), rows));
    }
    cardLayout_->addWidget(buildSeparator());

    // ── 底部入口 ──
    //
    // 「设置」和「退出 FlowGrid」做成底部工具条里的两个图标按钮（见
    // buildBottomBar），这里只留「键位映射」这一行——它是功能入口，不是开关，
    // 留在列表里更合适。
    {
        // 设计稿这行右侧是 .tray__kbd 的快捷键提示（Ctrl K）。
        QList<QWidget *> rows{
            buildNavRow(QStringLiteral("keyboard"), tr("键位映射"), buildKbdHint(QStringLiteral("K")),
                        QStringLiteral("keymap")),
        };
        cardLayout_->addWidget(buildGroup(QString(), rows));
    }

    // 底部工具条（设置 / 退出），与 mac 端 TrayView.bottomBar 一致。
    cardLayout_->addWidget(buildSeparator());
    cardLayout_->addWidget(buildBottomBar());

    // 设置屏改动（或「恢复默认」）后，托盘的快捷开关要跟着变。
    QObject::connect(&Settings::instance(), &Settings::changed, this, &TrayPopup::refresh);
    // 系统主题被切走时，卡片底、band、描边、投影都要跟着换色。
    QObject::connect(&Theme::instance(), &Theme::changed, this, [this] { restyle(); });
    restyle();
    refresh();
}

void TrayPopup::restyle()
{
    if (plate_ != nullptr) {
        plate_->setShadow(Theme::instance().palette().shadowTray, tokens::kShadowTrayR,
                          0, tokens::kShadowTrayY);
    }
    if (card_ != nullptr) {
        // 托盘头部是**外壳底自己**（设计稿 .tray__head 没有自己的 background，
        // 直接继承 .tray 的 surface），下面一条 line-1 分隔。
        //
        // 别跟 520px 浮板的标题栏学：那条是 surface-2（灰），因为 .titlebar
        // 明确写了 background。两个浮层同屏出现时，头部一灰一白，看着像两个
        // 不同控件拼的——「内外不一致」。
        card_->setBandColor(Theme::instance().palette().surface);
    }
    // 逐项重刷那些构造时写死了颜色的控件（头部标题、分组小标题、快捷开关行名、
    // 快捷键提示、当前设备那一块）。少了这一步，明暗切换后它们停在旧值上。
    for (const auto &fn : restylers_) {
        fn();
    }
    update();
}

void TrayPopup::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    syncShadowPlate();
}

bool TrayPopup::eventFilter(QObject *watched, QEvent *event)
{
    // 与 Panel 同一套：失焦即走。延后一拍再收，并且只在「仍然失焦」时收——
    // 否则用户刚重新点开托盘，这一条延后的 hide 会把它又收掉。
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !isActiveWindow()) {
                hide();
            }
        });
    }
    return QWidget::eventFilter(watched, event);
}

void TrayPopup::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    syncShadowPlate();
}

void TrayPopup::syncShadowPlate()
{
    if (plate_ == nullptr || card_ == nullptr) {
        return;
    }
    plate_->setGeometry(rect());
    plate_->setCardRect(card_->geometry());
    plate_->lower();
}

QWidget *TrayPopup::buildSeparator()
{
    // 真正的分隔线控件：早先这里是「1px 高的 QWidget + 样式表 margin」，
    // 样式表的 margin 是在控件内部扣的，1px 高的控件扣掉上下各 3px 之后
    // 内容区变成负数，那条线**根本没画出来**——两个区块看着就黏在一起。
    auto *sep = new Divider(this);
    sep->setInset(tokens::kS3);
    return sep;
}

QWidget *TrayPopup::buildHead()
{
    auto *head = new QWidget(this);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(tokens::kS3, 10, tokens::kS3, 10);
    hl->setSpacing(tokens::kS3);
    // 头部高度定死：里面的图标按钮 26px，上下各 10px 留白 = 46。
    // 卡片顶部的 band 就按这个高度画（band 与 520px 浮板的标题栏同一档底色），
    // 所以头部的底色和下边那道 hairline 都由卡片来画，头部自己保持透明——
    // 头部要是自己画方形底色，卡片顶部的圆角就被顶成直角了。
    head->setFixedHeight(46);
    if (card_ != nullptr) {
        card_->setBandHeight(46);
    }

    auto *icon = new QLabel(head);
    icon->setFixedSize(tokens::kIconMd, tokens::kIconMd);

    auto *title = new QLabel(QStringLiteral("FlowGrid"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));

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
    hl->addWidget(pill);

    // 头部三处的颜色登记一份重刷：图标、标题、状态胶囊的底色。
    restylers_.append([icon, title, pill, this] {
        const Palette &pal = Theme::instance().palette();
        icon->setPixmap(icons::icon(QStringLiteral("link"), tokens::kIconMd, pal.text1)
                            .pixmap(tokens::kIconMd, tokens::kIconMd));
        title->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(pal.text1)));
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                                .arg(cssColor(pal.ok.soft),
                                     QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
    });

    // 头部只有「图标 + 标题 + 状态胶囊」，右上角不放别的。
    //
    // 「设置 / 退出」两个图标按钮原先挂在头部右上角（见 buildBottomBar 的说明），
    // 现在挪到了底部工具条——和 mac 端 TrayView.bottomBar 一致，也是设计稿
    // .tray__head 的样子：头只负责「这是哪个应用 + 现在什么状态」。
    return head;
}

// 底部工具条：右对齐的「设置 / 退出」两个图标按钮。
//
// 位置参照 mac 端 TrayView.bottomBar：托盘的头只留标题和状态胶囊，设置/退出
// 这两个「看一眼就点」的动作放在底部工具条里（上方一条 hairline 隔开）。
//
// 之前这两个按钮挂在头部右上角，带来的问题：
//   · 头部变成三段（图标 / 标题 / 胶囊 / 按钮），右边一坨按钮跟左边的标题
//     不齐，标题栏看着像「塞了两排东西」；
//   · 设计稿 .tray__head 与 mac 端都没有这两个按钮，浮层头部跟我们自己的
//     参照实现对不上（「内外不一致」）；
//   · 底部最后一行「键位映射」直接贴着卡片的圆角收口，没有留白，看着像被裁了。
//
// 快捷键提示写进 tooltip：文字行没了，提示不能跟着一起消失（tooltip 里写全，
// 含修饰键，跟着设置屏的「切换热键」走）。
QWidget *TrayPopup::buildKbdHint(const QString &key)
{
    auto *w = new QWidget(this);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS1);
    // 修饰键名跟着设置屏的「切换热键」走：默认 1=Ctrl，改成 Alt 时这里也显示 Alt。
    QList<QLabel *> caps;
    for (const QString &part : {switchModifier(), key}) {
        auto *cap = new QLabel(part, w);
        cap->setFont(uiFont(tokens::kFont11));
        caps.append(cap);
        hl->addWidget(cap);
    }
    hl->addStretch(1);
    restylers_.append([caps] {
        const QColor c = Theme::instance().palette().text3;
        for (QLabel *cap : caps) {
            cap->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(c)));
        }
    });
    return w;
}

QWidget *TrayPopup::buildBottomBar()
{
    auto *bar = new QWidget(this);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
    bl->setSpacing(tokens::kS1);
    bl->addStretch(1);

    auto *settingsBtn = new IconButton(QStringLiteral("settings"),
                                       tr("设置 (%1)").arg(kbdText(",")), bar);
    connect(settingsBtn, &IconButton::clicked, this, [this] { Q_EMIT openSettings(); });
    auto *quitBtn = new IconButton(QStringLiteral("power"),
                                    tr("退出 FlowGrid (%1)").arg(kbdText("Q")), bar);
    connect(quitBtn, &IconButton::clicked, this, [this] { Q_EMIT quitRequested(); });
    bl->addWidget(settingsBtn);
    bl->addWidget(quitBtn);
    return bar;
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
        restylers_.append([lab] {
            // 设计稿 .tray__label：全大写 + 0.04em 字距 + text-3
            lab->setStyleSheet(
                QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px")
                    .arg(cssColor(Theme::instance().palette().text3)));
        });
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
        } else if (kind == QLatin1String("clipboard")) {
            Q_EMIT openClipboard();
        } else if (kind == QLatin1String("files")) {
            Q_EMIT openFiles();
        }
        // 「设置」和「退出」没有这一支了：它们现在是底部工具条里的图标按钮，
        // 直接连自己的信号（见 buildBottomBar），不再走导航行。
    });
    return row;
}

QWidget *TrayPopup::buildCurrentDevice()
{
    auto *group = buildGroup(tr("当前设备"), {});

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
                          .arg(cssColor(pal.accentLine))
                          .arg(QString::number(tokens::kRadiusCard))
                          .arg(cssColor(pal.accentSoft)));
    };
    connect(&Theme::instance(), &Theme::changed, row, styleActiveRow);
    styleActiveRow();

    deviceIcon_ = new QLabel(row);
    deviceIcon_->setFixedSize(tokens::kTrayActiveIconSize, tokens::kTrayActiveIconSize);
    deviceIcon_->setAlignment(Qt::AlignCenter);
    deviceIcon_->setFont(uiFont(tokens::kFont12, QFont::Bold));

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    deviceName_ = new QLabel(QStringLiteral("—"), row);
    deviceName_->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    deviceMeta_ = new QLabel(QStringLiteral("—"), row);
    deviceMeta_->setFont(uiFont(tokens::kFont11));
    col->addWidget(deviceName_);
    col->addWidget(deviceMeta_);

    // 「当前设备」那一枚状态方块与设备名 / 副标题的字色，一并跟着主题重刷。
    restylers_.append([this] {
        const Palette &pal = Theme::instance().palette();
        deviceIcon_->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                        .arg(cssColor(pal.ok.soft), cssColor(pal.ok.text))
                                        .arg(tokens::kRadiusCtl));
        deviceName_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(pal.text1)));
        deviceMeta_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(pal.text3)));
    });

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
    restylers_.append([text] {
        text->setStyleSheet(QStringLiteral("color:%1")
                                .arg(cssColor(Theme::instance().palette().text1)));
    });
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
    // 贴任务栏那一侧不留投影余白，理由与 520px 浮板完全一致（edgeAdjustedMargin）。
    // 屏按锚点选：多屏时任务栏在哪一侧也是按那块屏算的。
    margin_ = edgeAdjustedMargin(baseMargin_, screenForAnchor(anchor, this));
    root_->setContentsMargins(margin_.left, margin_.top, margin_.right, margin_.bottom);
    placePopup(this, anchor, margin_);
    animatePopupIn(this);
}

void TrayPopup::refresh()
{
    const Device *active = manager_.activeDevice();
    // 「有读数」和「设备在线」是两件事：设备在线但链路还没测通时，
    // 读数是「无」而不是 0。界面上一个是「--」，一个是绿点。
    const double ms = manager_.activeLatencyMs();
    const bool hasReading = ms >= 0.0;

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
        headLatency_->setText(hasReading ? QStringLiteral("%1ms").arg(ms, 0, 'f', 1)
                                          : QStringLiteral("--"));
        headLatency_->setStyleSheet(QStringLiteral("color:%1").arg(
            cssColor(Theme::instance().color(Theme::qualityOf(ms, hasReading)).text)));
    }
    if (trayLatency_ != nullptr) {
        trayLatency_->setLatency(ms, hasReading);
    }

    if (deviceCount_ != nullptr) {
        // 徽章讲的是**能切过去的对端**有几台在线（不含本机）。
        // 传输层没落地时对端一台也没有，如实说「仅本机」——
        // 写个「1 台在线」会让人以为有一台对端可以连。
        int peers = 0;
        bool localOnline = false;
        for (const Device &d : manager_.devices()) {
            if (!d.online) {
                continue;
            }
            if (d.isLocal) {
                localOnline = true;
            } else {
                ++peers;
            }
        }
        if (peers > 0) {
            deviceCount_->setText(tr("%1 台在线").arg(peers));
            deviceCount_->setVariant(Badge::Variant::Ok);
        } else if (localOnline) {
            deviceCount_->setText(tr("仅本机"));
            deviceCount_->setVariant(Badge::Variant::Idle);
        } else {
            deviceCount_->setText(tr("未联网"));
            deviceCount_->setVariant(Badge::Variant::Idle);
        }
    }

    if (deviceName_ != nullptr) {
        if (active != nullptr) {
            deviceIcon_->setText(active->abbrev.isEmpty()
                                     ? active->name.left(2).toUpper()
                                     : active->abbrev);
            deviceName_->setText(active->name);
            // 这一行写的是这台机器**真实**的两件事：系统与地址。
            // 早先这里写的是「Ctrl+2 切换回来」——那是在说「怎么切到这台」，
            // 而本机根本不是切换目标，写它等于回答了一个没人问的问题。
            QString meta = active->platformLabel;
            if (!active->address.isEmpty()) {
                meta += QStringLiteral(" · ") + active->address;
            }
            if (!active->online) {
                meta = tr("未联网");
            }
            deviceMeta_->setText(meta);
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
        int shown = 0;
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
            ++shown;
        }
        if (shown == 0) {
            // 没有可切换的对端时说清楚，而不是留一行空白让人以为渲染坏了。
            auto *hint = new QLabel(tr("暂无可切换的设备"), this);
            hint->setFont(uiFont(tokens::kFont11));
            hint->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(Theme::instance().palette().text3)));
            quickRow_->addWidget(hint);
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
