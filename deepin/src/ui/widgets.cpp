#include "widgets.h"
#include "icons.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QActionGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace fg {

void applyBoxStyle(QWidget *w, const QString &declarations)
{
    if (w == nullptr) {
        return;
    }
    // 自增名字保证同一屏里多个容器不会互相命中；销毁重建的容器也不会撞名。
    static int seq = 0;
    if (w->objectName().isEmpty()) {
        w->setObjectName(QStringLiteral("fgBox%1").arg(++seq));
    }
    w->setStyleSheet(QStringLiteral("#%1 { %2 }").arg(w->objectName(), declarations));
}


namespace {

QFont uiFont(int px, int weight = QFont::Normal)
{
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

// 与面板侧同一个坑：柔和底色靠 alpha 叠出来，HexRgb 会把它压成实色。
QString cssColor(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

void clearLayout(QLayout *layout)
{
    if (layout == nullptr) {
        return;
    }
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        if (QLayout *child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

QFont uiMonoFont(int px, int weight)
{
    QFont f = uiFont(px, weight);
    f.setFamilies({QStringLiteral("Noto Sans Mono"), QStringLiteral("DejaVu Sans Mono"),
                   QStringLiteral("monospace")});
    f.setFixedPitch(true);
    return f;
}

// ── StatusDot ─────────────────────────────────────────────────────────
StatusDot::StatusDot(QWidget *parent) : QWidget(parent)
{
    setFixedSize(tokens::kStatusDotSize, tokens::kStatusDotSize);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    solid_ = Theme::instance().color(Quality::Good).solid;

    // 设计稿 pulse 2s infinite，用 QTimeLine 表达比 QPropertyAnimation 更直白：
    // 0→1 全不透明，50% 处降到 0.4，来回。
    anim_ = new QPropertyAnimation(this, "pulse", this);
    anim_->setDuration(2000);
    anim_->setStartValue(1.0);
    anim_->setKeyValueAt(0.5, 0.4);
    anim_->setEndValue(1.0);
    anim_->start();
}

void StatusDot::setQuality(Quality q)
{
    solid_ = Theme::instance().color(q).solid;
    update();
}

void StatusDot::setPulsing(bool on)
{
    if (on) {
        if (anim_->state() != QAbstractAnimation::Running) {
            anim_->start();
        }
    } else {
        anim_->stop();
        pulse_ = 1.0;
        update();
    }
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    QColor c = solid_;
    c.setAlphaF(pulse_);
    p.setBrush(c);
    const qreal r = tokens::kStatusDotSize / 2.0;
    // 显式给 QPointF：否则 QPoint + qreal 会同时匹配 int / qreal 两个重载而报二义。
    p.drawEllipse(QPointF(rect().center()), r, r);
}

// ── Badge ─────────────────────────────────────────────────────────────
Badge::Badge(const QString &text, Variant variant, QWidget *parent)
    : QWidget(parent), text_(text), variant_(variant)
{
    setFixedHeight(tokens::kBadgeHeight);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    refresh();
    setMinimumWidth(sizeHint().width());
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        refresh();
        update();
    });
}

void Badge::refresh()
{
    const Palette &p = Theme::instance().palette();
    switch (variant_) {
    case Variant::Accent:   bg_ = p.accentSoft; fg_ = p.accentText; break;
    case Variant::Neutral:  bg_ = p.surface3;   fg_ = p.text2;      break;
    case Variant::Ok:       bg_ = p.ok.soft;    fg_ = p.ok.text;    break;
    case Variant::Warn:     bg_ = p.warn.soft;  fg_ = p.warn.text;  break;
    case Variant::Danger:   bg_ = p.danger.soft;fg_ = p.danger.text;break;
    case Variant::Idle:     bg_ = p.idle.soft;  fg_ = p.idle.text;  break;
    }
}

void Badge::setVariant(Variant v)
{
    variant_ = v;
    refresh();
    updateGeometry();
    update();
}

void Badge::setText(const QString &text)
{
    text_ = text;
    // 徽章放进 QHBoxLayout + addStretch 的组合里会被压成省略号，
    // 所以把 sizeHint 钉成下限，而不是只 updateGeometry。
    setMinimumWidth(sizeHint().width());
    updateGeometry();
    update();
}

QSize Badge::sizeHint() const
{
    const QFontMetrics fm(uiFont(tokens::kFont11, QFont::DemiBold));
    // 左右各留 1px：徽章宽度取整后恰好等于文字宽度时，某些 DPI 下会被
    // 舍入成差 1px，于是 elide 出一个省略号。
    return QSize(fm.horizontalAdvance(text_) + tokens::kBadgeHoriPad * 2 + 2,
                 tokens::kBadgeHeight);
}

// 文字变化后要把最小宽度一起改，所以覆写 changeEvent 统一走 setText 的效果。


void Badge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(bg_);
    p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    p.setPen(fg_);
    p.setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    p.drawText(rect(), Qt::AlignCenter, text_);
}

// ── ToggleSwitch ──────────────────────────────────────────────────────
ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
    setFixedSize(tokens::kSwitchWidth, tokens::kSwitchHeight);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &ToggleSwitch::toggled, this, [this] { update(); });
}

void ToggleSwitch::setChecked(bool on)
{
    if (checked_ != on) {
        checked_ = on;
        knob_ = on ? 1.0 : 0.0;
        update();
    }
}

