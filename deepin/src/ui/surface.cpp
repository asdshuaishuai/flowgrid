#include "surface.h"
#include "core/designtokens.h"
#include "core/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>

#include <vector>

namespace fg {

// ── 描边层 ─────────────────────────────────────────────────────────────
//
// 浮在最上面的那一圈线。它透明、不吃鼠标事件、不参与布局，只描边。
//
// 为什么必须单独一层而不是画在卡片的 paintEvent 里：Qt 的绘制顺序是
// 「父控件 paintEvent → 子控件」，卡片的内容区（不透明底色）画完就把父控件
// 里描的边盖掉了 1px。而卡片外面的子控件想补这一圈，就得自己画一条直边，
// 圆角处自然接不上——那条「不规则边框」就是这么来的。
class Surface::Edge : public QWidget {
public:
    explicit Edge(QWidget *parent) : QWidget(parent)
    {
        // 纯装饰：不吃鼠标事件（浮板边缘一圈还得能点得到，点边缘收起浮板是
        // 「失焦即走」的一部分）。
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

    void setRadius(int r)
    {
        radius_ = r;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Palette &p = Theme::instance().palette();
        const qreal r = radius_;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 描边画在圆角路径上：整块内缩半像素，让 1px 线正好落在圆角外沿，
        // 而不是被切掉半个像素（切掉的描边在深色主题下会显得更淡，
        // 浮板看着像描边不匀）。
        const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(rect, r, r);

        painter.setPen(QPen(p.line2, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // 顶部 1px 内高光：只沿顶部两个圆角画一段线（mac 端是
        // UnevenRoundedRectangle(顶角圆) + edgeHi 描边）。
        // 画在 line-2 描边**之上**：顶边因此比两侧亮一档，浮板看着有厚度。
        //
        // 路径的走法：左边 → 绕过左上角 → 顶边 → 绕过右上角 → 右边。
        //
        // **角度单位是「度」，不是 1/16 度**。QPainterPath::arcTo 的文档写着
        // 「1/16 度」，可实现里那句 `sweepAngle <= -360` 判的是度：按 1/16 度
        // 传 -90*16 = -1440，直接落进「整圈」分支，于是角上多画出一个直径
        // 24px 的圆，还多出一条横穿整块卡片的斜线——浮板左上角那个莫名其妙的
        // 圆圈就是它。
        QPainterPath top;
        top.moveTo(rect.left(), rect.top() + r);
        top.arcTo(rect.left(), rect.top(), r * 2, r * 2, 180.0, -90.0);
        top.lineTo(rect.right() - r, rect.top());
        top.arcTo(rect.right() - r * 2, rect.top(), r * 2, r * 2, 90.0, -90.0);
        painter.setPen(QPen(p.edgeHi, 1.0));
        painter.drawPath(top);
    }

private:
    int radius_ = tokens::kRadiusSurface;
};

Surface::Surface(QWidget *parent) : QWidget(parent)
{
    radius_ = tokens::kRadiusSurface;
    // 底色由 paintEvent 画（要圆角），不能让控件自己再刷一层方形底。
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground, true);

    edge_ = new Edge(this);
    edge_->setRadius(radius_);
    edge_->setGeometry(rect());
    edge_->raise();

    // 系统主题被切走时，外框、band、描边都得跟着换色。
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        update();
        if (edge_ != nullptr) {
            edge_->update();
        }
    });
}

void Surface::setBandHeight(int h)
{
    bandH_ = h;
    update();
}

void Surface::setFootHeight(int h)
{
    footH_ = h;
    update();
}

void Surface::setBandColor(const QColor &c)
{
    bandColor_ = c;
    update();
}

void Surface::setRadius(int r)
{
    radius_ = r;
    if (edge_ != nullptr) {
        edge_->setRadius(r);
    }
    update();
}

void Surface::paintEvent(QPaintEvent *)
{
    const Palette &p = Theme::instance().palette();
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(rect, radius_, radius_);

    // 底色：外壳档（= 设计稿 --fg-surface）。
    //
    // 这一档必须是外壳自己的面（白 / #1e2127），不是「比卡片暗一档」的那一档：
    // 设计稿里 .panel 的 background 就是 surface，内容区 .content 继承它，
    // 浮起来的是里面的 .section（surface-2）和 .list（surface）。外壳比内容区
    // 暗半档的话，内容区与外壳之间就没有「浮板是个壳、内容在壳里」的层次。
    pt.setPen(Qt::NoPen);
    pt.setBrush(p.surface);
    pt.drawPath(path);

    // 顶部 band（标题栏那条）与底部 band（页脚那条）：卡片档底 + hairline。
    // 必须裁在圆角路径里，否则顶部两个角会被方形底色顶出直角、底部两个角
    // 会把卡片的圆角盖成方的——两处都是「不规则边框」。
    if (bandH_ > 0 || footH_ > 0) {
        pt.save();
        pt.setClipPath(path);
        const QColor band = bandColor_.isValid() ? bandColor_ : p.surface2;
        if (bandH_ > 0) {
            pt.fillRect(QRectF(0, 0, width(), bandH_), band);
            // hairline 用 fillRect 画（整像素），不用 drawLine：1px 的线画在
            // 整数坐标上会横跨两个像素行，深色主题下看着像一条 2px 的粗线。
            pt.fillRect(QRectF(0, bandH_ - 1, width(), 1), p.hairline);
        }
        if (footH_ > 0) {
            const qreal top = height() - footH_;
            pt.fillRect(QRectF(0, top, width(), footH_), band);
            pt.fillRect(QRectF(0, top, width(), 1), p.hairline);
        }
        pt.restore();
    }
}

void Surface::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (edge_ != nullptr) {
        edge_->setGeometry(rect());
    }
}

