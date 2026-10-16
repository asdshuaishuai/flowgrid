// 设计稿组件的 Qt 复刻。
//
// 为什么不用 DTK 原生控件（如 DSwitch/DButton）：设计稿把控件几何钉死为
// 「28px 按钮 / 36×20 开关 / 18px 徽章 / 42px 设备图标」并要求四平台逐像素
// 一致，而 DTK 原生控件的尺寸随平台主题浮动，复刻反而对不齐。因此这里自绘
// 几何、自绘交互，但**颜色一律取自 Theme（即 DPalette 语义角色）**——
// 尺寸服从设计稿，颜色服从 DTK 规范，两条约束各归其位。
#ifndef FLOWGRID_WIDGETS_H
#define FLOWGRID_WIDGETS_H

#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/theme.h"

#include <QAbstractButton>
#include <QFrame>
#include <QWidget>

#include <vector>

class QLabel;
class QLineEdit;
class QHBoxLayout;
class QVBoxLayout;
class QGridLayout;
class QPropertyAnimation;

namespace fg {

// 给「带子控件的容器」加边框 / 底色时必须走这里。
//
// 原因：Qt 样式表里一条**不带选择器**的声明会级联到该控件的所有子孙，典型
// 症状是容器里的每个 QLabel 各自长出一圈同样的边框（拖放区一度画成三条虚线框）。
// 这里统一生成 #objectName 限定的选择器，语义不再外溢到子控件。
void applyBoxStyle(QWidget *w, const QString &declarations);

// ── 状态点：7px 圆点，2s 呼吸 ────────────────────────────────────────
class StatusDot : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)
public:
    explicit StatusDot(QWidget *parent = nullptr);

    void setQuality(Quality q);
    void setPulsing(bool on);
    qreal pulse() const { return pulse_; }
    void setPulse(qreal v) { pulse_ = v; }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override { return {tokens::kStatusDotSize, tokens::kStatusDotSize}; }

private:
    QColor solid_;
    QPropertyAnimation *anim_ = nullptr;
    qreal pulse_ = 1.0;
};

// ── 徽章：18px pill，5 种语义变体 ────────────────────────────────────
class Badge : public QWidget {
    Q_OBJECT
public:
    enum class Variant { Accent, Neutral, Ok, Warn, Danger, Idle };

    explicit Badge(const QString &text, Variant variant = Variant::Neutral,
                   QWidget *parent = nullptr);

    void setVariant(Variant v);
    void setText(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;

private:
    void refresh();
    QString text_;
    Variant variant_;
    QColor bg_;
    QColor fg_;
};

// ── 开关：36×20，滑块 16，按下 200ms 过渡 ────────────────────────────
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    bool isChecked() const { return checked_; }
    void setChecked(bool on);

    QSize sizeHint() const override { return {tokens::kSwitchWidth, tokens::kSwitchHeight}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool checked_ = false;
    bool hovered_ = false;
    qreal knob_ = 0.0;  // 0=关 1=开
};

// ── 延迟胶囊：min-width 56，按 good/warn/bad/idle 着色 ─────────────────
class LatencyChip : public QWidget {
    Q_OBJECT
public:
    explicit LatencyChip(QWidget *parent = nullptr);

    // online=false 时显示「—」并落到 idle 档（设计稿的 .lat--idle）。
    void setLatency(double ms, bool online);
    QString text() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
    Quality quality_ = Quality::Idle;
};

// ── 折叠区：chevron rotate(-90deg) / 200ms ───────────────────────────
class Collapsible : public QFrame {
    Q_OBJECT
public:
    // body 由调用方构建并交给本控件管理生命周期。
    Collapsible(const QString &title, const QString &iconName, QWidget *body,
                QWidget *badgeHost, QWidget *parent = nullptr);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed_; }

protected:
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void relayout();

    QWidget *body_ = nullptr;
    QWidget *head_ = nullptr;
    QLabel *chevron_ = nullptr;
    bool collapsed_ = false;
};

// ── 柱状图：70px 高 / 12 根 / 最近一根用主色 ──────────────────────────
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);

    // 推入一个新样本；窗口满则挤出最旧的一个。
    void pushSample(double ms);
    void clear();

    // 首屏铺底：第一次真实采样到达前图表是空的，折叠区一展开就是一片空白。
    // 用当前读数填满一屏作为占位，抖动带与 devicemanager.cpp 的 mock 源一致，
    // 首次 sampled() 到达后即被真实数据顶掉。seed <= 0（离线）时不铺。
    void prime(double seed);

    QSize sizeHint() const override {
        return {120, tokens::kChartHeight};
    }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    std::vector<double> samples_;
};

// ── 进度条：5px pill 轨道 + 主色填充（ui.css .bar）─────────────────────
class ProgressTrack : public QWidget {
    Q_OBJECT
    // 用属性动画驱动填充宽度，过渡时长取设计稿的 --dur-micro。
    Q_PROPERTY(int progress READ progress WRITE setProgress)
public:
    explicit ProgressTrack(QWidget *parent = nullptr);

    int progress() const { return progress_; }
    void setProgress(int pct);

