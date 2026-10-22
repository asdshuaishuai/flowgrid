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
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLayout;
class QLineEdit;
class QHBoxLayout;
class QVBoxLayout;
class QGridLayout;
class QPropertyAnimation;
class QTimer;
class QVariantAnimation;
class QFocusEvent;
class QKeyEvent;

namespace fg {

// 给「带子控件的容器」加边框 / 底色时必须走这里。//
// 原因：Qt 样式表里一条**不带选择器**的声明会级联到该控件的所有子孙，典型
// 症状是容器里的每个 QLabel 各自长出一圈同样的边框（拖放区一度画成三条虚线框）。
// 这里统一生成 #objectName 限定的选择器，语义不再外溢到子控件。
void applyBoxStyle(QWidget *w, const QString &declarations);

// 清空一个布局里的全部控件，供各屏重建列表用。
//
// 光 takeAt + deleteLater 是不够的：控件脱离布局后仍然是父控件的子控件、
// 保留着上一轮的旧几何，要等事件循环转一圈才被真正销毁。这中间它照常绘制，
// 于是「一次事件循环内重建两次」就会在旧位置上叠出一层残影——剪贴板屏的
// 历史列表每敲一个搜索键就重建一次，正是这个场景，残影肉眼可见。
// 先 hide 再 deleteLater，那段窗口期就没有了。
void clearLayout(QLayout *layout);

// 应用字体基底：界面上所有字体的**唯一**来源。
//
// 为什么不能各处直接 QApplication::font()：
// DTK 在应用主题之后会把应用字体整个换掉（连 StyleStrategy 一起换），我们
// 之前在 main.cpp 里 setFont 设的「关掉次像素」当场被冲掉，浅色主题下字的
// 边缘重新长出橙蓝彩边。渲染策略指望不上应用字体带过来，就必须在每个派生
// 字体上显式写死——baseFont() 就是干这件事的地方，uiFont() 走它。
//
// 关次像素的理由与主题无关：Qt 会不会用次像素渲染取决于它读到的**系统调色板**
// 明暗，而这个界面的底色跟的是 DTK 主题（可跟系统，也可被设置中心切成另一种），
// 两者对不上时每个字都带橙蓝彩边。灰度抗锯齿没有彩边，代价只是边缘略软一点。
//
// 真正管用的是 NoSubpixelAntialias：真桌面上并排量过四种策略，默认(0x1) 有
// 彩边，只加 PreferAntialias(0x80) 一样有彩边，只有带上 NoSubpixelAntialias
// 才是 0。两个都留着，但起作用的只有后者。
QFont baseFont();

// 界面字号：设计稿的 10/11/12/13/20/28px，统一按像素字号落地。
QFont uiFont(int px, int weight = QFont::Normal);

// 设计稿 tokens.css 的 --font-mono 在 deepin 档是 "Noto Sans Mono"。等宽用在
// 剪贴板原文、延迟大数字、传输百分比、键位表格的标识符这几处，字号跨度大，
// 收成一个函数免得每处各写一遍 QFontFamilies 的兜底顺序。
QFont uiMonoFont(int px, int weight = QFont::Normal);

// 颜色转 CSS 字符串：不透明走 #RRGGBB，带 alpha 走 #AARRGGBB。
//
// 必须带 alpha 的那一支是重点：ok-soft / accent-soft 这类柔和底色就是靠
// 12%~18% 透明度叠出来的，用 #RRGGBB 会把它们渲染成实色，设计稿的浅底全废。
//
// 统一放这里而不是各处抄一份：这段函数此前在 10 个 .cpp 里各有一份副本，
// 注释还不一致（有的写「必须带 alpha」，有的什么都没写），漂移就是这么来的。
QString cssColor(const QColor &c);

// 把「按当前主题重新上色」挂到 scope 上；scope 被销毁时自动摘掉。
//
// 为什么需要它：文字色只有两种写法——写进 QLabel 的样式表，或写进自绘控件
// palette 的 WindowText 角色（实测样式表的 color 会落到 WindowText 上，自绘
// 控件因此也跟着变）。两种写法记下的都是**构造那一刻**的主题值：明暗切换后
// 它们停在旧值上，深色底上是一片近黑的字，浅色底上是一片看不见的白字。
//
// 于是凡是构造时把颜色写死的控件，都必须在 Theme::changed 时重刷一遍。
// 走这个函数而不是各自 connect，是为了让「主题变了要重刷」只有一种写法。
void bindTheme(QObject *scope, const std::function<void()> &restyle);

// 相对时间，两种写法，各有各的位置。
//
// agoText：「30 秒前 / 5 分钟前 / 3 天前」——用在句子里。
//     设计稿 connections.html 的设备副标题就是「离线 · 上次连接 12 分钟前」。
//
// compactDuration：「30s / 5m / 2h / 3d」——用在列表行末尾的时间标记。
//     设计稿 clipboard.html / files.html 的 .clip__time、.t-11 都是这个写法。
//
// 此前这三份各抄一遍，且单位还不统一（剪贴板收秒、文件收毫秒、连接收秒），
// 格式也是两套（"30s/5m/2h" 对 "30 秒前/5 分钟前/3 天前"）——同一屏里两种写法
// 挨着放，看着像两个产品。收到一处：单位一律是秒，格式按用途分开。
QString agoText(qint64 seconds);
QString compactDuration(qint64 seconds);

// 文件大小的人类可读写法：B / KB / MB / GB。
//
// 此前剪贴板屏与文件屏各有一份，一份支持 GB 一份不支持，一份 KB 恒 0 位小数
// 一份非整除时给 1 位——同一批文件在两屏里显示成两个数。
QString humanSize(qint64 bytes);

// ── 分隔线：1px hairline，左右可内缩 ───────────────────────────────────
//
// 设计稿里各区之间的分隔是一道 hairline，两侧各留一点空。
//
// 早先它是「1px 高的 QWidget + 样式表 margin: 3px 12px」——样式表的 margin
// 是在控件**内部**扣的：一个 1px 高的控件扣掉上下各 3px，内容区高度变成负数，
// 那条线根本画不出来。分隔线看着像不存在，两个区块就黏在一起。
// 这里自己画：控件整体高 7px（线上下各 3px），线本体 1px，左右按 inset 内缩。
class Divider : public QWidget {
    Q_OBJECT
public:
    explicit Divider(QWidget *parent = nullptr);

