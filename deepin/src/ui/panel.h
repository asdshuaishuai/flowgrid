// 浮板基类：520px 宽、无边框、圆角 + 阴影、按需唤出失焦即走。
//
// 设计稿的「应用没有常驻主窗口，所有界面都是按需唤出的浮板」在这里落地：
// 浮板是 FramelessWindowHint + WA_TranslucentBackground，阴影由 QGraphicsDropShadowEffect
// 承担，圆角与顶部内高光在 paintEvent 里画，唤出动画走 QPropertyAnimation 的
// geometry + windowOpacity，节奏与设计稿一致（220ms / translateY(-8) / scale(.97)）。
#ifndef FLOWGRID_PANEL_H
#define FLOWGRID_PANEL_H

#include <QWidget>

class QVBoxLayout;
class QLabel;
class QPropertyAnimation;

namespace fg {

// 浮板摆位：贴住锚点、且绝不越出屏幕可用区。
// 托盘浮层（QWidget）与 520px 浮板（Panel）共用这一份几何，两处各写一遍
// 迟早会漂移，所以抽成自由函数。返回 false 表示屏幕不可用，调用方应放弃摆位。
bool placePopup(QWidget *w, const QPoint &anchor);

// 浮板入场动画（opacity + translateY(-8) + scale(.97)，220ms）。
// 同样被托盘浮层复用。
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
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *) override;

    QVBoxLayout *rootLayout() const { return root_; }

private:
    void paintChrome(QPainter &p);

    QVBoxLayout *root_ = nullptr;
    QWidget *titleBar_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QWidget *titleRight_ = nullptr;
    QWidget *content_ = nullptr;
    QWidget *footer_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_PANEL_H