    QSize sizeHint() const override { return {120, kTrackHeight}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kTrackHeight = 5;   // ui.css .bar
    int progress_ = 0;
};

// ── 按钮：28px 高（sm 24px），4 种变体 ────────────────────────────────
class FgButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Variant { Accent, Secondary, Ghost, Danger };

    FgButton(const QString &text, Variant variant = Variant::Accent, bool small = false,
             QWidget *parent = nullptr);

    // 设计稿的页脚按钮大多「图标 + 文字」并排（清空历史 / 接收目录 / 导出诊断）。
    void setIcon(const QString &iconName);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void refreshColors();
    QColor bg_;
    QColor fg_;

    QString text_;
    QString iconName_;
    Variant variant_;
    bool small_;
    bool hovered_ = false;
};

// ── 图标按钮：26×26，无边框 ──────────────────────────────────────────
class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit IconButton(const QString &iconName, const QString &tooltip = {},
                        QWidget *parent = nullptr);

    void setIcon(const QString &iconName);

    QSize sizeHint() const override { return {tokens::kIconButtonSize, tokens::kIconButtonSize}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString iconName_;
    bool hovered_ = false;
};

// ── 设备卡：42px 图标 + 1fr 文本 + auto 侧栏 ──────────────────────────
class DeviceCard : public QFrame {
    Q_OBJECT
public:
    explicit DeviceCard(const Device &device, QWidget *parent = nullptr);

    void setDevice(const Device &device);
    QString deviceId() const { return id_; }

    // 侧栏动作按钮：切换 / 重连 / 更多信息。离线设备给重连。
    void setAction(const QString &kind);   // "switch" | "reconnect" | "more" | "none"

Q_SIGNALS:
    void switchRequested(const QString &deviceId);
    void actionRequested(const QString &deviceId, const QString &kind);

protected:
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    void rebuild(const Device &device);

    QString id_;
    Device device_;
    bool active_ = false;
    bool online_ = false;
    bool hovered_ = false;
    QString actionKind_;

    QLabel *icon_ = nullptr;
    QLabel *name_ = nullptr;
    QLabel *meta_ = nullptr;
    LatencyChip *latency_ = nullptr;
    IconButton *action_ = nullptr;
};

// ── 导航行：图标 + 文字 + 右侧任意控件，整行可点、整行 hover ──────────
// 托盘浮层的所有入口都是这种形态。做成独立组件而不是散落的三段式布局，
// 是因为 hover 底色要铺满整行、点击要覆盖整行，子控件各管各的做不到。
class NavRow : public QWidget {
    Q_OBJECT
public:
    NavRow(const QString &iconName, const QString &label, QWidget *trailing = nullptr,
           QWidget *parent = nullptr);

    void setLabel(const QString &label);
    void setDanger(bool danger);

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    void repaintChildren(const Palette &p);

    QString iconName_;
    QString label_;
    QLabel *iconLabel_ = nullptr;
    QLabel *labelWidget_ = nullptr;
    QWidget *trailing_ = nullptr;
    bool danger_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
};

// ── 文字行：左标签 + 右控件，设置与列表两屏复用 ───────────────────────
class ListRow : public QFrame {
    Q_OBJECT
public:
    ListRow(const QString &label, const QString &hint, QWidget *control,
            QWidget *parent = nullptr);

    void setHint(const QString &hint);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QLabel *hint_ = nullptr;
};

// ── 输入类控件：按设计稿 .input / .select 的规格上样式 ─────────────────
//
// QLineEdit 保留原生实现（文本编辑、光标、选区这套行为自己写不划算），
// 只用样式表把几何和配色对齐设计稿；样式表里**不带选择器**，因为这些控件
// 都是叶子节点、没有子控件，不存在级联问题。
void applyInputStyle(QLineEdit *edit);

// ── 键帽：设计稿 .key，min-width 34 / padding 5×7 / 底部 1px 阴影 ───────
// 两屏共用：键位映射屏的规则行、设备与连接屏的折叠区。
class FgKeycap : public QWidget {
    Q_OBJECT
public:
    explicit FgKeycap(const QString &text, bool ghost = false,
                      QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
    bool ghost_;
};

// ── 下拉选择：替代 QComboBox ──────────────────────────────────────────
//
// 不用 QComboBox 的原因：设计稿的 .select 是 appearance:none + 自绘 chevron
// （ui.css .select-wrap .ic），而 Qt 样式表没法给 ::down-arrow 换一张随主题
// 变色的图——深色模式下原生箭头要么发黑要么发白，都不对。自己画才可控。
// 弹层用 QMenu，同样按 Theme 上色。
class FgSelect : public QAbstractButton {
    Q_OBJECT
public:
    FgSelect(const QStringList &options, int current = 0,
             QWidget *parent = nullptr);

    void setOptions(const QStringList &options);
    int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    QString currentText() const;

    QSize sizeHint() const override;

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void openMenu();

    QStringList options_;
    int current_ = 0;
    bool hovered_ = false;
};

} // namespace fg

#endif // FLOWGRID_WIDGETS_H
