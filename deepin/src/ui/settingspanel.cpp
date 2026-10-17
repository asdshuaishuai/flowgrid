#include "settingspanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcSettingsPanel, "flowgrid.settings.panel")

namespace fg {
namespace {

QFont uiFont(int px, int weight = QFont::Normal)
{
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

// 设计稿 settings.html 剪贴板组给的档位。协议侧只认这三个值，早先用的自由
// 数字输入框能填出 37 这种没人认的数。
constexpr int kHistoryCounts[3] = {50, 100, 200};

int historyCountIndex(int count)
{
    for (int i = 0; i < 3; ++i) {
        if (kHistoryCounts[i] == count) {
            return i;
        }
    }
    return 0;
}

// 组容器：hairline 包裹的列表，替代整块描边（设计稿的「线 + 留白」层级）
QWidget *groupBox(const QString &title, const QList<QWidget *> &rows, QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(parent);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    auto *list = new QWidget(box);
    auto *ll = new QVBoxLayout(list);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    // 分组标题在卡片**里面**的头一行（设计稿 .list__group-title），不是浮在卡片
    // 外的小标题——后者没有底色也没有那条 hairline，一眼看过去像另一个层级。
    if (!title.isEmpty()) {
        ll->addWidget(groupTitle(title, list));
    }
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2))
                      .arg(tokens::kRadiusCard)
                      .arg(css(p.surface)));
    for (QWidget *row : rows) {
        ll->addWidget(row);
    }
    vl->addWidget(list);
    return box;
}

// ListRow 的 trailing 是单个控件，但有两行的右侧是「输入 + 单位」或
// 「键帽 + 输入」这种组合，ListRow 装不下整个行，只能另包一层当控件用。
QWidget *makeControlRow(QWidget *parent, const QList<QWidget *> &parts)
{
    auto *row = new QWidget(parent);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);
    for (QWidget *w : parts) {
        hl->addWidget(w);
    }
    return row;
}

QLabel *unitLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setFont(uiFont(tokens::kFont11));
    l->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    return l;
}

// 还没接通的功能一律明确记一条日志，不做成点了没反应的按钮，也不假装成功。
FgButton *unwiredButton(const QString &text, const QString &what, QWidget *parent)
{
    auto *btn = new FgButton(text, FgButton::Variant::Secondary, true, parent);
    QObject::connect(btn, &FgButton::clicked, parent, [what] {
        qCInfo(lcSettingsPanel) << what << QStringLiteral("尚未接通");
    });
    return btn;
}

} // namespace

SettingsPanel::SettingsPanel(QWidget *parent) : Panel(tr("设置"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    auto &settings = Settings::instance();

    body->addWidget(
        banner(tr("设置即时生效并同步到全部已配对设备，无需重启。"), this,
               BannerVariant::Info));
    body->addWidget(buildConnectionGroup());
    body->addWidget(buildInputGroup());
    body->addWidget(buildSyncGroup());
    body->addWidget(buildSecurityGroup());
    // 设计稿把这条警告放在安全组之后：它解释的正是上一行开关的后果。
    body->addWidget(banner(tr("关闭「新设备需确认」会让同一网络内的任意设备发起配对请求，"
                              "仅在可信网络中临时使用。"),
                           this, BannerVariant::Warn));
    setFooterWidget(buildFooter());

    QObject::connect(&settings, &Settings::changed, this, &SettingsPanel::reload);
    reload();
}

QWidget *SettingsPanel::buildConnectionGroup()
{
    // 用自绘的 FgSelect 而不是 QComboBox：原生下拉的箭头无法跟随主题
    // （样式表换不了 ::down-arrow 的图），深色下会是一块发白的默认外观，
    // 而且固定 170px 装不下 "IntelligentLink 优先"，文字被截成 "IntelligentLink ···"。
    preferredLink_ = new FgSelect({tr("IntelligentLink 优先"), tr("DirectLink 优先"),
                                   tr("仅 IntelligentLink")}, 0, this);
    preferredLink_->setMinimumWidth(168);
    auto &s = Settings::instance();
    const int idx = s.preferredLink() == QLatin1String("DirectLink")        ? 1
                    : s.preferredLink() == QLatin1String("IntelligentLinkOnly") ? 2
                                                                                : 0;
    preferredLink_->setCurrentIndex(idx);
    QObject::connect(preferredLink_, &FgSelect::currentIndexChanged, this, [this](int i) {
        Settings::instance().setPreferredLink(i == 1   ? QStringLiteral("DirectLink")
                                                 : i == 2 ? QStringLiteral("IntelligentLinkOnly")
                                                          : QStringLiteral("IntelligentLink"));
    });

    autoRouting_ = new ToggleSwitch(this);
    autoRouting_->setChecked(s.autoRouting());
    QObject::connect(autoRouting_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setAutoRouting(autoRouting_->isChecked());
    });

    autoStart_ = new ToggleSwitch(this);
    autoStart_->setChecked(s.autoStart());
    QObject::connect(autoStart_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setAutoStart(autoStart_->isChecked());
    });

    return groupBox(tr("连接"),
                    {new ListRow(tr("优先链路"),
                                 tr("IntelligentLink 不可用时回落到 DirectLink"),
                                 preferredLink_, this),
                     new ListRow(tr("自动路由"), tr("按延迟自动选择中转节点"), autoRouting_, this),
                     new ListRow(tr("开机自启"), tr("登录后托盘常驻"), autoStart_, this)},
                    this);
}