void ToggleSwitch::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent *e)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QColor track = checked_ ? p.ok.solid : p.surface3;
    if (hovered_) {
        track = checked_ ? track.lighter(110) : p.surface3.lighter(112);
    }
    pt.setPen(Qt::NoPen);
    pt.setBrush(track);
    pt.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);

    const qreal pad = 2.0;
    const qreal travel = width() - tokens::kSwitchKnob - pad * 2;
    const qreal x = pad + knob_ * travel;
    pt.setBrush(QColor(Qt::white));
    pt.drawEllipse(QRectF(x, pad, tokens::kSwitchKnob, tokens::kSwitchKnob));

    if (hasFocus()) {
        QColor ring = p.accent;
        ring.setAlpha(200);
        pt.setPen(QPen(ring, 2));
        pt.setBrush(Qt::NoBrush);
        pt.drawRoundedRect(rect().adjusted(-2, -2, 2, 2), height() / 2.0 + 2, height() / 2.0 + 2);
    }
}

// ── LatencyChip ───────────────────────────────────────────────────────
LatencyChip::LatencyChip(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(tokens::kStatusPillHeight + 8);
    connect(&Theme::instance(), &Theme::changed, this, [this] { update(); });
}

void LatencyChip::setLatency(double ms, bool online)
{
    quality_ = Theme::qualityOf(ms, online);
    text_ = online ? QStringLiteral("%1ms").arg(ms, 0, 'f', 1) : QStringLiteral("—");
    update();
}

QString LatencyChip::text() const
{
    return text_;
}

QSize LatencyChip::sizeHint() const
{
    return {tokens::kLatChipMinWidth, height()};
}

void LatencyChip::paintEvent(QPaintEvent *)
{
    const StatusColor sc = Theme::instance().color(quality_);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(sc.soft);
    p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    p.setPen(sc.text);
    p.setFont(uiFont(tokens::kFont12, QFont::Bold));
    p.drawText(rect(), Qt::AlignCenter, text_);
}

// ── Collapsible ───────────────────────────────────────────────────────
Collapsible::Collapsible(const QString &title, const QString &iconName, QWidget *body,
                         QWidget *badgeHost, QWidget *parent)
    : QFrame(parent), body_(body)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    head_ = new QWidget(this);
    auto *hl = new QHBoxLayout(head_);
    hl->setContentsMargins(tokens::kS3, 9, tokens::kS3, 9);
    hl->setSpacing(tokens::kS2);

    if (!iconName.isEmpty()) {
        auto *ic = new QLabel(head_);
        ic->setFixedSize(16, 16);
        ic->setPixmap(icons::icon(iconName, 16, Theme::instance().palette().text2).pixmap(16, 16));
        hl->addWidget(ic);
    }

    auto *titleLabel = new QLabel(title, head_);
    titleLabel->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    titleLabel->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(Theme::instance().palette().text1)));
    hl->addWidget(titleLabel);

    if (badgeHost != nullptr) {
        hl->addWidget(badgeHost);
    }
    hl->addStretch(1);

    chevron_ = new QLabel(head_);
    chevron_->setFixedSize(16, 16);
    hl->addWidget(chevron_);

    root->addWidget(head_);
    root->addWidget(body_);

    setCollapsed(false);
}

void Collapsible::setCollapsed(bool collapsed)
{
    collapsed_ = collapsed;
    body_->setVisible(!collapsed);
    // 设计稿是 chevron rotate(-90deg)；这里换用右向图标表达同一状态，
    // 比为一个 90° 旋转再引一套变换动画更省，且在任何后端上表现一致。
    if (chevron_ != nullptr) {
        const QString name = collapsed ? QStringLiteral("chevronRight")
                                       : QStringLiteral("chevronDown");
        chevron_->setPixmap(
            icons::icon(name, 16, Theme::instance().palette().text3).pixmap(16, 16));
    }
    updateGeometry();
}

void Collapsible::mouseReleaseEvent(QMouseEvent *e)
{
    if (head_->geometry().contains(e->position().toPoint())) {
        setCollapsed(!collapsed_);
    }
    QFrame::mouseReleaseEvent(e);
}

