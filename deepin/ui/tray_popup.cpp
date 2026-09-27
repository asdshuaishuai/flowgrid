#include "tray_popup.h"

#include "chip.h"
#include "theme.h"

#include <DPalette>
#include <DFrame>
#include <DMessageManager>
#include <DLabel>
#include <DSwitchButton>

#include <QGridLayout>
#include <QHBoxLayout>
#include <DGuiApplicationHelper>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

using namespace DTK_WIDGET_NAMESPACE;
using namespace DTK_GUI_NAMESPACE;

namespace {

constexpr int kWidth = 320;

// menu item row: full-width hover highlight without QSS (gotcha 2.1 —
// never paint DTK widgets with stylesheets)
class MenuItemRow : public QFrame {
public:
    MenuItemRow(const QString &text, bool danger, std::function<void()> onClick,
                QWidget *parent = nullptr)
        : QFrame(parent), m_onClick(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(26);
        setFrameShape(QFrame::NoFrame);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 2, 6, 2);
        m_label = new QLabel(text);
        DPalette pl = m_label->palette();
        pl.setColor(DPalette::WindowText,
                    danger ? Theme::kBad
                           : DGuiApplicationHelper::instance()->applicationPalette().color(DPalette::WindowText));
        m_label->setPalette(pl);
        layout->addWidget(m_label);
    }

protected:
    void enterEvent(QEnterEvent *) override {
        m_label->setAutoFillBackground(true);
        DPalette pl = m_label->palette();
        pl.setColor(DPalette::ItemBackground,
                    DGuiApplicationHelper::instance()->applicationPalette().color(DPalette::ItemBackground));
        m_label->setPalette(pl);
    }
    void leaveEvent(QEvent *) override { m_label->setAutoFillBackground(false); }
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton && m_onClick)
            m_onClick();
        QFrame::mouseReleaseEvent(ev);
    }

private:
    QLabel *m_label = nullptr;
    std::function<void()> m_onClick;
};

QLabel *sectionTitle(const QString &text) {
    auto *label = new DLabel(text);
    QFont f = label->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    label->setFont(f);
    DPalette pl = label->palette();
    pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    label->setPalette(pl);
    return label;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}

} // namespace