QWidget *SettingsPanel::buildInputGroup()
{
    auto &s = Settings::instance();

    // 设计稿这一行是「编辑」按钮而不是开关：键位映射在独立浮板里编辑，
    // 这里的开关没有第二处能改，纯属重复入口。开关键位映射的是键位浮板自己。
    auto *editKeymap = new FgButton(tr("编辑"), FgButton::Variant::Secondary, true, this);
    QObject::connect(editKeymap, &FgButton::clicked, this, [this] {
        Q_EMIT keymapRequested();
    });

    smoothing_ = new ToggleSwitch(this);
    smoothing_->setChecked(s.mouseSmoothing());
    QObject::connect(smoothing_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setMouseSmoothing(smoothing_->isChecked());
    });

    edgeThreshold_ = new QLineEdit(QString::number(s.edgeThreshold()), this);
    applyInputStyle(edgeThreshold_);
    edgeThreshold_->setFixedWidth(64);
    QObject::connect(edgeThreshold_, &QLineEdit::editingFinished, this, [this] {
        Settings::instance().setEdgeThreshold(edgeThreshold_->text().toInt());
    });

    switchHotkey_ = new QLineEdit(QString::number(s.switchHotkey()), this);
    applyInputStyle(switchHotkey_);
    switchHotkey_->setFixedWidth(44);
    QObject::connect(switchHotkey_, &QLineEdit::editingFinished, this, [this] {
        Settings::instance().setSwitchHotkey(switchHotkey_->text().toInt());
    });

    return groupBox(tr("输入"),
           {new ListRow(tr("键位映射"), tr("跨平台修饰键转换"), editKeymap, this),
            new ListRow(tr("鼠标平滑"), tr("降低跨屏移动抖动"), smoothing_, this),
            new ListRow(tr("边缘切换阈值"), tr("指针抵达屏幕边缘后触发"),
                        makeControlRow(this, {edgeThreshold_, unitLabel(tr("px"), this)}),
                        this),
            // 设计稿的 trailing 是修饰键键帽（按住后按数字直切）。我们把数字
            // 也留着可编辑，所以按 .keys 的「键帽 + 输入」排，而不是只画键帽。
            new ListRow(tr("切换热键"), tr("按住后按数字键直切"),
                        makeControlRow(this,
                                       {new FgKeycap(QStringLiteral("Ctrl"), false, this),
                                        switchHotkey_}),
                        this)},
           this);
}