// ── BarChart ──────────────────────────────────────────────────────────
BarChart::BarChart(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(tokens::kChartHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BarChart::pushSample(double ms)
{
    samples_.push_back(ms);
    while (samples_.size() > tokens::kChartBars) {
        samples_.erase(samples_.begin());
    }
    update();
}

void BarChart::clear()
{
    samples_.clear();
    update();
}

void BarChart::prime(double seed)
{
    if (seed <= 0.0) {
        return;
    }
    samples_.clear();
    // 抖动带 -600~1400µs 与 devicemanager.cpp 的 mock 源保持一致，
    // 这样首屏形状和随后到达的真实采样是连续的，不会突然「跳」一下。
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < tokens::kChartBars; ++i) {
        const double jitterMs = rng->bounded(-600, 1400) / 1000.0;
        samples_.push_back(qMax(0.5, seed + jitterMs));
    }
    update();
}

void BarChart::paintEvent(QPaintEvent *)
{
    if (samples_.empty()) {
        return;
    }
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    // 峰值取样本最大值，但至少按 5ms 归一：否则同一链路上几个几乎相同的
    // 采样会被放大成满格起伏，看着剧烈其实没变。
    double peak = 5.0;
    for (double v : samples_) {
        peak = qMax(peak, v);
    }
    const qreal gap = 4.0;
    const qreal barW =
        qMax<qreal>(3.0, (width() - gap * (samples_.size() - 1)) / samples_.size());
    // 设计稿 ui.css .chart：内容盒 = 70 - padding(12 上 / 8 下) = 50px，
    // 样例数据的柱高在 22~44px 之间波动。这里照抄这个区间而不是按比例缩满，
    // 否则矮柱会被拉成近乎正方的色块。
    const qreal usable = height() - tokens::kS3 - tokens::kS2;
    const qreal maxBar = qMin(44.0, usable);
    const qreal minBar = qMin(22.0, maxBar * 0.5);

    for (int i = 0; i < static_cast<int>(samples_.size()); ++i) {
        const qreal ratio = qBound(0.0, samples_[i] / peak, 1.0);
        const qreal h = minBar + (maxBar - minBar) * ratio;
        const qreal x = i * (barW + gap);
        const qreal y = height() - h;
        pt.setPen(Qt::NoPen);
        pt.setBrush(static_cast<size_t>(i) + 1 == samples_.size() ? p.accent : p.accentSoft);

        // 设计稿是 border-radius: pill pill 0 0（两个上圆角 + 两个下直角）。
        // 早先用 addRoundedRect 再 addRect 去「削平底部」，但 QPainterPath 默认
        // OddEven 填充，两段一交叠就把交叠区挖成空洞，矮柱子直接变成白色透镜。
        // 这里改成用 arcTo 直接描出上圆下平的轮廓，柱子高矮都不会退化。
        const qreal r = qMin(barW / 2.0, h);
        QPainterPath path;
        path.moveTo(x, y + h);
        path.lineTo(x, y + r);
        path.arcTo(QRectF(x, y, r, r), 180, -90);
        path.lineTo(x + barW - r, y);
        path.arcTo(QRectF(x + barW - r, y, r, r), 90, -90);
        path.lineTo(x + barW, y + h);
        path.closeSubpath();
        pt.drawPath(path);
    }
}

// ── ProgressTrack ─────────────────────────────────────────────────────
ProgressTrack::ProgressTrack(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kTrackHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ProgressTrack::setProgress(int pct)
{
    pct = qBound(0, pct, 100);
    if (pct == progress_) {
        return;
    }
    // 设计稿的 .bar__fill 是 width 上的 150ms 过渡；这里用属性动画对齐，
    // 免得每次进度跳变都是硬切。
    auto *anim = new QPropertyAnimation(this, "progress", this);
    anim->setDuration(tokens::kDurMicro);
    anim->setStartValue(progress_);
    anim->setEndValue(pct);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    progress_ = pct;
}

void ProgressTrack::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);
    pt.setPen(Qt::NoPen);

    const qreal r = height() / 2.0;
    const QRectF track(0, 0, width(), height());
    pt.setBrush(p.surface3);
    pt.drawRoundedRect(track, r, r);

    // 填充宽度在 paint 时按当前 progress_ 取：动画每帧改一次属性，
    // 自然触发重绘，无需自己连 update()。
    const qreal w = width() * progress_ / 100.0;
    if (w > 0.0) {
        pt.setBrush(p.accent);
        pt.drawRoundedRect(QRectF(0, 0, qMax<qreal>(w, height()), height()), r, r);
    }
}

// ── FgButton ──────────────────────────────────────────────────────────
FgButton::FgButton(const QString &text, Variant variant, bool small, QWidget *parent)
    : QAbstractButton(parent), text_(text), variant_(variant), small_(small)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    refreshColors();
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        refreshColors();
        update();
    });
}

void FgButton::refreshColors()
{
    const Palette &p = Theme::instance().palette();
    switch (variant_) {
    case Variant::Accent:    bg_ = p.accent;          fg_ = p.onAccent; break;
    case Variant::Secondary: bg_ = p.surface;         fg_ = p.text1;    break;
    case Variant::Ghost:     bg_ = QColor(Qt::transparent); fg_ = p.text2; break;
    case Variant::Danger:    bg_ = p.danger.solid;    fg_ = QColor(Qt::white); break;
    }
}

void FgButton::setIcon(const QString &iconName)
{
    if (iconName_ == iconName) {
        return;
    }
    iconName_ = iconName;
    updateGeometry();   // 宽度依赖图标是否存在，必须让布局重算
}

QSize FgButton::sizeHint() const
{
    const QFontMetrics fm(uiFont(small_ ? tokens::kFont11 : tokens::kFont12, QFont::DemiBold));
    const int h = small_ ? 24 : tokens::kControlHeight;
    const int gap = iconName_.isEmpty() ? 0 : (small_ ? 5 : tokens::kS1);
    const int iconW = iconName_.isEmpty() ? 0 : (small_ ? 13 : 14);
    return QSize(fm.horizontalAdvance(text_) + iconW + gap + (small_ ? tokens::kS2 : tokens::kS3) * 2,
                 h);
}

void FgButton::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void FgButton::leaveEvent(QEvent *e)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void FgButton::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QColor bg = bg_;
    if (variant_ == Variant::Secondary) {
        bg = isDown() ? p.surfacePress : (hovered_ ? p.surfaceHi : p.surface);
    } else if (hovered_ && isDown() && bg.isValid()) {
        bg = bg.darker(108);
    } else if (hovered_ && bg.isValid()) {
        bg = bg.lighter(108);
    }

    pt.setBrush(bg);
    pt.setPen(variant_ == Variant::Secondary ? QPen(p.line2, 1) : QPen(Qt::NoPen));
    pt.drawRoundedRect(rect().adjusted(0, 0, -1, -1), tokens::kRadiusCtl, tokens::kRadiusCtl);

    pt.setPen(fg_);
    const QFont f = uiFont(small_ ? tokens::kFont11 : tokens::kFont12, QFont::DemiBold);
    pt.setFont(f);
    if (iconName_.isEmpty()) {
        pt.drawText(rect(), Qt::AlignCenter, text_);
        return;
    }

    // 「图标 + 文字」并排：先量出整块宽度再居中，避免两段各自居中导致错位。
    const int iconSize = small_ ? 13 : 14;
    const int gap = small_ ? 5 : tokens::kS1;
    const QFontMetrics fm(f);
    const int total = iconSize + gap + fm.horizontalAdvance(text_);
    const int x0 = (width() - total) / 2;
    const int y0 = (height() - iconSize) / 2;
    pt.drawPixmap(QPoint(x0, y0), icons::icon(iconName_, iconSize, fg_).pixmap(iconSize, iconSize));
    pt.drawText(QRect(x0 + iconSize + gap, 0, fm.horizontalAdvance(text_) + 2, height()),
                Qt::AlignVCenter | Qt::AlignLeft, text_);
}