    // 左右各内缩多少像素。
    void setInset(int px);
    int inset() const { return inset_; }

    QSize sizeHint() const override { return {0, kDividerHeight}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    // 线本体 1px + 上下各 3px 留白。
    static constexpr int kDividerHeight = 7;
    int inset_ = 0;
};

// ── 状态点：7px 圆点，2s 呼吸 ────────────────────────────────────────
class StatusDot : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)
public:
    explicit StatusDot(QWidget *parent = nullptr);

    void setQuality(Quality q);
    void setPulsing(bool on);
    qreal pulse() const { return pulse_; }

    // 写完必须重绘。
    //
    // 呼吸动画是 QPropertyAnimation 写这个自定义属性推进的：属性系统的通知只
    // 走到 READ/WRITE 这对访问器上，不会替我们调 update()。少了这一句，圆点就
    // 停在初始那一帧——要么冻着不动，要么等到别处触发重绘时「啪」地跳一下，
    // 看着像卡顿而不是呼吸。
    void setPulse(qreal v);

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
//
// 勾选状态只有 QAbstractButton 那一份，这里不再自己存一份。早先留了个
// checked_ 影子，绘制与 isChecked() 都读影子，而点击翻的是基类状态：用户
// 按下去纹丝不动，回调里读到的还是旧值（「剪贴板同步」关不掉就是这么来的）。
// 过渡（ui.css .switch 的 200ms）由 toggled 驱动 progress_ 插值完成。
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {tokens::kSwitchWidth, tokens::kSwitchHeight}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    bool hovered_ = false;
    qreal progress_ = 0.0;                     // 0=关 1=开；过渡中的当前值
    QVariantAnimation *transition_ = nullptr;  // this 为父对象，无需手动释放
    // 焦点态用成员而不是 hasFocus()：hasFocus() 依赖窗口处于激活态，无头
    // 截图里恒为假，焦点环就永远审不到。focusIn/focusOut 是控件自己收到的事件。
    bool focused_ = false;
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
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    // 设计稿 .section__head:hover 是 surface-hi 底、:focus-visible 是 2px 主色
    // 内描边。头是唯一能点的区域，所以悬停判定要落在头上而不是整个折叠区。
    void refreshHeadState();
    void paintHeadFocusRing(QPainter &p);
    // .section 的框与底色。构造与主题切换共用这一处。
    void applySectionStyle();

