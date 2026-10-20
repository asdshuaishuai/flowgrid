#include "panel.h"
#include "core/designtokens.h"
#include "core/theme.h"
#include "widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace fg {
namespace {

// 透明色必须带 alpha：ok-soft / accent-soft 这类柔和底色就是靠透明度叠出来的。
QString colorHex(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

QFont titleFont()
{
    QFont f = baseFont();
    // 面板标题是这一屏最重的字：13px + DemiBold。原先 12px，和正文里那些
    // 13px 的组标题撞在一档，标题栏看着像「又一行说明文字」。
    f.setPixelSize(tokens::kFont13);
    f.setWeight(QFont::DemiBold);
    return f;
}

} // namespace

ShellMargin edgeAdjustedMargin(const ShellMargin &base)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return base;
    }
    const QRect geo = screen->geometry();
    const QRect avail = screen->availableGeometry();

    // 任务栏在哪一侧：屏幕矩形 - 可用矩形的差（X11 的 struts / Wayland 的工作区）。
    const bool atBottom = avail.bottom() < geo.bottom();
    const bool atTop = avail.top() > geo.top();

    ShellMargin m = base;
    if (atBottom || !atTop) {
        // 贴下沿：底部不留投影余白。
        m.bottom = 0;
    } else {
        // 贴上沿：顶部不留余白（投影本来就朝下，顶部那圈余白给不出东西）。
        m.top = 0;
    }
    return m;
}

bool placePopup(QWidget *w, const QPoint &anchor, const ShellMargin &margin)
{
    if (w == nullptr) {
        return false;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        w->show();
        return false;
    }
    const QRect geo = screen->geometry();
    const QRect avail = screen->availableGeometry();

    // 窗口高度 = 卡片高 + 上下留白。sizeHint 给的是整块窗口的高度。
    const int winH = qMin(w->sizeHint().height(), avail.height() - tokens::kPanelScreenMargin);
    w->resize(w->width(), winH);
    // 卡片（看得见的那块）在窗口里的尺寸。
    const int cardW = w->width() - margin.left - margin.right;
    const int cardH = winH - margin.top - margin.bottom;

    // 贴着**任务栏**摆：卡片下沿落在任务栏上沿，右沿离屏幕右缘一个阴影留白
    // （留白正好给投影落脚，卡片就不会被屏幕边缘切出一圈硬边）。
    //
    // 原来拿托盘图标的中心当锚点（x = anchor.x() - width），有两个毛病：
    //   1. DDE 的 dock 不提供 SNI 图标几何，QSystemTrayIcon::geometry() 恒为空，
    //      于是走「退到主屏右上角」的兜底——浮板浮在屏幕靠边的上方，离任务栏
    //      200 多像素。实测窗口落在 y=64，任务栏顶边在 y=1039，中间空一大截，
    //      看着像「靠边居中」而不是「从菜单栏顶上长出来」。
    //   2. 就算几何取得到，图标中心离屏幕右缘还有二十来像素（托盘区有内边距），
    //      浮板右边也就跟着缺一截。
    //
    // 所以改成按「工作区（可用区）在任务栏那一侧的角」定位。任务栏在哪一侧由
    // 「屏幕矩形 - 可用矩形」的差推出（X11 的 struts / Wayland 的工作区）。
    const bool panelAtBottom = avail.bottom() < geo.bottom();
    const bool panelAtTop = avail.top() > geo.top();
    const bool panelAtLeft = avail.left() > geo.left();

    // 横向贴哪一侧：托盘图标在哪一侧就贴哪一侧。图标几何时拿不到（DDE 上
    // 就是拿不到），按「托盘通常在右」处理。
    const bool anchorOnLeft = !anchor.isNull() && anchor.x() < avail.center().x();

    int cardX = anchorOnLeft ? avail.left() + margin.left : avail.right() - margin.right - cardW;
    // 任务栏在左：可用区已经右移过了任务栏，此时浮板左边贴 avail.left()
    // 才是「从任务栏边上长出来」；否则保持上面算出的靠右/靠左。
    if (panelAtLeft && !panelAtBottom && !panelAtTop) {
        cardX = avail.left() + margin.left;
    }
    // 任务栏在下（或者压根没有任务栏）→ 贴下沿；任务栏在上 → 贴上沿。
    //
    // 卡片的**下沿**正好压在可用区的最后一行上（任务栏的第一行）。用
    // avail.bottom() - cardH + 1 而不是 avail.bottom() - cardH：QRect 的
    // bottom() 是「最后一行」，差这一像素卡片就与任务栏之间多出一条缝。
    int cardY = (panelAtBottom || !panelAtTop) ? avail.bottom() - cardH + 1 : avail.top();

    if (cardX < avail.left()) {
        cardX = avail.left();
    }
    if (cardX + cardW > avail.right()) {
        cardX = avail.right() - cardW;
    }
    if (cardY < avail.top()) {
        cardY = avail.top();
    }
    // 卡片的**最后一行**不能越过可用区的最后一行（inclusive 比较）。
    if (cardY + cardH - 1 > avail.bottom()) {
        cardY = avail.bottom() - cardH + 1;
    }

    // 窗口 = 卡片 + 四周留白（贴边那一侧不留，见 edgeAdjustedMargin）。
    w->move(cardX - margin.left, cardY - margin.top);
    w->show();
    w->raise();
    return true;
}