// ── IconButton ────────────────────────────────────────────────────────
IconButton::IconButton(const QString &iconName, const QString &tooltip, QWidget *parent)
    : QAbstractButton(parent), iconName_(iconName)
{
    setFixedSize(tokens::kIconButtonSize, tokens::kIconButtonSize);
    setCursor(Qt::PointingHandCursor);
    if (!tooltip.isEmpty()) {
        setToolTip(tooltip);
    }
}

void IconButton::setIcon(const QString &iconName)
{
    if (iconName_ != iconName) {
        iconName_ = iconName;
        update();
    }
}

void IconButton::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void IconButton::leaveEvent(QEvent *e)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void IconButton::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);
    if (hovered_) {
        pt.setPen(Qt::NoPen);
        pt.setBrush(p.surfaceHi);
        pt.drawRoundedRect(rect(), tokens::kRadiusCtl, tokens::kRadiusCtl);
    }
    const int s = tokens::kIconButtonSize - 6;
    pt.drawPixmap((width() - s) / 2, (height() - s) / 2,
                  icons::icon(iconName_, s, hovered_ ? p.text1 : p.text3).pixmap(s, s));
}

// ── DeviceCard ────────────────────────────────────────────────────────
DeviceCard::DeviceCard(const Device &device, QWidget *parent) : QFrame(parent)
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    grid->setHorizontalSpacing(tokens::kS3);
    grid->setVerticalSpacing(0);
    grid->setColumnStretch(1, 1);

    icon_ = new QLabel(this);
    icon_->setFixedSize(tokens::kDeviceIconSize, tokens::kDeviceIconSize);
    icon_->setAlignment(Qt::AlignCenter);
    icon_->setFont(uiFont(tokens::kFont12, QFont::Bold));

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    name_ = new QLabel(this);
    name_->setFont(uiFont(tokens::kFont13, QFont::DemiBold));
    meta_ = new QLabel(this);
    meta_->setFont(uiFont(tokens::kFont11));
    meta_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(Theme::instance().palette().text3)));
    textCol->addWidget(name_);
    textCol->addWidget(meta_);

    latency_ = new LatencyChip(this);
    action_ = new IconButton(QStringLiteral("arrowRight"), tr("切换到此设备"), this);
    connect(action_, &IconButton::clicked, this, [this] {
        if (actionKind_ == QLatin1String("switch")) {
            Q_EMIT switchRequested(id_);
        } else if (!actionKind_.isEmpty() && actionKind_ != QLatin1String("none")) {
            Q_EMIT actionRequested(id_, actionKind_);
        }
    });

    grid->addWidget(icon_, 0, 0);
    grid->addLayout(textCol, 0, 1);
    grid->addWidget(latency_, 0, 2, Qt::AlignVCenter);
    grid->addWidget(action_, 0, 3);

    rebuild(device);
}

void DeviceCard::rebuild(const Device &device)
{
    id_ = device.id;
    device_ = device;
    active_ = device.isActive;
    online_ = device.online;

    const Palette &p = Theme::instance().palette();
    // 两字标识由设备自己带（设计稿里是 MB/PC/LX/TP 这类人工指定值）；
    // 缺失时才退回首词，否则 "Windows 11 Desktop" 会退化成毫无含义的 "WI"。
    QString abbrev = device.abbrev;
    if (abbrev.isEmpty()) {
        abbrev = device.name.section(QLatin1Char(' '), 0, 0).left(2).toUpper();
    }

    icon_->setText(abbrev);
    if (online_) {
        icon_->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                 .arg(cssColor(p.accentSoft), cssColor(p.accentText))
                                 .arg(tokens::kRadiusCard));
    } else {
        icon_->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:%3px")
                                 .arg(cssColor(p.surface3), cssColor(p.text3))
                                 .arg(tokens::kRadiusCard));
    }

    name_->setText(device.name);
    name_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text1)));

    QString meta = device.online ? device.platformLabel : tr("离线");
    if (device.online) {
        meta += QStringLiteral(" · ") + linkName(device.link);
        if (device.isActive) {
            meta += tr(" · 当前控制端");
        }
    }
    meta_->setText(meta);
    latency_->setLatency(device.latencyMs, device.online);

    if (!online_) {
        setAction(QStringLiteral("reconnect"));
        action_->setToolTip(tr("重连"));
    } else if (active_) {
        setAction(QStringLiteral("more"));
        action_->setToolTip(tr("更多信息"));
    } else {
        setAction(QStringLiteral("switch"));
        action_->setToolTip(tr("切换到此设备"));
    }
    update();
}

void DeviceCard::setDevice(const Device &device)
{
    rebuild(device);
}

void DeviceCard::setAction(const QString &kind)
{
    actionKind_ = kind;
    action_->setVisible(kind != QLatin1String("none"));
    if (kind == QLatin1String("switch")) {
        action_->setIcon(QStringLiteral("arrowRight"));
    } else if (kind == QLatin1String("reconnect")) {
        action_->setIcon(QStringLiteral("refresh"));
    } else if (kind == QLatin1String("more")) {
        action_->setIcon(QStringLiteral("more"));
    }
}

void DeviceCard::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QFrame::enterEvent(e);
}

void DeviceCard::leaveEvent(QEvent *e)
{
    hovered_ = false;
    update();
    QFrame::leaveEvent(e);
}

void DeviceCard::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QColor bg = active_ ? p.accentSoft : p.surface;
    if (hovered_ && !active_) {
        bg = p.surface;
    }
    pt.setPen(Qt::NoPen);
    pt.setBrush(bg);
    pt.drawRoundedRect(rect().adjusted(0, 0, -1, -1), tokens::kRadiusCard, tokens::kRadiusCard);

    QPen border(active_ ? p.accentLine : (hovered_ ? p.line3 : p.line2));
    border.setWidthF(1.0);
    pt.setPen(border);
    pt.setBrush(Qt::NoBrush);
    pt.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), tokens::kRadiusCard,
                       tokens::kRadiusCard);

    if (!online_) {
        pt.setPen(QColor(0, 0, 0, 90));   // .device--off opacity: .6 的近似
    }
}

