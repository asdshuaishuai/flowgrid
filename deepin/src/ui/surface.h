// 浮板外壳的「卡片」：圆角底 + 单一描边 + 顶部内高光。
//
// 520px 浮板与 300px 托盘浮层共用这一份外壳。两处各写一遍外框逻辑，迟早
// 会漂移——托盘那套是「描边和底色一笔画」，浮板那套是「底色在父控件、描边
// 在单独一层」，两套画出来的边框粗细、圆角、高光位置都不一样，同屏摆在一起
// 就是「内外不一致」。
//
// 三个实现要点，都是踩出来的：
//
//  1. 描边必须**抬到最后画**。Qt 里子控件永远画在父控件 paintEvent 之后，
//     内容区那张不透明底色正好盖住父控件里画的描边 1px。早先的变通是让
//     内容区自己再画一对左右直边——于是圆角边上并着一条直边，两条线接不上，
//     就是那条「不规则边框」。现在描边单独一层、抬到最上面，只有一条线。
//
//  2. 底色只能由卡片自己画，**卡片外面不能有子控件画不透明底色**。底色一旦
//     由子控件（内容区）画，方形的底就会盖住圆角，描边是圆的、底是方的，
//     角上又是一种「不规则边框」。所以内容区/滚动视口要么透明、要么自己带
//     与卡片一致的圆角。
//
//  3. 顶部高光是**沿顶部两个圆角的一段 1px edge-高光线**，不是一条从上到下
//     渐变的色带。mac 端是 UnevenRoundedRectangle(顶角圆) + edgeHi 描边，
//     这里照抄同一件事：先把顶角画成圆，再描 1px。
#ifndef FLOWGRID_SURFACE_H
#define FLOWGRID_SURFACE_H

#include <QColor>
#include <QImage>
#include <QRect>
#include <QWidget>

namespace fg {

class Surface : public QWidget {
    Q_OBJECT
public:
    explicit Surface(QWidget *parent = nullptr);

    // 顶部 band（标题栏那条）的高度。0 = 不画。
    // band 的底色默认取 card（比页面亮一档，正好和 mac 的 surface-2 一个角色）。
    void setBandHeight(int h);

    // 底部 band（页脚那条）的高度。0 = 不画。顶部一道 hairline。
    void setFootHeight(int h);

    // band 底色。传非法色表示用默认（card）。
    void setBandColor(const QColor &c);

    // 卡片圆角。默认 kRadiusSurface。
    void setRadius(int r);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;

private:
    class Edge;
    Edge *edge_ = nullptr;
    int bandH_ = 0;
    int footH_ = 0;
    int radius_ = 0;
    QColor bandColor_;
};

// ── 阴影板 ─────────────────────────────────────────────────────────────
//
// 浮板真正的投影在这里**自己画**。
//
// 为什么不用 QGraphicsDropShadowEffect：
//   · 挂在**顶层窗口**上 → 投影落在窗口之外，被窗口边界切成一圈硬边，浮板看着
//     像贴了张纸片（这正是原来那版的样子）。
//   · 挂在**卡片**上 → 效果每次重绘都要把整棵控件树渲染进离屏位图再模糊一次，
//     内容一滚动就重算一遍，滚起来发顿，缓存与滚动内容不同步时还会留残影。
//   · 它的模糊把 alpha 摊得太开：42 的模糊半径下来，投影峰值只剩 2/255——
//     等于没有（实测留白处 alpha 全是 0~2）。
//
// 所以这里自己算：把卡片的圆角矩形画进一张 alpha 缓冲，用三次 box blur 逼近
// 高斯（box 宽按 CSS 的 blur/2 折算），再按 shadowPanel 的颜色上色。
// 算一次就缓存住，尺寸与参数不变时绝不重算——滚动时纹丝不动。
class ShadowPlate : public QWidget {
    Q_OBJECT
public:
    explicit ShadowPlate(QWidget *parent = nullptr);

    // 卡片在窗口里的矩形（板子与卡片是兄弟，坐标系一致）。
    void setCardRect(const QRect &r);

    // 卡片圆角。
    void setRadius(int r);

    // 阴影本体：颜色（带 alpha）、模糊半径、偏移。
    void setShadow(const QColor &color, int blur, int dx, int dy);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void rebuild();

    QImage shadow_;
    QRect card_;
    QColor color_ = QColor(0, 0, 0, 36);
    int blur_ = 42;
    int dx_ = 0;
    int dy_ = 18;
    int radius_ = 0;
    bool dirty_ = true;
};

} // namespace fg

#endif // FLOWGRID_SURFACE_H
