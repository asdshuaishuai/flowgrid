#include "panel.h"
#include "core/designtokens.h"
#include "core/theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFocusEvent>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollArea>
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
    QFont f = QApplication::font();
    f.setPixelSize(tokens::kFont12);
    f.setWeight(QFont::DemiBold);
    return f;
}

} // namespace

bool placePopup(QWidget *w, const QPoint &anchor)
{
    if (w == nullptr) {
        return false;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        w->show();
        return false;
    }
    const QRect avail = screen->availableGeometry();

    // 先按内容算一次实际高度：窗口尚未显示，sizeHint 已可用。
    const int h = qMin(w->sizeHint().height(), avail.height() - tokens::kPanelScreenMargin);
    w->resize(w->width(), h);

    // 锚点一般是托盘图标，其右侧即屏幕右边，所以先按「自身右缘贴锚点」试。
    int x = anchor.x() - w->width();
    int y = anchor.y();
    if (x < avail.left()) {
        x = avail.left() + tokens::kS3;
    }
    if (x + w->width() > avail.right()) {
        x = avail.right() - w->width();
    }
    if (y + w->height() > avail.bottom()) {
        y = avail.bottom() - w->height();
    }
    if (y < avail.top()) {
        y = avail.top() + tokens::kS3;
    }
    w->move(x, y);
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
    setFixedWidth(tokens::kPanelWidth);

    // 设计稿：0 18px 42px rgba(24,30,44,.14)
    shadow_ = new QGraphicsDropShadowEffect(this);
    shadow_->setBlurRadius(tokens::kShadowPanelR);
    shadow_->setOffset(0, tokens::kShadowPanelY);
    shadow_->setColor(Theme::instance().palette().shadowPanel);
    setGraphicsEffect(shadow_);

    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);

    if (!title.isEmpty()) {
        titleBar_ = new QWidget(this);
        auto *hl = new QHBoxLayout(titleBar_);
        hl->setContentsMargins(tokens::kS3, 0, tokens::kS3, 0);
        hl->setSpacing(tokens::kS2);
        titleLabel_ = new QLabel(title, titleBar_);
        titleLabel_->setFont(titleFont());
        hl->addWidget(titleLabel_);
        hl->addStretch(1);
        titleRight_ = new QWidget(titleBar_);
        titleRight_->setLayout(new QHBoxLayout(titleRight_));
        ((QHBoxLayout *)titleRight_->layout())->setContentsMargins(0, 0, 0, 0);
        titleRight_->setVisible(false);
        hl->addWidget(titleRight_);
        titleBar_->setFixedHeight(tokens::kTitlebarHeight);
        root_->addWidget(titleBar_);
    }

    auto *scroll = new QScrollArea(this);
    scroll_ = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 滚动条样式与内容区底色一起在 restyle() 里设（含系统主题变更时的重刷）。
    // 关键：viewport 默认 autoFillBackground=true，会拿 QPalette::Window 自己填一层，
    // 于是 Panel 画好的深色底被白色盖住——浅色主题下看不出来（都是白的），
    // 深色主题下内容区就是一整块白板。必须显关掉。
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setAttribute(Qt::WA_TranslucentBackground, false);

    content_ = new QWidget(scroll);
    // 内容区自己刷 surface 底色。早先是靠 Panel::paintEvent 那张圆角矩形
    // 「透」上来的，实际测下来这块一直是全透明的（截图 alpha=0）：深色主题下
    // 透明和 surface-2 差不了多少，肉眼看不出来；一换浅色主题整块浮板直接
    // 没有底。子控件不透传、滚动区也不画底，能兜住底色的就只剩内容区自己。
    content_->setObjectName(QStringLiteral("panelContent"));
    content_->setAttribute(Qt::WA_StyledBackground, true);
    content_->setAutoFillBackground(false);
    restyle();
    auto *cl = new QVBoxLayout(content_);
    cl->setContentsMargins(tokens::kContentPadding, tokens::kContentPadding,
                            tokens::kContentPadding, tokens::kContentPadding);
    cl->setSpacing(tokens::kS3);
    scroll->setWidget(content_);
    root_->addWidget(scroll, 1);

    // 系统主题在运行中被设置中心切走时，浮板自身的外框、投影、底色必须跟着换，
    // 否则里面 Badge / 开关 / 按钮都换成深色了，浮板还顶着一圈浅色描边和白色
    // 内容底——整块面板像坏掉一样。paintChrome 本来就是每帧现取调色板的，
    // 真正卡住的是这里烘死进样式表和阴影里的几个颜色。
    connect(&Theme::instance(), &Theme::changed, this, [this] { restyle(); });
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
        root_->removeWidget(footer_);
        footer_->deleteLater();
        footer_ = nullptr;
    }
    if (widget == nullptr) {
        return;
    }
    footer_ = widget;
    footer_->setFixedHeight(44);   // 设计稿页脚 10px 上下边距 + 控件高度
    root_->addWidget(footer_);
}

void Panel::popupAt(const QPoint &anchor)
{
    placePopup(this, anchor);
    animatePopupIn(this);
}