void animatePopupIn(QWidget *w)
{
    if (w == nullptr) {
        return;
    }
    auto *fade = new QPropertyAnimation(w, "windowOpacity", w);
    fade->setDuration(tokens::kDurEnter);
    fade->setEasingCurve(QEasingCurve(QEasingCurve::OutCubic));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);

    auto *move = new QPropertyAnimation(w, "geometry", w);
    move->setDuration(tokens::kDurEnter);
    move->setEasingCurve(QEasingCurve(QEasingCurve::InOutQuint));
    const QRect target = w->geometry();
    // 设计稿写的是 translateY(-8px) + scale(.97)。早先把 scale 也做进了 geometry
    // 动画里（起始高度取 0.97 倍），结果每帧都在改窗口高度——Qt 随之重排整个
    // 内容树，文字反复折行、滚动条反复闪，一句话的浮板打开过程看着像在抽搐。
    // scale 是纯视觉变换，Qt 的 Widget 没有 transform 属性可用；这里只保留位移，
    // 位移只动位置不动尺寸，不会触发任何重排。
    move->setStartValue(QRect(target.x(), target.y() - 8, target.width(), target.height()));
    move->setEndValue(target);
    move->start(QAbstractAnimation::DeleteWhenStopped);
}

Panel::Panel(const QString &title, QWidget *parent) : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Popup | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::StrongFocus);

    // 窗口四周的阴影留白。数值见 designtokens.h：按 CSS box-shadow 推，
    // blur 42 的可见扩散约 21px，向下偏移 18px，于是左右上各留 21、下留 39。
    //
    // 「下留 39」是**基准值**：真正用的时候，贴任务栏那一侧会被去掉（见
    // edgeAdjustedMargin），因为 DDE 上 popup 窗口不能越出工作区，留着就等于
    // 让卡片悬在任务栏上方。先记下基准值，popupAt 每次按当前任务栏方向重算。
    baseMargin_ = {tokens::kShadowMarginX, tokens::kShadowMarginTop,
                    tokens::kShadowMarginX, tokens::kShadowMarginBot};
    margin_ = baseMargin_;
    // 窗口比卡片宽两圈留白；卡片本身仍然定宽 520（设计稿的 --w-panel）。
    setFixedWidth(tokens::kPanelWidth + margin_.left + margin_.right);

    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(margin_.left, margin_.top, margin_.right, margin_.bottom);
    root_->setSpacing(0);

    // 卡片：圆角底 + 唯一一条描边 + 顶部高光，全部由 Surface 负责。
    card_ = new Surface(this);
    cardLayout_ = new QVBoxLayout(card_);
    cardLayout_->setContentsMargins(0, 0, 0, 0);
    cardLayout_->setSpacing(0);
    root_->addWidget(card_);

    // 阴影板：投影由它自己画（见 surface.h 的说明）。
    //
    // 设计稿：0 18px 42px rgba(24,30,44,.14)。早先把投影挂在顶层窗口上——
    // Qt 画不出窗口之外的像素，投影被窗口边界切成一圈硬边，浮板看着像贴了
    // 一张纸片；挂到卡片上又会随内容滚动反复重算。
    plate_ = new ShadowPlate(this);
    plate_->setRadius(tokens::kRadiusSurface);
    plate_->setShadow(Theme::instance().palette().shadowPanel, tokens::kShadowPanelR,
                      0, tokens::kShadowPanelY);

    if (!title.isEmpty()) {
        titleBar_ = new QWidget(card_);
        auto *hl = new QHBoxLayout(titleBar_);
        // 左右内边距与内容列一致（都是 14），标题左边缘因此和下面那张卡片
        // 的左边缘对齐——早先标题栏是 12、内容是 14，标题看着比内容往左飘。
        hl->setContentsMargins(tokens::kContentPadding, 0, tokens::kContentPadding, 0);
        hl->setSpacing(tokens::kS2);
        titleLabel_ = new QLabel(title, titleBar_);
        titleLabel_->setFont(titleFont());
        // 不设色的话它继承 QLabel 的 WindowText，比 text1 淡一整档——面板标题
        // 反而不像标题。面板标题是这一屏最重的一档字。
        titleLabel_->setStyleSheet(
            QStringLiteral("color:%1").arg(colorHex(Theme::instance().palette().text1)));
        hl->addWidget(titleLabel_);
        hl->addStretch(1);
        titleRight_ = new QWidget(titleBar_);
        titleRight_->setLayout(new QHBoxLayout(titleRight_));
        ((QHBoxLayout *)titleRight_->layout())->setContentsMargins(0, 0, 0, 0);
        titleRight_->setVisible(false);
        hl->addWidget(titleRight_);
        titleBar_->setFixedHeight(tokens::kTitlebarHeight);
        cardLayout_->addWidget(titleBar_);
        card_->setBandHeight(tokens::kTitlebarHeight);
    }

    auto *scroll = new QScrollArea(card_);
    scroll_ = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 滚动区自己**不能刷底色**。
    //
    // QWidget 默认 autoFillBackground=true，会拿 QPalette::Window 铺一层。那一
    // 档是应用的默认调色板（白），不是 DDE 的面色——于是滚动区把卡片画好的
    // 深色底整个盖成一块白板。浅色主题下两者都是白，看不出来；深色主题下
    // 内容区就是一整块白板，浮板看着像两张皮拼在一起。
    scroll->setAutoFillBackground(false);
    scroll->setAttribute(Qt::WA_TranslucentBackground, true);
    // 视口同理保持透明：底色由内容区自己铺（内容区始终盖住视口，滚动时不会
    // 露出上一次画面的旧像素）。
    scroll->viewport()->setAutoFillBackground(false);

    content_ = new QWidget(scroll);
    // 内容区自己刷页面底色。
    //
    // 这张底色是方的，所以它的下沿必须**让出卡片的圆角**（见 applyContentMargins）：
    // 方形底色盖到卡片下沿，就把两个底角盖成直角，描边是圆的、底是方的——
    // 又是一种「不规则边框」。
    content_->setObjectName(QStringLiteral("panelContent"));
    content_->setAttribute(Qt::WA_StyledBackground, true);
    content_->setAutoFillBackground(false);
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(tokens::kContentPadding, tokens::kContentPadding,
                                       tokens::kContentPadding, tokens::kContentPadding);
    contentLayout_->setSpacing(tokens::kS3);
    scroll->setWidget(content_);
    cardLayout_->addWidget(scroll, 1);

    // 系统主题在运行中被设置中心切走时，浮板自身的外框、投影、底色必须跟着换，
    // 否则里面 Badge / 开关 / 按钮都换成深色了，浮板还顶着一圈浅色描边和白色
    // 内容底——整块面板像坏掉一样。
    applyContentMargins();
    connect(&Theme::instance(), &Theme::changed, this, [this] { restyle(); });
    restyle();
}