    QWidget *body_ = nullptr;
    QWidget *head_ = nullptr;
    QLabel *chevron_ = nullptr;
    bool collapsed_ = false;
    bool headHovered_ = false;
    bool focused_ = false;
};

// ── 柱状图：70px 高 / 12 根 / 最近一根用主色 ──────────────────────────
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget *parent = nullptr);

    // 推入一个新样本；窗口满则挤出最旧的一个。
    void pushSample(double ms);
    void clear();

    // 首屏**不铺底**。早先这里会用当前读数加一段随机抖动填满 12 根柱子，
    // 好让折叠区一展开不是空白——但那 12 根柱子是从没量过的数，图表整片是假的。
    // 现在图表从空开始，随真实采样一根根长出来（每 2 秒一根，24 秒填满）；
    // 空的时候画布上写明「等待采样」，不留一根假柱子。

    QSize sizeHint() const override {
        return {120, tokens::kChartHeight};
    }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void animateTo(const std::vector<double> &target);

    // samples_ 是真值（刚从采样器拿到的数）；shown_ 是当前**画在屏幕上**的高度，
    // 由 500ms 过渡（设计稿 .chart__bar transition: height 500ms）插值过来。
    std::vector<double> samples_;
    std::vector<double> shown_;
    std::vector<double> from_;
    QVariantAnimation *anim_ = nullptr;
};

// ── 进度条：5px pill 轨道 + 主色填充（ui.css .bar）─────────────────────
class ProgressTrack : public QWidget {
    Q_OBJECT
public:
    explicit ProgressTrack(QWidget *parent = nullptr);

    // 填充百分比 0–100。改动走一段 --dur-micro 的过渡（同一个动画实例反复用，
    // 每帧自己收值，不经属性系统——原因见 .cpp）。
    void setProgress(int pct);
    int progress() const { return progress_; }

    QSize sizeHint() const override { return {120, kTrackHeight}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kTrackHeight = 5;   // ui.css .bar
    int progress_ = 0;
    QVariantAnimation *transition_ = nullptr;   // 惰性建：有的进度条只更新一两次
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
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    void refreshColors();
    QColor bg_;
    QColor fg_;

    QString text_;
    QString iconName_;
    Variant variant_;
    bool small_;
    bool hovered_ = false;
    bool focused_ = false;
};

// 尚未接通的功能统一走这里：记一条 qCInfo 说明是哪个功能没接通，再弹一条
// toast 让用户看见——不做「点了没反应」的按钮，也不假装它已经生效。
//
// 参数：text 是按钮上的字，what 是功能名（进日志与 toast），iconName 可空，
// small 是 24px 紧凑款，accent 是主色实心款，onClick 是额外要跑的回调。
//
// 此前这份逻辑在三屏各抄一份：设置屏那份只写日志不弹提示（用户点了屏幕
// 纹丝不动，实测「管理」「3 个」两个按钮都是这样），键位映射屏那份同样只
// 写日志，行为还不一致。统一成一份：一条 qCInfo + 一条 toast。
FgButton *unwiredButton(const QString &text, const QString &what, QWidget *parent,
                        const QString &iconName = QString(), bool small = false,
                        bool accent = false,
                        const std::function<void()> &onClick = {});

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
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    QString iconName_;
    bool hovered_ = false;
    bool focused_ = false;
};