TrayPopup::TrayPopup(QWidget *parent)
    : DBlurEffectWidget(parent)
    , m_core(CoreClient::instance()) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    // native dde popup: blur behind window, mask follows theme
    setBlendMode(DBlurEffectWidget::BehindWindowBlend);
    setMaskColor(DBlurEffectWidget::AutoColor);
    setBlurRectXRadius(12);
    setBlurRectYRadius(12);
    setFixedWidth(kWidth);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 12, 14, 12);
    rootLayout->setSpacing(4);

    // header: FlowGrid + live status pill (design: .tray-head)
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("FlowGrid");
    title->setFont(QFont(title->font().family(), -1, QFont::Bold));
    header->addWidget(title);
    header->addStretch(1);
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(7, 7);
    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1;border-radius:3px;").arg(Theme::kSuccess.name()));
    m_latencyText = new QLabel("--");
    m_latencyText->setFont(QFont(m_latencyText->font().family(), -1, QFont::Bold));
    header->addWidget(m_statusDot);
    header->addWidget(m_latencyText);
    rootLayout->addLayout(header);

    // current device (design: .active-device)
    m_currentTitle = sectionTitle(tr("当前设备"));
    rootLayout->addWidget(m_currentTitle);
    m_currentDeviceCard = new QFrame;
    m_currentDeviceCard->setAutoFillBackground(true);
    {
        DPalette cardPl = m_currentDeviceCard->palette();
        cardPl.setColor(DPalette::ItemBackground, Theme::applicationPalette().color(DPalette::ItemBackground));
        m_currentDeviceCard->setPalette(cardPl);
    }
    auto *currentLayout = new QHBoxLayout(m_currentDeviceCard);
    currentLayout->setContentsMargins(8, 6, 8, 6);
    currentLayout->setSpacing(8);
    m_currentDeviceIcon = new Chip(Chip::Tile, "--");
    m_currentDeviceIcon->setFixedSize(28, 28);
    currentLayout->addWidget(m_currentDeviceIcon);
    m_currentDeviceName = new QLabel(tr("未连接设备"));
    m_currentDeviceName->setFont(QFont(m_currentDeviceName->font().family(), -1, QFont::Bold));
    currentLayout->addWidget(m_currentDeviceName, 1);
    rootLayout->addWidget(m_currentDeviceCard);

    // quick switch (design: .switch-row with Ctrl+N hints)
    m_quickTitle = sectionTitle(tr("快速切换"));
    rootLayout->addWidget(m_quickTitle);
    m_quickSwitchHost = new QWidget;
    m_quickSwitchLayout = new QGridLayout(m_quickSwitchHost);
    m_quickSwitchLayout->setContentsMargins(0, 0, 0, 0);
    m_quickSwitchLayout->setHorizontalSpacing(6);
    m_quickSwitchLayout->setVerticalSpacing(4);
    rootLayout->addWidget(m_quickSwitchHost);

    rootLayout->addWidget(separator());

    // devices (design: .tray-section Devices)
    m_devicesTitle = sectionTitle(tr("设备"));
    rootLayout->addWidget(m_devicesTitle);
    auto *deviceRowsHost = new QWidget;
    m_deviceRowsLayout = new QVBoxLayout(deviceRowsHost);
    m_deviceRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceRowsLayout->setSpacing(1);
    rootLayout->addWidget(deviceRowsHost);

    rootLayout->addWidget(separator());

    // quick actions (design: Key Mapping / Clipboard Sync / Mouse Smoothing)
    rootLayout->addWidget(sectionTitle(tr("快捷开关")));
    rootLayout->addWidget(toggleRow(tr("键位映射"), "keyMapping", false));
    rootLayout->addWidget(toggleRow(tr("剪贴板同步"), "clipboardSync", false));
    rootLayout->addWidget(toggleRow(tr("鼠标平滑"), "mouseSmoothing", false));
    rootLayout->addWidget(toggleRow(tr("控制其他设备"), QString(), true));

    rootLayout->addWidget(separator());

    // menu items (design: Show Connections / Latency / Quit)
    rootLayout->addWidget(new MenuItemRow(tr("显示主窗口"), false,
                                          [this] { emit showMainRequested(); close(); }, this));
    rootLayout->addWidget(new MenuItemRow(tr("延迟监控"), false,
                                          [this] { emit latencyRequested(); close(); }, this));
    rootLayout->addWidget(new MenuItemRow(tr("关于 FlowGrid"), false,
                                          [this] { emit aboutRequested(); close(); }, this));
    rootLayout->addWidget(separator());
    rootLayout->addWidget(new MenuItemRow(tr("退出 FlowGrid"), true,
                                          [this] { emit quitRequested(); close(); }, this));

    refresh();
    connect(&m_core, &CoreClient::latencyChanged, this, &TrayPopup::refresh);
    connect(&m_core, &CoreClient::devicesChanged, this, &TrayPopup::refresh);
    connect(&m_core, &CoreClient::settingsChanged, this, &TrayPopup::refresh);
}

void TrayPopup::popup() {
    refresh();
    move(QCursor::pos() - QPoint(kWidth / 2, 12));
    show();
    raise();
    activateWindow();
}

QLabel *TrayPopup::sectionTitle(const QString &text) {
    return ::sectionTitle(text);
}