// ── NavRow ────────────────────────────────────────────────────────────
NavRow::NavRow(const QString &iconName, const QString &label, QWidget *trailing, QWidget *parent)
    : QWidget(parent), iconName_(iconName), label_(label), trailing_(trailing)
{
    setCursor(Qt::PointingHandCursor);

    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(tokens::kS3, 7, tokens::kS3, 7);
    hl->setSpacing(tokens::kS2);

    iconLabel_ = new QLabel(this);
    iconLabel_->setFixedSize(16, 16);
    hl->addWidget(iconLabel_);

    labelWidget_ = new QLabel(label_, this);
    hl->addWidget(labelWidget_);

    hl->addStretch(1);
    if (trailing_ != nullptr) {
        hl->addWidget(trailing_);
    }

    repaintChildren(Theme::instance().palette());
}

void NavRow::setLabel(const QString &label)
{
    label_ = label;
    labelWidget_->setText(label);
}

void NavRow::setDanger(bool danger)
{
    danger_ = danger;
    repaintChildren(Theme::instance().palette());
}

void NavRow::repaintChildren(const Palette &p)
{
    iconLabel_->setPixmap(icons::icon(iconName_, 16, danger_ ? p.danger.text : p.text2)
                              .pixmap(16, 16));
    labelWidget_->setFont(uiFont(tokens::kFont12));
    labelWidget_->setStyleSheet(
        QStringLiteral("color:%1").arg(cssColor(danger_ ? p.danger.text : p.text1)));
}

void NavRow::mousePressEvent(QMouseEvent *)
{
    pressed_ = true;
    update();
}

void NavRow::mouseReleaseEvent(QMouseEvent *e)
{
    if (pressed_ && rect().contains(e->position().toPoint())) {
        Q_EMIT clicked();
    }
    pressed_ = false;
    update();
}

void NavRow::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(e);
}

void NavRow::leaveEvent(QEvent *e)
{
    hovered_ = false;
    pressed_ = false;
    update();
    QWidget::leaveEvent(e);
}

void NavRow::paintEvent(QPaintEvent *)
{
    if (!hovered_ && !pressed_) {
        return;   // 常态透明，省一次重绘
    }
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);
    pt.setPen(Qt::NoPen);
    pt.setBrush(pressed_ ? p.surfacePress : p.surfaceHi);
    pt.drawRect(rect());
}

// ── ElidedLabel ──────────────────────────────────────────────────────
ElidedLabel::ElidedLabel(const QString &text, QWidget *parent) : QWidget(parent), full_(text)
{
    // 横向 Ignored 是这个控件能用的前提，见 widgets.h 的说明。
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TranslucentBackground);
    QFont f = uiFont(tokens::kFont12);
    f.setPixelSize(tokens::kFont12);
    setFont(f);
}

void ElidedLabel::setFullText(const QString &text)
{
    if (full_ == text) {
        return;
    }
    full_ = text;
    update();
}

void ElidedLabel::setAlignmentFlag(Qt::Alignment a)
{
    align_ = a;
    update();
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetricsF fm(font());
    return {qCeil(fm.horizontalAdvance(full_)), qCeil(fm.height())};
}

void ElidedLabel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    update();
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter pt(this);
    pt.setPen(palette().color(QPalette::WindowText));
    pt.setFont(font());
    // 宽度为负会抛断言，构造期与隐藏态都可能拿到 0，直接跳过。
    if (width() <= 0) {
        return;
    }
    pt.drawText(rect(), int(align_), fontMetrics().elidedText(full_, Qt::ElideRight, width()));
}

// ── ClipRow ───────────────────────────────────────────────────────────
ClipRow::ClipRow(const QString &iconName, const QString &text, const QString &hint,
                 QWidget *trailing, QWidget *parent, QWidget *leading)
    : QWidget(parent), leading_(leading), trailing_(trailing)
{
    setCursor(Qt::PointingHandCursor);
    const Palette &p = Theme::instance().palette();

    // ui.css .list__row：padding 10px var(--s-3)、gap var(--s-3)；
    // .list__label：竖排 gap 2；.list__hint：t-11 / text-3。
    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(tokens::kS3, 10, tokens::kS3, 10);
    hl->setSpacing(tokens::kS3);

    if (leading_ == nullptr) {
        // .clip__type：18×18 / r 4 / surface-3 底 / text-2 图标。
        leading_ = new QWidget(this);
        auto *tl = new QHBoxLayout(leading_);
        tl->setContentsMargins(0, 0, 0, 0);
        auto *iconLabel = new QLabel(leading_);
        iconLabel->setFixedSize(tokens::kBadgeHeight, tokens::kBadgeHeight);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(icons::icon(iconName, 12, p.text2).pixmap(12, 12));
        tl->addWidget(iconLabel);
        applyBoxStyle(leading_,
                      QStringLiteral("border-radius:4px;background:%1").arg(cssColor(p.surface3)));
    } else {
        leading_->setParent(this);
    }
    hl->addWidget(leading_, 0, Qt::AlignVCenter);

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    auto *title = new ElidedLabel(text, this);
    title->setFont(uiFont(tokens::kFont12));
    title->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text1)));
    col->addWidget(title);
    auto *hintLabel = new ElidedLabel(hint, this);
    hintLabel->setFont(uiFont(tokens::kFont11));
    hintLabel->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    col->addWidget(hintLabel);
    hl->addLayout(col, 1);

    if (trailing_ != nullptr) {
        hl->addWidget(trailing_, 0, Qt::AlignVCenter);
    }
}