// ── 设备卡：42px 图标 + 1fr 文本 + auto 侧栏 ──────────────────────────
class DeviceCard : public QFrame {
    Q_OBJECT
public:
    explicit DeviceCard(const Device &device, QWidget *parent = nullptr);

    void setDevice(const Device &device);
    QString deviceId() const { return id_; }

    // 固定副标题：盖掉 setDevice() 按设备字段拼出来的那一串。
    //
    // 设备卡自带的副标题是「系统 · 链路 · 地址 · 当前控制端」，那是设备列表
    // 的口径；连接屏那一行要的是 mac 的 meta(for:) 口径（「本机 · deepin 25」
    // 「Windows 11 · DirectLink · 已连接」）。两屏各用各的行文，就得有这个
    // 覆写口子，否则只能把整张卡重写一遍。
    //
    // 传空串恢复默认。
    void setMetaText(const QString &text);

    // 侧栏动作按钮：切换 / 重连 / 更多信息。离线设备给重连。
    void setAction(const QString &kind);   // "switch" | "reconnect" | "more" | "none"

    // 自定义尾部插槽：把默认的「延迟芯片 + 动作按钮」换掉。
    //
    // 对应 mac 端 FGDeviceRow 的 trailing 闭包——同一张设备卡，本机、对端、
    // 离线设备右侧要摆的东西完全不同（本机是「本机」徽章，对端是「控制中」
    // + 延迟 + 断开按钮）。不给出这个口子，这些差异就只能散在各个屏幕里
    // 各写一遍，或者退化成 mac 那样的一行纯文本。
    //
    // 传 nullptr 表示**交回默认**：延迟芯片 + 动作按钮重新露出来，自定义尾部
    // 被销毁。（此前这句写的是「不改变现状」，与实现对不上——实现是会回到默认
    // 的。）
    void setTrailingWidget(QWidget *widget);

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
    QString metaOverride_;   // 非空时盖掉默认副标题

    QLabel *icon_ = nullptr;
    QLabel *name_ = nullptr;
    QLabel *meta_ = nullptr;
    LatencyChip *latency_ = nullptr;
    IconButton *action_ = nullptr;
    QGridLayout *grid_ = nullptr;
    QWidget *trailing_ = nullptr;
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
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
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
    bool focused_ = false;
};

// ── 文字行：左标签 + 右控件，设置与列表两屏复用 ───────────────────────
class ListRow : public QFrame {
    Q_OBJECT
public:
    // titleWidget 非空时用它顶替纯文本主标题（要带图标时用；由本行接管生命周期）。
    ListRow(const QString &label, const QString &hint, QWidget *control,
            QWidget *parent = nullptr, QWidget *titleWidget = nullptr);

    void setHint(const QString &hint);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QLabel *hint_ = nullptr;
    bool hovered_ = false;
};

// ── 单行省略标签：设计稿的 .ellipsis ───────────────────────────────────
//
// QLabel 自己不画省略号，长文件名 / 长 mime / 长对端名会把整行撑破，浮板
// 又是定宽 520px，多出来的部分直接被裁掉。
//
// 关键在于横向要配 QSizePolicy::Ignored：QLayout 算最小宽度时看的是
// minimumSizeHint，只对 Ignored 才会算成 0。写成 setMinimumWidth(0) 是压不住的
// ——QWidgetItem 取的是 max(minimumSizeHint, minimumSize)，hint 仍然按全文算。
// 只设 Ignored 不设省略号也不行，文字会直接被裁掉半截。
class ElidedLabel : public QWidget {
    Q_OBJECT
public:
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr);

    void setFullText(const QString &text);
    QString fullText() const { return full_; }
    void setAlignmentFlag(Qt::Alignment a);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QString full_;
    Qt::Alignment align_ = Qt::AlignLeft | Qt::AlignVCenter;
};