void Panel::setTitleBarWidget(QWidget *widget)
{
    if (titleRight_ == nullptr || widget == nullptr) {
        return;
    }
    auto *hl = static_cast<QHBoxLayout *>(titleRight_->layout());
    hl->addWidget(widget);
    titleRight_->setVisible(true);
}

void Panel::setFooterWidget(QWidget *widget)
{
    if (footer_ != nullptr) {
        cardLayout_->removeWidget(footer_);
        footer_->deleteLater();
        footer_ = nullptr;
        card_->setFootHeight(0);
    }
    if (widget == nullptr) {
        restyle();
        return;
    }
    footer_ = widget;
    footer_->setFixedHeight(44);   // 设计稿页脚 10px 上下边距 + 控件高度
    cardLayout_->addWidget(footer_);
    card_->setFootHeight(footer_->height());
    applyContentMargins();
    restyle();
}

// 内容区与滚动区的下边距。
//
// 没有页脚时，底部要**让出卡片圆角那么一条（12px）**：内容区自己刷底色，
// 而那张底色是方的，直接铺到卡片下沿就会把两个底角盖成直角。��出这 12px
// 之后，内容的方角落在卡片直边内侧，看不出来；而最后一行到卡片下沿的
// 距离仍是设计稿的 14px（内容区自己留 2px + 让出的 12px）。
//
// 有页脚时，页脚下沿就是卡片下沿，页脚那条带由卡片自己画（裁在圆角路径里），
// 内容区与滚动区都不用让。
void Panel::applyContentMargins()
{
    if (contentLayout_ == nullptr) {
        return;
    }
    const bool foot = footer_ != nullptr;
    const int bottom = foot ? tokens::kContentPadding
                             : tokens::kContentPadding - tokens::kRadiusSurface;
    contentLayout_->setContentsMargins(tokens::kContentPadding, tokens::kContentPadding,
                                       tokens::kContentPadding, bottom);
    cardLayout_->setContentsMargins(0, 0, 0, foot ? 0 : tokens::kRadiusSurface);
}