void ClipRow::mousePressEvent(QMouseEvent *)
{
    pressed_ = true;
    update();
}

void ClipRow::mouseReleaseEvent(QMouseEvent *e)
{
    if (pressed_ && rect().contains(e->position().toPoint())) {
        Q_EMIT clicked();
    }
    pressed_ = false;
    update();
}

void ClipRow::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(e);
}

void ClipRow::leaveEvent(QEvent *e)
{
    hovered_ = false;
    pressed_ = false;
    update();
    QWidget::leaveEvent(e);
}

void ClipRow::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    if (hovered_ || pressed_) {
        pt.setPen(Qt::NoPen);
        pt.setBrush(pressed_ ? p.surfacePress : p.surfaceHi);
        pt.drawRect(rect());
    }
    // .list__row 的行间分隔：1px hairline。末行多一道线比少一道更难看出差别，
    // 这里统一画满宽，不去为「是不是最后一行」再存一个状态。
    pt.setPen(p.hairline);
    pt.drawLine(0, height() - 1, width() - 1, height() - 1);
}

// ── ListRow ───────────────────────────────────────────────────────────
ListRow::ListRow(const QString &label, const QString &hint, QWidget *control, QWidget *parent,
                 QWidget *titleWidget)
    : QFrame(parent)
{
    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(tokens::kS3, 9, tokens::kS3, 9);
    hl->setSpacing(tokens::kS3);

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    // titleWidget 非空时用它顶替纯文本主标题——设备列表那几行要带类型图标
    // （设计稿 pairing.html 的 laptop / monitor / tablet），QLabel 塞不下。
    // 生命周期跟着本行走：加进布局即被 reparent，析构时一起回收。
    QWidget *title = nullptr;
    if (titleWidget != nullptr) {
        title = titleWidget;
        title->setParent(this);
    } else {
        auto *labelWidget = new QLabel(label, this);
        labelWidget->setFont(uiFont(tokens::kFont12));
        labelWidget->setStyleSheet(
            QStringLiteral("color:%1").arg(cssColor(Theme::instance().palette().text1)));
        title = labelWidget;
    }
    hint_ = new QLabel(hint, this);
    hint_->setFont(uiFont(tokens::kFont11));
    hint_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(Theme::instance().palette().text3)));
    col->addWidget(title);
    col->addWidget(hint_);

    hl->addLayout(col);
    hl->addStretch(1);
    if (control != nullptr) {
        hl->addWidget(control);
    }
}

void ListRow::setHint(const QString &hint)
{
    hint_->setText(hint);
}

void ListRow::paintEvent(QPaintEvent *)
{
    // 列表行之间用 hairline 分隔：设计稿是「分隔线 + 留白」而非整块描边。
    QPainter p(this);
    p.setPen(Theme::instance().palette().hairline);
    p.drawLine(0, 0, width(), 0);
}

// ── applyInputStyle ───────────────────────────────────────────────────
// 设计稿 ui.css .input：28px 高 / padding 0 8px / 1px line-2 描边 /
// r-ctl 圆角 / surface 底 / text-1 字色；focus 时描边转主色并加 3px 主色浅底光晕。
//
// 原生 QLineEdit 不会跟随 DPalette——DTK 的窗口装饰只管到 QWidget 级，
// 内部输入区仍走 Fusion 默认外观，深色下就是一块发白的浅灰。这里显式补上。
void applyInputStyle(QLineEdit *edit)
{
    if (edit == nullptr) {
        return;
    }
    const auto restyle = [edit] {
        const Palette &p = Theme::instance().palette();
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  height:%1px; padding:0 %2px;"
            "  border:1px solid %3; border-radius:%4px;"
            "  background:%5; color:%6;"
            "  selection-background-color:%7; selection-color:%8;"
            "}"
            "QLineEdit:focus { border-color:%7; }"
            "QLineEdit:disabled { color:%9; background:%10; }")
            .arg(tokens::kControlHeight)
            .arg(tokens::kS2)
            .arg(cssColor(p.line2), QString::number(tokens::kRadiusCtl),
                 cssColor(p.surface), cssColor(p.text1),
                 cssColor(p.accent), cssColor(p.onAccent),
                 cssColor(p.text3), cssColor(p.surface3)));
        // 光晕用外发光近似：Qt 样式表对 box-shadow 支持有限，这里靠 focus 时
        // 换描边色 + 浅底来表达同一意图，不引入第二套视觉。
    };
    restyle();
    QObject::connect(&Theme::instance(), &Theme::changed, edit, [edit, restyle] {
        restyle();
        edit->update();
    });
}

// ── FgKeycap ──────────────────────────────────────────────────────────
FgKeycap::FgKeycap(const QString &text, bool ghost, QWidget *parent)
    : QWidget(parent), text_(text), ghost_(ghost)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize FgKeycap::sizeHint() const
{
    // ui.css .key：min-width 34 + padding 5×7 + 1px 描边
    const QFontMetrics fm(uiFont(tokens::kFont11, QFont::DemiBold));
    return QSize(qMax(34, fm.horizontalAdvance(text_) + tokens::kS2 * 2 + 2),
                 fm.height() + 5 * 2 + 2);
}

void FgKeycap::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    if (ghost_) {
        // .key--ghost：虚线描边、透明底、无底部阴影、文字用 text-3
        QPen pen(p.line2, 1, Qt::DashLine);
        pen.setDashPattern({4.0, 3.0});
        pt.setPen(pen);
        pt.setBrush(Qt::NoBrush);
    } else {
        pt.setPen(QPen(p.line2, 1));
        pt.setBrush(p.surface);
    }
    pt.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                       tokens::kRadiusCtl, tokens::kRadiusCtl);

    if (!ghost_) {
        // box-shadow: 0 1px 0 line-2 —— 键帽厚度
        pt.setPen(Qt::NoPen);
        pt.setBrush(p.line2);
        pt.drawRoundedRect(QRectF(1.0, height() - 3.0, width() - 2.0, 2.5),
                           tokens::kRadiusCtl, tokens::kRadiusCtl);
    }

    pt.setPen(ghost_ ? p.text3 : p.text1);
    pt.setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    pt.drawText(rect(), Qt::AlignCenter, text_);
}