// ── 剪贴板历史行：18px 类型块 + 双行文字 + 时间 + 写入按钮 ───────────────
//
// 设计稿 ui.css .list__row + .clip__type + .list__label/.list__hint。
// 单行 NavRow 装不下第二行 hint，尾部的「时间 + 图标按钮」又是一组而不是一个
// 控件，所以单开一个组件；行高、hover 底色、hairline 分隔与 .list__row 一致。
class ClipRow : public QWidget {
    Q_OBJECT
public:
    // leading 非空时用它顶替默认的 18px 类型块（文件传输屏要的是 28px 状态色
    // 图标，由调用方按 ok/warn/danger 取好色再传进来）；为空则按 iconName
    // 生成类型块。trailing 由调用方给出。
    ClipRow(const QString &iconName, const QString &text, const QString &hint,
            QWidget *trailing, QWidget *parent = nullptr, QWidget *leading = nullptr);

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    // 主题变了要重刷：类型块的图标、标题与提示三处的颜色都是构造那一刻写死的。
    void refreshColors();

    QWidget *leading_ = nullptr;
    QWidget *trailing_ = nullptr;
    QLabel *typeIcon_ = nullptr;     // 默认类型块里那枚图标
    ElidedLabel *title_ = nullptr;
    ElidedLabel *hint_ = nullptr;
    QString iconName_;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
};

// ── 空态：设计稿 ui.css .empty ─────────────────────────────────────────
//
// 40px 圆形图标底 + 13px 半粗标题 + 12px 说明。此前各处只摆一行居中文字，
// 列表空的时候看上去像加载坏了，而不是「本来就没有」。
class EmptyState : public QWidget {
    Q_OBJECT
public:
    EmptyState(const QString &iconName, const QString &title, const QString &desc,
               QWidget *parent = nullptr);
};

// ── Toast：设计稿 ui.css .toast ────────────────────────────────────────
//
// 按钮点了之后的反馈。设计稿八屏里到处是 data-action="toast"，而这些按钮此前
// 点了什么都没有——没成没败都不吭声，用户只能靠猜。
//
// 做成无焦点、不拦鼠标的顶层窗口：浮板是 Qt::Popup，点完就消失，Toast 得
// 挂在 QApplication 上才不会被浮板一起带走。
// Toast 的语义档：设计稿 app.js 里 toast(msg, kind) 会按 kind 选图标——
// 默认 info，ok 走 check，danger 走 warn。
enum class ToastKind { Info, Ok, Danger };

// 设计稿 .toast 里的 .ic 是 16px。
inline constexpr int kToastIcon = 16;

// 窗口四周给阴影（0 10px 26px）预留的透明边距。
inline constexpr int kToastMargin = 18;

class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent = nullptr);

    // 显示 text 并按 --dur-toast 自动收起。同一时刻只保留一个：再来一条就接着
    // 上一条的动画往下改文案，而不是让两层叠起来。
    // 刻意不叫 show(QString)：QWidget::show() 无参，名字一撞就成了「重载取
    // 零参数版本」，调用处的 Toast::show(text) 会编译不过。
    void showMessage(const QString &text, ToastKind kind = ToastKind::Info);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
    ToastKind kind_ = ToastKind::Info;
    QPropertyAnimation *anim_ = nullptr;
    // 收起定时器全应用复用这一个。早先用 QTimer::singleShot 每唤一次排一个、
    // 旧的又不撤销：第二条提示刚滑上来，前一条排的收起就会把它整块收走。
    QTimer *hideTimer_ = nullptr;
};

// 全应用共用同一个 Toast 实例。
//
// 必须挂在 QApplication 而不是某个浮板上：浮板是 Qt::Popup，唤出即抢走输入、
// 关闭即销毁，提示挂在它身上会跟着一起没，用户刚点的反馈就没了。
void toast(const QString &text, ToastKind kind = ToastKind::Info);

// 当前那个 Toast 实例。
//
// 它是**首次调用时被创建**的单例（还没唤出过时这里会顺手建一个），所以
// 拿到的指针永远非空——早先的注释写着「还没唤出过就是 nullptr」，与实现不符，
// 按那个注释写的调用方会误判。
//
// 只给截图模式用：Toast 是独立顶层窗，浮板的 w->grab() 抓不到它——早先 Toast
// 的药丸被内缩压成 3px 高、底色等于没画，就这么一路发出去没人发现。
QWidget *activeToast();