QWidget *TrayPopup::toggleRow(const QString &label, const QString &key, bool usesCapturing) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 2, 4, 2);
    auto *labelWidget = new QLabel(label);
    layout->addWidget(labelWidget, 1);
    auto *switchButton = new DSwitchButton;
    switchButton->setChecked(usesCapturing ? m_core.capturing()
                                           : m_core.settings().value(key, true).toBool());
    layout->addWidget(switchButton);
    const auto announce = [this, label](bool on) {
        DMessageManager::instance()->sendMessage(
            this, QIcon(), tr("%1已%2").arg(label, on ? tr("启用") : tr("关闭")));
    };
    if (usesCapturing) {
        connect(switchButton, &DSwitchButton::toggled, &m_core,
                [this, switchButton, announce](bool checked) {
                    if (!m_core.setCapturing(checked)) {
                        QSignalBlocker blocker(switchButton);
                        switchButton->setChecked(!checked);
                        return;
                    }
                    announce(checked);
                });
        connect(&m_core, &CoreClient::capturingChanged, switchButton,
                [switchButton, announce](bool enabled) {
                    QSignalBlocker blocker(switchButton);
                    switchButton->setChecked(enabled);
                    announce(enabled);
                });
    } else {
        connect(switchButton, &DSwitchButton::toggled, &m_core,
                [this, key, announce](bool checked) {
                    m_core.setSetting(key, checked);
                    announce(checked);
                });
    }
    return row;
}

void TrayPopup::refresh() {
    const QString latency = m_core.latency();
    const bool connected = latency != QLatin1String("--");
    m_latencyText->setText(latency);
    m_latencyText->setStyleSheet(
        QStringLiteral("color:%1;").arg(connected ? Theme::kSuccess.name() : Theme::kWarn.name()));
    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1;border-radius:3px;")
            .arg(connected ? Theme::kSuccess.name()
                           : Theme::applicationPalette().color(DPalette::TextTips).name()));

    QVariantList connectedDevices;
    for (const QVariant &v : m_core.devices())
        if (v.toMap().value("connected").toBool())
            connectedDevices.append(v);

    // current device card
    const bool hasCurrent = !connectedDevices.isEmpty();
    m_currentDeviceCard->setVisible(hasCurrent);
    m_currentTitle->setVisible(hasCurrent);
    if (hasCurrent) {
        const QVariantMap d = connectedDevices.first().toMap();
        static_cast<Chip *>(m_currentDeviceIcon)->setText(d.value("name").toString().split(' ').value(0).left(2).toUpper());
        m_currentDeviceName->setText(d.value("name").toString());
    }

    // quick switch grid: up to 3 connected devices with Ctrl+N hints
    while (QLayoutItem *item = m_quickSwitchLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_quickSwitchHost->setVisible(!connectedDevices.isEmpty());
    m_quickTitle->setVisible(!connectedDevices.isEmpty());
    const QStringList keys = { "Ctrl+1", "Ctrl+2", "Ctrl+3" };
    for (int i = 0; i < connectedDevices.size() && i < 3; ++i) {
        const QVariantMap d = connectedDevices.at(i).toMap();
        auto *button = new QPushButton(
            QStringLiteral("%1\n%2").arg(d.value("name").toString().split(' ').value(0),
                                         keys.value(i)));
        button->setMinimumHeight(44);
        m_quickSwitchLayout->addWidget(button, 0, i);
        connect(button, &QPushButton::clicked, this, [this, i] {
            emit quickSwitchRequested(i);
            close();
        });
    }

    // device rows: name + status (design: Connected hint)
    while (QLayoutItem *item = m_deviceRowsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QVariantList devices = m_core.devices();
    m_devicesTitle->setVisible(!devices.isEmpty());
    if (devices.isEmpty()) {
        auto *row = new QLabel(tr("暂无设备"));
        DPalette pl = row->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        row->setPalette(pl);
        m_deviceRowsLayout->addWidget(row);
    }
    for (const QVariant &v : devices) {
        const QVariantMap d = v.toMap();
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        auto *name = new QLabel(d.value("name").toString());
        rowLayout->addWidget(name, 1);
        auto *status = new QLabel(d.value("connected").toBool() ? tr("已连接") : tr("空闲"));
        DPalette statusPl = status->palette();
        statusPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        status->setPalette(statusPl);
        rowLayout->addWidget(status);
        m_deviceRowsLayout->addWidget(row);
    }
    resize(kWidth, sizeHint().height());
}