// ── banner ───────────────────────────────────────────────────────────
// ui.css .banner 的六档变体一一对应：info 走 accent 族（accentLine /
// accentSoft / accentText 三个角色齐全），neutral 走中性面（line-2 描边 +
// surface-2 底 + text-2），warn / danger / ok 三族 Palette 里只有 solid /
// soft / text，没有 line，描边按 solid 压一档透明度派生——宁可从语义角色
// 派生，也不写死主题色。
QWidget *banner(const QString &text, QWidget *parent, BannerVariant variant)
{
    const Palette &p = Theme::instance().palette();
    QColor line;
    QColor bg;
    QColor fg;
    QString iconName = QStringLiteral("info");
    switch (variant) {
    case BannerVariant::Warn:
        line = p.warn.solid;
        line.setAlphaF(0.45);
        bg = p.warn.soft;
        fg = p.warn.text;
        iconName = QStringLiteral("warn");
        break;
    case BannerVariant::Danger:
        line = p.danger.solid;
        line.setAlphaF(0.45);
        bg = p.danger.soft;
        fg = p.danger.text;
        iconName = QStringLiteral("warn");
        break;
    case BannerVariant::Ok:
        line = p.ok.solid;
        line.setAlphaF(0.45);
        bg = p.ok.soft;
        fg = p.ok.text;
        iconName = QStringLiteral("check");
        break;
    case BannerVariant::Neutral:
        line = p.line2;
        bg = p.surface2;
        fg = p.text2;
        break;
    case BannerVariant::Info:
    default:
        line = p.accentLine;
        bg = p.accentSoft;
        fg = p.accentText;
        break;
    }

    auto *w = new QWidget(parent);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(10, tokens::kS3, tokens::kS3, 10);
    hl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(w);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(iconName, 14, fg).pixmap(14, 14));
    auto *label = new QLabel(text, w);
    label->setWordWrap(true);
    label->setFont(uiFont(tokens::kFont12));
    label->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(fg)));
    hl->addWidget(icon, 0, Qt::AlignTop);
    hl->addWidget(label, 1);
    applyBoxStyle(w,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(cssColor(line))
                      .arg(tokens::kRadiusCard)
                      .arg(cssColor(bg)));
    return w;
}

// ── SegmentedControl ─────────────────────────────────────────────────
// ui.css .seg：外框 padding 2 / gap 2 / r-ctl 圆角 / surface-3 底；每档
// height 24、padding 0 10px、r 为 4；选中档换 surface 底 + shadow-bar。
// 一档就是一个几何，不是子控件——用 QPushButton + 布局的话 hover 底色、
// 选中浮起都要逐个贴样式表，加一档就多一份样式声明。
SegmentedControl::SegmentedControl(const QStringList &items, QWidget *parent)
    : QWidget(parent), labels_(items)
{
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    const QFont f = uiFont(tokens::kFont11, QFont::DemiBold);
    const QFontMetrics fm(f);
    for (const QString &t : items) {
        Segment s;
        s.text = t;
        s.data = t;
        // 文字宽度 + 左右各 10px（ui.css .seg button 的 padding: 0 10px）。
        s.size = QSize(fm.horizontalAdvance(t) + 20, 24);
        segments_.append(s);
    }
}

QVariant SegmentedControl::currentData() const
{
    return current_ >= 0 && current_ < segments_.size() ? segments_.at(current_).data
                                                        : QVariant();
}

void SegmentedControl::setCurrentIndex(int index)
{
    if (index < 0 || index >= segments_.size() || index == current_) {
        return;
    }
    current_ = index;
    update();
    Q_EMIT currentIndexChanged(current_);
}

QSize SegmentedControl::sizeHint() const
{
    int w = 4;  // 外框左右各 2px padding
    for (const Segment &s : segments_) {
        w += s.size.width();
    }
    w += qMax(0, segments_.size() - 1) * 2;  // gap 2
    return {w, 24 + 4};
}

int SegmentedControl::hitTest(int x) const
{
    int cursor = 2;
    for (int i = 0; i < segments_.size(); ++i) {
        const int w = segments_.at(i).size.width();
        if (x < cursor + w) {
            return i;
        }
        cursor += w + 2;
    }
    return -1;
}

void SegmentedControl::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath frame;
    frame.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tokens::kRadiusCtl,
                         tokens::kRadiusCtl);
    pt.fillPath(frame, p.surface3);

    int cursor = 2;
    for (int i = 0; i < segments_.size(); ++i) {
        const Segment &s = segments_.at(i);
        const QRectF cell(cursor, 2, s.size.width(), s.size.height());
        if (i == current_) {
            // 选中档「浮」在外框之上：先铺 surface，再压一层 shadow-bar。
            QPainterPath lifted;
            lifted.addRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            pt.fillPath(lifted, p.shadowBar);
            pt.fillPath(lifted, p.surface);
        }
        pt.setPen((i == current_ || i == hover_) ? p.text1 : p.text2);
        pt.setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pt.drawText(cell, Qt::AlignCenter, s.text);
        cursor += s.size.width() + 2;
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent *event)
{
    const int i = hitTest(event->position().toPoint().x());
    if (i >= 0) {
        setCurrentIndex(i);
    }
}

void SegmentedControl::enterEvent(QEnterEvent *event)
{
    hover_ = hitTest(event->position().toPoint().x());
    update();
    QWidget::enterEvent(event);
}