// ── 提示条：设计稿 ui.css .banner ───────────────────────────────────────
//
// 四屏都要用（设置 / 配对 / 键位映射 / 剪贴板），此前每屏各抄一份，三份的
// padding、圆角、图标尺寸已经开始漂。这里收成一处：三档变体只决定取哪一族
// 语义色，几何完全一致。
//
// 六档变体与 ui.css 的 .banner / .banner--{warn,danger,ok,neutral} 一一对应。
// info 走 accent 族（accentLine / accentSoft / accentText 三个角色齐全），
// neutral 走中性面（line-2 描边 + surface-2 底 + text-2），warn / danger /
// ok 三族 Palette 里只有 solid / soft / text，没有 line，描边按 solid 压一档
// 透明度派生——宁可从语义角色派生，也不写死主题色。
enum class BannerVariant { Info, Warn, Danger, Ok, Neutral };

QWidget *banner(const QString &text, QWidget *parent, BannerVariant variant);

// ── 列表分组标题：设计稿 ui.css .list__group-title ──────────────────────
//
// 它是 .list 卡片**里面**的头一行（surface-2 底 + 下 hairline），不是浮在卡片
// 外的小标题——后者没有底色也没有那条分隔线，一眼看过去像另一个层级。
// 五屏都要用（设置 / 连接 / 延迟 / 剪贴板 / 文件），此前每屏各写一份，padding
// 与底色已经开始漂，这里收成一处。
QLabel *groupTitle(const QString &text, QWidget *parent);

// ── 分段控件：设计稿 ui.css .seg ────────────────────────────────────────
//
// 外框 padding 2 / gap 2 / r-ctl 圆角 / surface-3 底；每档 24px 高、
// 左右 10px 内边距，选中档换成 surface 底 + shadow-bar。
// 剪贴板屏的类型过滤用它，设计控制条同款。
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    // items 的 text 是显示文字，data 是数据（选中后由 currentData() 给出）。
    explicit SegmentedControl(const QStringList &items, QWidget *parent = nullptr);

    int currentIndex() const { return current_; }
    QVariant currentData() const;
    void setCurrentIndex(int index);

    QSize sizeHint() const override;

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    struct Segment {
        QString text;
        QVariant data;
        QSize size;
    };

    int hitTest(int x) const;

    QList<Segment> segments_;
    int current_ = 0;
    int hover_ = -1;
    bool focused_ = false;
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

    // 键帽上的字可以换（设置屏的「切换热键」键帽跟着配置走），宽度随之重算。
    void setText(const QString &text);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    // ui.css .key 用 var(--font-mono)：键位串按等宽画出来宽窄才一致，
    // 否则 "Alt" 比 "Win" 宽，同一行键帽的边缘对不齐。
    static QFont keycapFont() { return uiMonoFont(tokens::kFont11, QFont::DemiBold); }

    QString text_;
    bool ghost_;
};

// ── 键位映射网格：设计稿 ui.css .mapping ───────────────────────────────
//
// 3 列等宽网格，每格是一个 .keypair（竖排：[源键] → [目标键]，下方一行说明）。
// 键位映射屏的「全局」区和设备与连接屏的折叠区是同一套结构，抽出来免得两处
// 的列数、间距、padding 各漂一点。
struct KeyPair {
    QString from;
    QString to;
    QString note;         // 键帽下方那一行说明
    bool ghost = false;   // .key--ghost：虚线描边 + text-3（未设置 / 点击添加）
};

QWidget *buildMappingGrid(const QList<KeyPair> &pairs, QWidget *parent);

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
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    void openMenu();

    QStringList options_;
    int current_ = 0;
    bool hovered_ = false;
    bool focused_ = false;
};

} // namespace fg

#endif // FLOWGRID_WIDGETS_H