void Panel::popupAt(const QPoint &anchor)
{
    // 贴任务栏那一侧不留投影余白（理由见 edgeAdjustedMargin）。余白变了，
    // 窗口根布局的边距要跟着改，否则卡片会被挤在窗口里比该在的位置高/低一截。
    margin_ = edgeAdjustedMargin(baseMargin_);
    root_->setContentsMargins(margin_.left, margin_.top, margin_.right, margin_.bottom);
    placePopup(this, anchor, margin_);
    animatePopupIn(this);
}

QSize Panel::sizeHint() const
{
    // 浮板宽度是 setFixedWidth 定死的，所以内容宽度可以直接算出来，而不是去问
    // content_->width()——首次调用 sizeHint 时浮板还没显示过，那个值是 0，
    // heightForWidth(0) 会把每个可换行标签都当成「窄到每个字一行」来算，
    // 于是浮板凭空高出几百像素（实测设置屏首报 1594、稳定值 1054）。
    // 这里按「浮板宽度 - 内容左右内边距」算，布局状态无关，结果稳定。
    const int contentW = qMax(1, tokens::kPanelWidth - 2 * tokens::kContentPadding);
    int body = 0;
    if (content_ != nullptr && content_->layout() != nullptr) {
        body = content_->layout()->heightForWidth(contentW);
    }
    if (body <= 0) {
        body = content_ != nullptr ? content_->sizeHint().height() : 0;
    }
    const int chrome = (titleBar_ != nullptr ? titleBar_->height() : 0)
                       + (footer_ != nullptr ? footer_->height() : 0);
    const int cardH = qMax(body + chrome, QWidget::sizeHint().height());
    // 返回的是**窗口**尺寸（含四周阴影留白），摆位与限高都按这个来。
    return QSize(tokens::kPanelWidth + margin_.left + margin_.right,
                 cardH + margin_.top + margin_.bottom);
}

void Panel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // 阴影板要压在卡片**下面**：它和卡片画的是同一块圆角底，被卡片整个盖住，
    // 只有溢出卡片的那圈投影露出来。
    syncShadowPlate();
    setFocus(Qt::OtherFocusReason);
}

void Panel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    syncShadowPlate();
}

void Panel::syncShadowPlate()
{
    if (plate_ == nullptr || card_ == nullptr) {
        return;
    }
    // 板子铺满整个窗口：投影要能溢到卡片之外（窗口四周的留白就是给它的）。
    plate_->setGeometry(rect());
    plate_->setCardRect(card_->geometry());
    plate_->lower();
}

bool Panel::eventFilter(QObject *watched, QEvent *event)
{
    // 设计稿：失焦即走。Qt::Popup 本身在多数后端已经会自动关闭，这里兜住
    // Wayland 等不自动关的情况。
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        QTimer::singleShot(0, this, &QWidget::hide);
    }
    return QWidget::eventFilter(watched, event);
}

