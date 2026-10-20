// 浮板基类：520px 宽、无边框、圆角 + 阴影、按需唤出失焦即走。
//
// 设计稿的「应用没有常驻主窗口，所有界面都是按需唤出的浮板」在这里落地。
//
// 外壳分三层，缺一层就会出不同的毛病：
//
//   窗口（半透明）          ← 四周留出阴影的落脚处
//     └ 阴影板（永远不变）   ← QGraphicsDropShadowEffect 挂在这里
//         └ 卡片 Surface    ← 圆角底 + 唯一一条描边 + 顶部高光
//             └ 标题栏 / 滚动内容区 / 页脚
//
// 阴影挂在**窗口**上会被窗口自己裁掉（Qt 画不出界外的像素），于是浮板看着
// 「没有阴影，只有一圈硬边」；阴影挂在**卡片**上又会随内容滚动反复重算，
// 滚起来发顿、还容易留下残影。所以中间加一块永远不变的阴影板：效果算一次，
// 之后一直复用。
#ifndef FLOWGRID_PANEL_H
#define FLOWGRID_PANEL_H

#include "surface.h"

#include <QWidget>

class QVBoxLayout;
class QLabel;
class QScrollArea;
class QScrollBar;

namespace fg {

// 浮板窗口四周为阴影留出的透明边距。
//
// 窗口比「看得见的那块卡片」大一圈，这一圈是给投影留的：卡片自身的圆角
// 描边只描 1px，没有这圈留白，投影就会被窗口边界切成一圈硬边。
struct ShellMargin {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// 按任务栏所在的方向，把**贴边那一侧**的阴影余白去掉。
//
// 为什么必须去掉：X11 上 popup 窗口被限制在工作区（可用区）内——实测把浮板
// 用 xdotool move 到任务栏区域，KWin 会把它拉回工作区内侧。于是「卡片贴住
// 任务栏」和「卡片下方留 39px 给投影落脚」这两件事在 DDE 上**不能同时成立**：
// 留了余白，卡片就悬在任务栏上方三十几像素，浮板看着像浮在任务栏旁边，
// 而不是从任务栏里长出来；而且同一套代码里托盘浮层（留 32）与 520px 浮板
// （留 39）悬空的高度还不一样，两个浮层同屏出现就是「内外不一致」。
//
// 去掉之后：卡片下沿正好压在任务栏上沿上，投影落在左右和上方——那才是
// 「浮板从任务栏里长出来」的观感（mac 上浮板也是贴着菜单栏下沿长的）。
// 底边贴着任务栏，本来也看不见投影。
ShellMargin edgeAdjustedMargin(const ShellMargin &base);

// 浮板摆位：贴住锚点、且绝不越出屏幕可用区。
//
// margin 是窗口四周的阴影留白：定位算的是**卡片**（看得见的那块）在屏幕上的
// 位置，窗口再按留白往四周撑开。托盘浮层（QWidget）与 520px 浮板（Panel）
// 共用这一份几何，两处各写一遍迟早会漂移，所以抽成自由函数。
// 返回 false 表示屏幕不可用，调用方应放弃摆位。
bool placePopup(QWidget *w, const QPoint &anchor, const ShellMargin &margin = {});

// 浮板入场动画（opacity + translateY(-8)，220ms）。同样被托盘浮层复用。
void animatePopupIn(QWidget *w);

class Panel : public QWidget {
    Q_OBJECT
public:
    // title 为空则不画标题栏（托盘浮层走自己的头部）。
    explicit Panel(const QString &title, QWidget *parent = nullptr);

    // 内容区：标题栏之下、页脚之上的可滚动容器。
    QWidget *content() const { return content_; }

    // 标题栏右侧插槽：传 nullptr 表示不放任何东西。
    void setTitleBarWidget(QWidget *widget);

    // 页脚：留空则不画页脚分隔线。
    void setFooterWidget(QWidget *widget);

    // 显示时把浮板对齐到锚点（通常是托盘图标或屏幕右上角），并保证不越出屏幕。
    void popupAt(const QPoint &anchor);

    // 按「内容实际需要多高」算整块浮板的高度，而不是让 QScrollArea 的 sizeHint
    // 代替。后者不含折叠区展开后的真实高度，结果是屏幕明明放得下，浮板一打开
    // 就凭空多出一条滚动条、末行还被裁掉半截。
    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void restyle();
    void syncShadowPlate();
    void applyContentMargins();

    ShellMargin margin_;
    ShellMargin baseMargin_;   // 未按任务栏方向调整前的基准余白
    QVBoxLayout *root_ = nullptr;
    QVBoxLayout *cardLayout_ = nullptr;
    QVBoxLayout *contentLayout_ = nullptr;
    Surface *card_ = nullptr;
    ShadowPlate *plate_ = nullptr;
    QWidget *titleBar_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QWidget *titleRight_ = nullptr;
    QWidget *content_ = nullptr;
    QWidget *footer_ = nullptr;
    QScrollArea *scroll_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_PANEL_H