void Surface::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // 描边要压住所有子控件：布局随时可能重新堆叠子控件（换页脚、塞标题栏
    // 右侧的槽位），显示时统一抬一次，保证它永远在最上层。
    if (edge_ != nullptr) {
        edge_->setGeometry(rect());
        edge_->raise();
    }
}

// ── 阴影板 ─────────────────────────────────────────────────────────────

namespace {

// 三次 box blur 逼近高斯。
//
// n 次宽 w 的 box 卷积的等效高斯 σ = w*sqrt((n²-1)/(12n))；n=3 时系数
// 0.4714，所以 box 宽取 σ/0.4714 ≈ 2.12σ。走滑动窗口求和，每像素 O(1)，
// 一张 562×752 的图三遍下来十几毫秒，只在尺寸变化时算一次。
void boxBlur(std::vector<float> &a, int w, int h, int radius)
{
    if (radius < 1 || w <= 0 || h <= 0) {
        return;
    }
    std::vector<float> tmp(a.size(), 0.0f);
    const float inv = 1.0f / float(2 * radius + 1);
    for (int pass = 0; pass < 3; ++pass) {
        // 横向
        for (int y = 0; y < h; ++y) {
            const float *row = a.data() + y * w;
            float *out = tmp.data() + y * w;
            float sum = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                sum += row[qBound(0, i, w - 1)];
            }
            for (int x = 0; x < w; ++x) {
                out[x] = sum * inv;
                sum += row[qMin(w - 1, x + radius + 1)] - row[qMax(0, x - radius)];
            }
        }
        // 纵向
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                sum += tmp[qBound(0, i, h - 1) * w + x];
            }
            for (int y = 0; y < h; ++y) {
                a[y * w + x] = sum * inv;
                sum += tmp[qMin(h - 1, y + radius + 1) * w + x]
                       - tmp[qMax(0, y - radius) * w + x];
            }
        }
    }
}

} // namespace

ShadowPlate::ShadowPlate(QWidget *parent) : QWidget(parent)
{
    radius_ = tokens::kRadiusSurface;
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    // 纯装饰：不吃鼠标事件。
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void ShadowPlate::setCardRect(const QRect &r)
{
    if (card_ == r) {
        return;
    }
    card_ = r;
    dirty_ = true;
    update();
}

void ShadowPlate::setRadius(int r)
{
    if (radius_ == r) {
        return;
    }
    radius_ = r;
    dirty_ = true;
    update();
}

void ShadowPlate::setShadow(const QColor &color, int blur, int dx, int dy)
{
    if (color_ == color && blur_ == blur && dx_ == dx && dy_ == dy) {
        return;
    }
    color_ = color;
    blur_ = blur;
    dx_ = dx;
    dy_ = dy;
    dirty_ = true;
    update();
}

void ShadowPlate::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    dirty_ = true;
}

void ShadowPlate::rebuild()
{
    const int w = width();
    const int h = height();
    dirty_ = false;
    if (w <= 0 || h <= 0 || !card_.isValid()) {
        shadow_ = QImage();
        return;
    }

    // 1) 阴影本体：卡片那块圆角矩形，按偏移量挪一下。
    QImage shape(w, h, QImage::Format_ARGB32);
    shape.fill(Qt::transparent);
    {
        QPainter p(&shape);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        const QRectF r = QRectF(card_).adjusted(-0.5, -0.5, -0.5, -0.5)
                             .translated(dx_, dy_);
        QPainterPath path;
        path.addRoundedRect(r, radius_, radius_);
        p.drawPath(path);
    }

    // 2) 取 alpha，做高斯模糊。
    std::vector<float> a(size_t(w) * size_t(h), 0.0f);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(shape.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            a[size_t(y) * size_t(w) + size_t(x)] = float(qAlpha(line[x]));
        }
    }
    // CSS 的 blur 半径 r 对应 σ = r/2。
    const int sigma = qMax(1, blur_ / 2);
    const int boxR = qMax(1, int((sigma / 0.4714 - 1.0) / 2.0 + 0.5));
    boxBlur(a, w, h, boxR);

    // 3) 按阴影色上色。
    //
    // 用**非预乘**的 ARGB32：预乘格式下，alpha 只有个位数时把 (24,30,44)
    // 直接写进去会被当成「已经预乘过的值」，再反解回来就溢成了白色——阴影
    // 于是变成一圈白边（深色主题下看着像描了条白线）。
    shadow_ = QImage(w, h, QImage::Format_ARGB32);
    shadow_.fill(Qt::transparent);
    const int baseAlpha = color_.alpha();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(shadow_.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int alpha = qBound(0, int(a[size_t(y) * size_t(w) + size_t(x)] + 0.5f), 255);
            line[x] = qRgba(color_.red(), color_.green(), color_.blue(),
                            baseAlpha * alpha / 255);
        }
    }
}

void ShadowPlate::paintEvent(QPaintEvent *)
{
    if (dirty_) {
        rebuild();
    }
    if (shadow_.isNull()) {
        return;
    }
    QPainter p(this);
    p.drawImage(0, 0, shadow_);
}

} // namespace fg