void Panel::keyPressEvent(QKeyEvent *e)
{
    // Esc 关闭浮板。
    //
    // 「失焦即走」是设计稿定的浮板行为，但它是**鼠标**语义：点了别处，浮板
    // 收回去。纯键盘用户把焦点停在浮板里（Tab 到某个按钮上）时，点不到别处，
    // 于是浮板只能靠「按 Tab 走到最后一个控件再 Tab 出去」这种笨办法收场——
    // 而这里所有控件都在同一个窗口里，Tab 出去就把浮板留着不动。
    //
    // DDE 控制中心、dde-dock 的浮层都是 Esc 关闭，这一条是桌面端的通用约定，
    // 也是无主窗设计里唯一能保证「键盘用户随时能收起浮板」的办法。
    if (e->key() == Qt::Key_Escape) {
        hide();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Panel::restyle()
{
    const Palette &pal = Theme::instance().palette();
    if (plate_ != nullptr) {
        plate_->setShadow(pal.shadowPanel, tokens::kShadowPanelR, 0, tokens::kShadowPanelY);
    }
    if (titleLabel_ != nullptr) {
        titleLabel_->setStyleSheet(QStringLiteral("color:%1").arg(colorHex(pal.text1)));
    }
    if (card_ != nullptr) {
        // 标题栏与页脚都是 **surface-2** 底（设计稿 .titlebar / .panel__foot）：
        // 比外壳底低半档，于是浮板是「外壳白 → 条带灰 → 内容白 → 折叠区灰」。
        // 这四层各差半档，浮板才有「壳 / 条 / 内容 / 卡片」的层次。
        //
        // 早先用 card（浅色下是白），结果浅色主题下标题栏和页脚跟外壳一样白，
        // 三层糊成一层，只剩一条 hairline 分着——浮板看着像没有标题栏。
        card_->setBandColor(pal.surface2);
    }
    if (content_ != nullptr) {
        // 内容区自己刷外壳档底色。
        //
        // 这层底色同时是**消除滚动残影**的关键：视口在滚动时只是把旧像素
        // 往上搬，露出来的那一条得有人重铺。内容区始终盖住整个视口（能滚
        // 的时候它比视口高），所以那一条每次都被重新铺一遍，旧像素不会留
        // 下来——「残影」就是这么没的。
        content_->setStyleSheet(
            QStringLiteral("#panelContent{background:%1;}").arg(colorHex(pal.surface)));
    }
    if (scroll_ != nullptr) {
        // 滚动条样式**下在滚动条自己身上**。
        //
        // 早先是把整段样式表设在 QScrollArea 上，规则写成
        // `QScrollArea QScrollBar::handle:vertical` —— 这种带祖先限定的选择器
        // 在 Qt 里要求 QScrollArea 祖先是**样式表宿主之外**的祖先，而宿主就是
        // 那个 QScrollArea 自己，于是规则一条都没匹配上：滑块退回 DTK 默认样式，
        // 变成一根 10px 宽的灰板子，夹在浮板外边框和内容区之间，像贴了块补丁。
        //
        // 挂在滚动条自己身上就没有这个坑：宿主即滚动条，`QScrollBar::handle`
        // 直接命中。滑块左右各留 3px margin，10px 的槽里露出来的就是设计稿
        // 要的 4px 细药丸。
        scroll_->verticalScrollBar()->setStyleSheet(
            QStringLiteral("QScrollBar:vertical{width:%1px;background:transparent;"
                           "margin:0px;border:none;}"
                           "QScrollBar::handle:vertical{background:%2;border-radius:%3px;"
                           "min-height:32px;margin:0px %4px;}"
                           "QScrollBar::add-line:vertical,"
                           "QScrollBar::sub-line:vertical{height:0px;"
                           "border:none;background:transparent;}"
                           "QScrollBar::add-page:vertical,"
                           "QScrollBar::sub-page:vertical{background:transparent;}")
                .arg(tokens::kScrollbarWidth)
                .arg(colorHex(pal.line3))
                .arg(tokens::pillRadius((tokens::kScrollbarWidth - 4) / 2))
                .arg((tokens::kScrollbarWidth - 4) / 2));
    }
    update();
}

} // namespace fg
