#include "settingspanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

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

// 分组标题：设计稿 .list__group-title，全大写 + 字距 + text-3
QLabel *groupTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    label->setContentsMargins(0, tokens::kS2, 0, tokens::kS1);
    label->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px")
            .arg(css(Theme::instance().palette().text3)));
    return label;
}

// 组容器：hairline 包裹的列表，替代整块描边（设计稿的「线 + 留白」层级）
QWidget *groupBox(const QString &title, const QList<QWidget *> &rows, QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(parent);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    if (!title.isEmpty()) {
        vl->addWidget(groupTitle(title, box));
    }
    auto *list = new QWidget(box);
    auto *ll = new QVBoxLayout(list);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
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

QWidget *banner(const QString &text, QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *w = new QWidget(parent);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
    hl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(w);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("info"), 14, p.text3).pixmap(14, 14));
    auto *label = new QLabel(text, w);
    label->setWordWrap(true);
    label->setFont(uiFont(tokens::kFont11));
    label->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(label, 1);
    applyBoxStyle(w, QStringLiteral("border-radius:%1px").arg(tokens::kRadiusCtl));
    return w;
}

} // namespace

SettingsPanel::SettingsPanel(QWidget *parent) : Panel(tr("设置"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    auto &settings = Settings::instance();

    body->addWidget(banner(tr("设置即时生效并同步到全部已配对设备，无需重启。"), this));
    body->addWidget(buildConnectionGroup());
    body->addWidget(buildInputGroup());
    body->addWidget(buildSyncGroup());
    body->addWidget(buildSecurityGroup());
    setFooterWidget(buildFooter());

    QObject::connect(&settings, &Settings::changed, this, &SettingsPanel::reload);
    reload();
}

QWidget *SettingsPanel::buildConnectionGroup()
{
    preferredLink_ = new QComboBox(this);
    preferredLink_->addItems({tr("IntelligentLink 优先"), tr("DirectLink 优先"),
                              tr("仅 IntelligentLink")});
    preferredLink_->setFixedWidth(170);
    auto &s = Settings::instance();
    const int idx = s.preferredLink() == QLatin1String("DirectLink")        ? 1
                    : s.preferredLink() == QLatin1String("IntelligentLinkOnly") ? 2
                                                                                : 0;
    preferredLink_->setCurrentIndex(idx);
    QObject::connect(preferredLink_, &QComboBox::currentIndexChanged, this, [this](int i) {
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
                    {new ListRow(tr("优先链路"), tr("无可用链路时自动回落"), preferredLink_, this),
                     new ListRow(tr("自动路由"), tr("按延迟自动选择中转节点"), autoRouting_, this),
                     new ListRow(tr("开机自启"), tr("登录后托盘常驻"), autoStart_, this)},
                    this);
}

QWidget *SettingsPanel::buildInputGroup()
{
    auto &s = Settings::instance();

    keymap_ = new ToggleSwitch(this);
    keymap_->setChecked(s.keymapEnabled());
    QObject::connect(keymap_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setKeymapEnabled(keymap_->isChecked());
    });

    smoothing_ = new ToggleSwitch(this);
    smoothing_->setChecked(s.mouseSmoothing());
    QObject::connect(smoothing_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setMouseSmoothing(smoothing_->isChecked());
    });

    edgeThreshold_ = new QLineEdit(QString::number(s.edgeThreshold()), this);
    edgeThreshold_->setFixedWidth(64);
    QObject::connect(edgeThreshold_, &QLineEdit::editingFinished, this, [this] {
        Settings::instance().setEdgeThreshold(edgeThreshold_->text().toInt());
    });

    switchHotkey_ = new QLineEdit(QString::number(s.switchHotkey()), this);
    switchHotkey_->setFixedWidth(64);
    QObject::connect(switchHotkey_, &QLineEdit::editingFinished, this, [this] {
        Settings::instance().setSwitchHotkey(switchHotkey_->text().toInt());
    });

    auto *edgeRow = new QWidget(this);
    auto *ehl = new QHBoxLayout(edgeRow);
    ehl->setContentsMargins(tokens::kS3, 9, tokens::kS3, 9);
    ehl->setSpacing(tokens::kS2);
    auto *edgeLabel = new QLabel(tr("边缘切换阈值"), edgeRow);
    edgeLabel->setFont(uiFont(tokens::kFont12));
    ehl->addWidget(edgeLabel);
    ehl->addStretch(1);
    ehl->addWidget(edgeThreshold_);
    auto *px = new QLabel(tr("px"), edgeRow);
    px->setFont(uiFont(tokens::kFont11));
    px->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    ehl->addWidget(px);

    return groupBox(tr("输入"),
           {new ListRow(tr("键位映射"), tr("跨平台修饰键转换"), keymap_, this),
            new ListRow(tr("鼠标平滑"), tr("降低跨屏移动抖动"), smoothing_, this),
            edgeRow,
            new ListRow(tr("切换热键"), tr("按住后按数字键直切"), switchHotkey_, this)},
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

    clipHistory_ = new QLineEdit(QString::number(s.clipboardHistory()), this);
    clipHistory_->setFixedWidth(64);
    QObject::connect(clipHistory_, &QLineEdit::editingFinished, this, [this] {
        Settings::instance().setClipboardHistory(clipHistory_->text().toInt());
    });

    fileTransfer_ = new ToggleSwitch(this);
    fileTransfer_->setChecked(s.fileTransfer());
    QObject::connect(fileTransfer_, &ToggleSwitch::clicked, this, [this] {
        Settings::instance().setFileTransfer(fileTransfer_->isChecked());
    });

    return groupBox(tr("跨端同步"),
           {new ListRow(tr("剪贴板"), tr("文本与图片，单条上限 8 MB"), clipSync_, this),
            new ListRow(tr("历史条数"), tr("超出后按时间淘汰"), clipHistory_, this),
            new ListRow(tr("文件传输"), tr("拖放或选择文件发送"), fileTransfer_, this)},
           this);
}

QWidget *SettingsPanel::buildSecurityGroup()
{
    auto &s = Settings::instance();

    // 端到端加密强制开启（PROTOCOL.md §3），所以这里只读不写，
    // 给一个禁用开关 + 「已启用」徽章，避免用户以为能关。
    auto *e2e = new ToggleSwitch(this);
    e2e->setChecked(s.e2eEncryption());
    e2e->setEnabled(false);
    auto *e2eRow = new ListRow(tr("端到端加密"), tr("XChaCha20-Poly1305"), e2e, this);

    auto *confirm = new ToggleSwitch(this);
    confirm->setChecked(s.requireConfirmation());
    confirm->setEnabled(false);   // 只读项：本轮未实现配对确认流

    return groupBox(tr("安全"),
           {e2eRow,
            new ListRow(tr("新设备需确认"), tr("关闭后任何局域网设备可直接配对"), confirm, this)},
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
    sync(keymap_, s.keymapEnabled());
    sync(smoothing_, s.mouseSmoothing());
    sync(clipSync_, s.clipboardSync());
    sync(fileTransfer_, s.fileTransfer());

    if (edgeThreshold_ != nullptr) {
        edgeThreshold_->setText(QString::number(s.edgeThreshold()));
    }
    if (clipHistory_ != nullptr) {
        clipHistory_->setText(QString::number(s.clipboardHistory()));
    }
}

} // namespace fg