QWidget *SettingsPanel::buildSyncGroup()
{
    auto &s = Settings::instance();

    clipSync_ = new ToggleSwitch(this);
    clipSync_->setChecked(s.clipboardSync());
    QObject::connect(clipSync_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setClipboardSync(clipSync_->isChecked());
    });

    // 设计稿是 50 / 100 / 200 三档下拉，不是自由输入。
    clipHistory_ = new FgSelect({tr("50 条"), tr("100 条"), tr("200 条")}, 0, this);
    clipHistory_->setMinimumWidth(96);
    clipHistory_->setCurrentIndex(historyCountIndex(s.clipboardHistory()));
    QObject::connect(clipHistory_, &FgSelect::currentIndexChanged, this, [](int i) {
        if (i >= 0 && i < 3) {
            Settings::instance().setClipboardHistory(kHistoryCounts[i]);
        }
    });

    confirm_ = new ToggleSwitch(this);
    confirm_->setChecked(s.requireConfirmation());
    QObject::connect(confirm_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setRequireConfirmation(confirm_->isChecked());
    });

    return groupBox(tr("剪贴板"),
           {new ListRow(tr("跨端同步"), tr("文本与图片，单条上限 63,240 B"), clipSync_, this),
            new ListRow(tr("历史条数"), tr("超出后按时间淘汰"), clipHistory_, this),
            new ListRow(tr("排除应用"), tr("密码管理器等窗口内不采集"),
                        unwiredButton(tr("3 个"), tr("排除应用列表"), this), this)},
           this);
}

QWidget *SettingsPanel::buildSecurityGroup()
{
    // 端到端加密是协议层的硬约束（PROTOCOL.md §3），不是用户能关的开关。
    // 早先这里摆的是一个禁用开关，视觉上像"可以改但改不了"；设计稿给的是
    // 徽章，直接说清状态。
    auto *e2eBadge = new Badge(tr("已启用"), Badge::Variant::Ok, this);
    QObject::connect(&Theme::instance(), &Theme::changed, e2eBadge, [e2eBadge] {
        e2eBadge->update();
    });

    return groupBox(tr("安全"),
           {new ListRow(tr("端到端加密"), tr("XChaCha20-Poly1305"), e2eBadge, this),
            new ListRow(tr("新设备需确认"), tr("关闭后任何局域网设备可直接配对"),
                        confirm_, this),
            new ListRow(tr("已授权设备"), tr("可单独撤销信任"),
                        unwiredButton(tr("管理"), tr("授权设备管理"), this), this)},
           this);
}

QWidget *SettingsPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    auto *reset = new FgButton(tr("恢复默认"), FgButton::Variant::Ghost, true, foot);
    QObject::connect(reset, &FgButton::clicked, this, &SettingsPanel::restoreDefaults);
    hl->addWidget(reset);
    hl->addStretch(1);

    // 设置是即时生效的，「取消」等价于放弃本次改动：重读一遍配置即可回到打开时的状态。
    auto *cancel = new FgButton(tr("取消"), FgButton::Variant::Secondary, true, foot);
    QObject::connect(cancel, &FgButton::clicked, this, [this] {
        reload();
        hide();
    });
    hl->addWidget(cancel);

    auto *save = new FgButton(tr("保存"), FgButton::Variant::Accent, true, foot);
    QObject::connect(save, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(save);
    return foot;
}

void SettingsPanel::restoreDefaults()
{
    Settings::instance().resetToDefaults();
    reload();
}

void SettingsPanel::reload()
{
    auto &s = Settings::instance();
    if (preferredLink_ != nullptr) {
        const int idx = s.preferredLink() == QLatin1String("DirectLink")        ? 1
                        : s.preferredLink() == QLatin1String("IntelligentLinkOnly") ? 2
                                                                                    : 0;
        if (preferredLink_->currentIndex() != idx) {
            preferredLink_->setCurrentIndex(idx);
        }
    }
    const auto sync = [](ToggleSwitch *w, bool v) {
        if (w != nullptr && w->isChecked() != v) {
            w->setChecked(v);
        }
    };
    sync(autoRouting_, s.autoRouting());
    sync(autoStart_, s.autoStart());
    sync(smoothing_, s.mouseSmoothing());
    sync(clipSync_, s.clipboardSync());
    sync(confirm_, s.requireConfirmation());

    if (edgeThreshold_ != nullptr) {
        edgeThreshold_->setText(QString::number(s.edgeThreshold()));
    }
    if (switchHotkey_ != nullptr) {
        switchHotkey_->setText(QString::number(s.switchHotkey()));
    }
    if (clipHistory_ != nullptr) {
        clipHistory_->setCurrentIndex(historyCountIndex(s.clipboardHistory()));
    }
}

} // namespace fg