void SegmentedControl::leaveEvent(QEvent *event)
{
    hover_ = -1;
    update();
    QWidget::leaveEvent(event);
}

// ── buildMappingGrid ──────────────────────────────────────────────────
QWidget *buildMappingGrid(const QList<KeyPair> &pairs, QWidget *parent)
{
    // ui.css .mapping：repeat(3, 1fr) / gap 8 / padding 12。
    // .keypair：竖排、gap 5、padding 8；.keys：横排、gap 5。
    const Palette &p = Theme::instance().palette();
    constexpr int kColumns = 3;

    auto *grid = new QWidget(parent);
    auto *gl = new QGridLayout(grid);
    gl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    gl->setHorizontalSpacing(tokens::kS2);
    gl->setVerticalSpacing(tokens::kS2);

    for (int i = 0; i < pairs.size(); ++i) {
        const KeyPair &pair = pairs.at(i);
        auto *cell = new QWidget(grid);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(tokens::kS2, tokens::kS2, tokens::kS2, tokens::kS2);
        cl->setSpacing(5);

        auto *keys = new QWidget(cell);
        auto *kl = new QHBoxLayout(keys);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(5);
        kl->addWidget(new FgKeycap(pair.from, pair.ghost, keys));
        auto *arrow = new QLabel(keys);
        arrow->setFixedSize(12, 12);
        arrow->setPixmap(icons::icon(QStringLiteral("arrowRight"), 12, p.text3).pixmap(12, 12));
        kl->addWidget(arrow, 0, Qt::AlignVCenter);
        kl->addWidget(new FgKeycap(pair.to, pair.ghost, keys));
        kl->addStretch(1);
        cl->addWidget(keys);

        if (!pair.note.isEmpty()) {
            auto *note = new QLabel(pair.note, cell);
            note->setFont(uiFont(tokens::kFont11));
            note->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
            cl->addWidget(note);
        }
        gl->addWidget(cell, i / kColumns, i % kColumns);
    }
    return grid;
}

// ── FgSelect ──────────────────────────────────────────────────────────
FgSelect::FgSelect(const QStringList &options, int current, QWidget *parent)
    : QAbstractButton(parent), options_(options), current_(current)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &QAbstractButton::clicked, this, [this] { openMenu(); });
}

void FgSelect::setOptions(const QStringList &options)
{
    options_ = options;
    if (current_ >= options_.size()) {
        current_ = options_.isEmpty() ? 0 : options_.size() - 1;
    }
    updateGeometry();
    update();
}

void FgSelect::setCurrentIndex(int index)
{
    if (index == current_ || index < 0 || index >= options_.size()) {
        return;
    }
    current_ = index;
    updateGeometry();
    update();
    Q_EMIT currentIndexChanged(current_);
}

QString FgSelect::currentText() const
{
    return current_ >= 0 && current_ < options_.size() ? options_.at(current_) : QString();
}

QSize FgSelect::sizeHint() const
{
    const QFontMetrics fm(uiFont(tokens::kFont12, QFont::DemiBold));
    // 右侧给 chevron 留 24px（ui.css .select 的 padding-right: var(--s-6)）
    return QSize(fm.horizontalAdvance(currentText()) + tokens::kS6 + tokens::kS2 * 2 + 4,
                 tokens::kControlHeight);
}

void FgSelect::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    QColor bg = hovered_ ? p.surfaceHi : p.surface;
    pt.setPen(QPen(p.line2, 1));
    pt.setBrush(bg);
    pt.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                       tokens::kRadiusCtl, tokens::kRadiusCtl);

    pt.setPen(p.text1);
    pt.setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    const QRect textRect = rect().adjusted(tokens::kS2, 0, -tokens::kS6, 0);
    pt.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(pt.font()).elidedText(currentText(), Qt::ElideRight,
                                                   textRect.width()));

    // chevron：.select-wrap .ic，right 8px、垂直居中
    const int ic = 12;
    pt.drawPixmap(QPoint(width() - tokens::kS2 - ic, (height() - ic) / 2),
                  icons::icon(QStringLiteral("chevronDown"), ic, p.text3).pixmap(ic, ic));
}

void FgSelect::enterEvent(QEnterEvent *e)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void FgSelect::leaveEvent(QEvent *e)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void FgSelect::openMenu()
{
    if (options_.isEmpty()) {
        return;
    }
    const Palette &p = Theme::instance().palette();
    const int rowH = tokens::kControlHeight + tokens::kS2;

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background:%1; border:1px solid %2; border-radius:%3px; padding:4px 0; }"
        "QMenu::item { padding:6px 12px; color:%4; font-size:%5px; border-radius:%4px; }"
        "QMenu::item:selected { background:%6; color:%7; }"
        "QMenu::separator { height:1px; background:%2; margin:4px 0; }")
        .arg(cssColor(p.surface), cssColor(p.line2), QString::number(tokens::kRadiusCtl),
             cssColor(p.text1), QString::number(tokens::kFont12),
             cssColor(p.accent), cssColor(p.onAccent)));

    // 用 QActionGroup 预选当前项：QMenu 本身没有 setCurrentAction，
    // 而不预选的话弹出菜单第一项总是高亮，长列表里等于没有记忆。
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    int idx = 0;
    for (const QString &opt : options_) {
        QAction *act = menu.addAction(opt);
        act->setCheckable(true);
        act->setChecked(idx == current_);
        group->addAction(act);
        ++idx;
    }

    // 弹在控件正下方；屏幕空间不够时 QMenu 自己会上翻。
    QPoint pos = mapToGlobal(QPoint(0, height()));
    menu.setGeometry(QRect(pos, QSize(width(), rowH * options_.size())));
    QAction *chosen = menu.exec(pos);
    if (chosen != nullptr) {
        setCurrentIndex(menu.actions().indexOf(chosen));
    }
}

} // namespace fg