QSize Panel::sizeHint() const
{
    // 浮板宽度是 setFixedWidth 定死的，所以内容宽度可以直接算出来，而不是去问
    // content_->width()——首次调用 sizeHint 时浮板还没显示过，那个值是 0，
    // heightForWidth(0) 会把每个可换行标签都当成「窄到每个字一行」来算，
    // 于是浮板凭空高出几百像素（实测设置屏首报 1594、稳定值 1054）。
    // 这里按「浮板宽度 - 内容左右内边距」算，布局状态无关，结果稳定。
    const int w = qMax(1, width());
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
    return QSize(w, qMax(body + chrome, QWidget::sizeHint().height()));
}

void Panel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    setFocus(Qt::OtherFocusReason);
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

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    paintChrome(p);
}

void Panel::restyle()
{
    if (shadow_ != nullptr) {
        shadow_->setColor(Theme::instance().palette().shadowPanel);
    }
    if (content_ == nullptr) {
        return;
    }
    // 左右各留 1px 描边：内容铺满整宽，不自己描边会把面板那圈 1px 边框盖掉
    // （CSS 里子元素是裁在 padding box 内的，Qt 没有这个盒模型）。
    const Palette &p = Theme::instance().palette();
    content_->setStyleSheet(
        QStringLiteral("#panelContent{border-left:1px solid %1;border-right:1px solid %1;"
                       "background:%2;}")
            .arg(colorHex(p.line2), colorHex(p.surface)));
    if (scroll_ != nullptr) {
        // 设计稿 ui.css .content::-webkit-scrollbar：10px 宽、line-3 色、pill 形，
        // 靠 3px 透明边框把滑块收成 4px 的细条。DTK 自带的是一根 8px 亮灰粗条，
        // 深色浮板上比稿子亮一大截，浅色下又压过卡片描边，所以按稿子显式画一根；
        // 行末的箭头按钮一并去掉——设计稿没有。
        //
        // 「3px 透明边框」在 Qt 里不能用 handle 的 margin 收窄：一旦给滑块加了
        // margin，Qt 的滚动条样式引擎会把整根条判成无效直接不画（实测滑块连轨道
        // 一起消失，右边缘只剩面板描边）。改用等价的 background-clip:content-box
        // + 透明 border，视觉效果一致且引擎认。
        //
        // 圆角是 --r-pill：对 kScrollbarWidth 宽的条即「宽的一半」。这里原先写死
        // 5px，而宽度是另一处字面量 10——两者是「10÷2」的两半，宽度一改圆角就不
        // 跟着变，药丸直接破形。交给 pillRadius() 从宽度推导。
        scroll_->setStyleSheet(
            QStringLiteral("QScrollArea{background:transparent;}"
                           "QScrollArea QScrollBar:vertical{width:%1px;background:transparent;"
                           "margin:0px;border:none;}"
                           "QScrollArea QScrollBar::handle:vertical{background:%2;"
                           "background-clip:content-box;border:3px solid transparent;"
                           "border-radius:%3px;min-height:32px;}"
                           "QScrollArea QScrollBar::add-line:vertical,"
                           "QScrollArea QScrollBar::sub-line:vertical{height:0px;"
                           "border:none;background:transparent;}"
                           "QScrollArea QScrollBar::add-page:vertical,"
                           "QScrollArea QScrollBar::sub-page:vertical{background:transparent;}")
                .arg(tokens::kScrollbarWidth)
                .arg(colorHex(p.line3))
                .arg(tokens::pillRadius(tokens::kScrollbarWidth)));
    }
    update();
}

void Panel::paintChrome(QPainter &p)
{
    const Palette &pal = Theme::instance().palette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = tokens::kRadiusSurface;

    QPainterPath path;
    path.addRoundedRect(r, radius, radius);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(pal.line2, 1));
    p.setBrush(pal.surface);
    p.drawPath(path);

    // 标题栏底色（surface-2）+ 与内容之间的 hairline 分隔
    if (titleBar_ != nullptr && titleBar_->isVisible()) {
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(0, 0, width(), tokens::kTitlebarHeight), pal.surface2);
        p.setPen(QPen(pal.hairline, 1));
        p.drawLine(0, tokens::kTitlebarHeight, width(), tokens::kTitlebarHeight);
        p.restore();
    }

    // 页脚同理
    if (footer_ != nullptr) {
        const QRect footRect(0, height() - footer_->height(), width(), footer_->height());
        p.save();
        p.setClipPath(path);
        p.fillRect(footRect, pal.surface2);
        p.setPen(QPen(pal.hairline, 1));
        p.drawLine(footRect.top(), footRect.top(), width(), footRect.top());
        p.restore();
    }

    // 设计稿的「精致度打磨」：顶部 1px 内高光，叠加在边框之上而非替换它。
    p.save();
    p.setClipPath(path);
    QLinearGradient edge(0, 0, 0, 2);
    edge.setColorAt(0.0, pal.edgeHi);
    edge.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(1, 1, width() - 2, 2), edge);
    p.restore();
}

} // namespace fg
