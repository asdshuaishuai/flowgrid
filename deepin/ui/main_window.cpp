#include "main_window.h"

#include "add_device_dialog.h"
#include "chip.h"
#include "theme.h"

#include <DPalette>
#include <DBackgroundGroup>
#include <dheaderline.h>
#include <DWarningButton>
#include <DSuggestButton>
#include <DSwitchButton>
#include <DFrame>
#include <DLabel>
#include <DMessageManager>

#include <QCloseEvent>
#include <QDrag>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <QVBoxLayout>

using namespace DTK_WIDGET_NAMESPACE;
using namespace DTK_GUI_NAMESPACE;

static constexpr const char *kMimeDevice = "application/x-flowgrid-device";

namespace {

// Latency pill: ItemBackground pill + semantic text color (design: good/warn/bad).
QWidget *latencyPill(const QString &text, QWidget *ref, double ms) {
    auto *pill = new Chip(Chip::Pill, text);
    pill->setFgColor(Theme::latencyColor(ms));
    Q_UNUSED(ref);
    return pill;
}

// Flat icon action button (design: .icon-btn glyph buttons).
DIconButton *iconButton(QStyle::StandardPixmap pixmap, const QString &toolTip) {
    auto *button = new DIconButton(pixmap);
    button->setToolTip(toolTip);
    button->setFixedSize(26, 26);
    button->setFlat(true);
    return button;
}

QLabel *makeDot(const QColor &color) {
    auto *dot = new QLabel;
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QString("background:%1;border-radius:4px;").arg(color.name()));
    return dot;
}

bool parseMs(const QString &text, double *out) {
    bool ok = false;
    const double v = text.toDouble(&ok);
    *out = v;
    return ok;
}

// Toggles a collapsible section when its header is clicked.
class SectionToggle : public QObject {
public:
    SectionToggle(MainWindow *window, QWidget *body, QLabel *chevron)
        : QObject(body), m_window(window), m_body(body), m_chevron(chevron) {}
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            m_body->setVisible(m_body->isHidden());
            m_chevron->setText(m_body->isHidden() ? "▶" : "▼");
            Q_UNUSED(m_window);
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    MainWindow *m_window;
    QWidget *m_body;
    QLabel *m_chevron;
};

// Starts a drag carrying the device id when an occupied cell is pressed.
class CellDragHandler : public QObject {
public:
    CellDragHandler(QFrame *frame) : QObject(frame), m_frame(frame) {}
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(ev);
            if (me->button() == Qt::LeftButton) {
                const QString id = m_frame->property("dragId").toString();
                if (id.isEmpty())
                    return false;
                QDrag *drag = new QDrag(m_frame);
                auto *mime = new QMimeData;
                mime->setData(kMimeDevice, id.toUtf8());
                drag->setMimeData(mime);
                drag->exec(Qt::MoveAction);
            }
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    QFrame *m_frame;
};

// Accepts drops of device ids into a free grid slot.
class CellDropHandler : public QObject {
public:
    CellDropHandler(MainWindow *window, QFrame *frame)
        : QObject(frame), m_window(window), m_frame(frame) {}
    bool eventFilter(QObject *, QEvent *ev) override {
        switch (ev->type()) {
        case QEvent::DragEnter:
            static_cast<QDragEnterEvent *>(ev)->acceptProposedAction();
            return true;
        case QEvent::DragMove:
            static_cast<QDragMoveEvent *>(ev)->acceptProposedAction();
            return true;
        case QEvent::Drop: {
            auto *de = static_cast<QDropEvent *>(ev);
            const QString id = QString::fromUtf8(de->mimeData()->data(kMimeDevice));
            if (!id.isEmpty()) {
                m_window->moveDeviceToSlot(id, m_frame->property("gridX").toInt(),
                                           m_frame->property("gridY").toInt());
                de->acceptProposedAction();
            }
            return true;
        }
        default:
            return false;
        }
    }
private:
    MainWindow *m_window;
    QFrame *m_frame;
};

} // namespace

// public: used by CellDropHandler
void MainWindow::moveDeviceToSlot(const QString &id, int gridX, int gridY) {
    for (LayoutSlot &s : m_layout)
        if (s.deviceId == id) {
            s.gridX = gridX;
            s.gridY = gridY;
        }
    const bool exists = std::any_of(m_layout.cbegin(), m_layout.cend(),
                                    [&](const LayoutSlot &s) { return s.deviceId == id; });
    if (!exists)
        m_layout.append({ gridX, gridY, id });
    rebuildLayoutGrid();
    showToast(tr("布局已更新"));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_core(CoreClient::instance()) {
    setWindowTitle("FlowGrid");
    resize(520, 700);
    setMinimumSize(480, 600);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    setCentralWidget(scroll);

    auto *content = new QWidget(scroll);
    m_mainLayout = new QVBoxLayout(content);
    m_mainLayout->setContentsMargins(14, 8, 14, 14);
    m_mainLayout->setSpacing(10);

    m_mainLayout->addWidget(buildToolbar());
    m_mainLayout->addWidget(buildDeviceSection());
    m_mainLayout->addWidget(buildKeymapSection());
    m_mainLayout->addWidget(buildModeSection());
    m_mainLayout->addWidget(buildLayoutSection());
    m_mainLayout->addWidget(buildSettingsSection());
    m_mainLayout->addStretch(1);
    scroll->setWidget(content);

    rebuildDeviceList();
    rebuildRules();
    rebuildLayoutGrid();
    refreshConnectionStatus();

    connect(&m_core, &CoreClient::devicesChanged, this, &MainWindow::rebuildDeviceList);
    connect(&m_core, &CoreClient::keymapChanged, this, &MainWindow::rebuildRules);
    connect(&m_core, &CoreClient::errorOccurred, this, &MainWindow::showToast);
    connect(&m_core, &CoreClient::latencyChanged, this, &MainWindow::rebuildDeviceList);
}

QWidget *MainWindow::buildToolbar() {
    auto *wrap = new QWidget;
    auto *layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    m_statusDot = makeDot(Theme::applicationPalette().color(DPalette::TextTips));
    m_statusLabel = new QLabel(tr("未连接任何设备"));
    auto *addButton = new DSuggestButton(tr("添加设备"));
    layout->addWidget(m_statusDot);
    layout->addWidget(m_statusLabel, 1);
    layout->addWidget(addButton);
    connect(addButton, &QPushButton::clicked, this, [this] {
        AddDeviceDialog dialog(m_core, this);
        dialog.exec();
    });
    return wrap;
}

QWidget *MainWindow::buildDeviceSection() {
    auto *header = new DHeaderLine;
    header->setTitle(tr("设备"));
    m_mainLayout->addWidget(header);

    m_deviceListHost = new QWidget;
    m_deviceListLayout = new QVBoxLayout(m_deviceListHost);
    m_deviceListLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceListLayout->setSpacing(6);

    m_deviceGroup = new DBackgroundGroup(m_deviceListLayout);
    m_deviceGroup->setItemMargins(QMargins(10, 8, 10, 8));
    m_mainLayout->addWidget(m_deviceGroup);
    return m_deviceGroup;
}

QWidget *MainWindow::makeTile(const QString &text, int size) {
    auto *tile = new Chip(Chip::Tile, text);
    tile->setFixedSize(size, size);
    return tile;
}

QWidget *MainWindow::buildDeviceCard(const QVariantMap &device) {
    const QString id = device.value("id").toString();
    const bool connected = device.value("connected").toBool();
    const QString name = device.value("name").toString();
    const QString latencyText = device.value("latency").toString();
    double ms = 0;
    parseMs(latencyText, &ms);

    auto *card = new QWidget;
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    layout->addWidget(makeTile(platformAbbrev(device.value("platform").toString()), 42));

    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(3);
    auto *nameLabel = new QLabel(name);
    nameLabel->setFont(QFont(nameLabel->font().family(), -1, QFont::DemiBold));
    auto *meta = new QLabel(device.value("meta").toString());
    DPalette metaPl = meta->palette();
    metaPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    meta->setPalette(metaPl);
    textColumn->addWidget(nameLabel);
    textColumn->addWidget(meta);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    actions->addWidget(latencyPill(latencyText, card, connected ? ms : 0));
    if (connected) {
        auto *toggle = iconButton(QStyle::SP_MediaStop, tr("断开"));
        connect(toggle, &QToolButton::clicked, &m_core, [this, id, name] {
            m_core.disconnectDevice(id);
            showToast(tr("已断开 %1").arg(name));
        });
        actions->addWidget(toggle);
    } else {
        auto *toggle = iconButton(QStyle::SP_BrowserReload, tr("连接"));
        connect(toggle, &QToolButton::clicked, &m_core, [this, id, name] {
            m_core.connectDevice(id);
            showToast(tr("正在连接 %1").arg(name));
        });
        actions->addWidget(toggle);
    }
    auto *remove = iconButton(QStyle::SP_TrashIcon, tr("移除"));
    connect(remove, &QToolButton::clicked, &m_core, [this, id, name] {
        m_core.removeDevice(id);
        showToast(tr("已移除 %1").arg(name));
    });
    actions->addWidget(remove);

    layout->addLayout(textColumn, 1);
    layout->addLayout(actions);
    return card;
}

QWidget *MainWindow::buildCollapsibleSection(Section *section, const QString &title,
                                             const QString &badgeText, QWidget *body) {
    auto *head = new QWidget;
    head->setCursor(Qt::PointingHandCursor);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(4, 6, 4, 6);
    headLayout->setSpacing(8);
    auto *titleLabel = new QLabel(title);
    titleLabel->setFont(QFont(titleLabel->font().family(), -1, QFont::Bold));
    headLayout->addWidget(titleLabel);
    if (!badgeText.isEmpty()) {
        m_keymapBadge = new Chip(Chip::Badge, badgeText);
        headLayout->addWidget(m_keymapBadge);
    }
    headLayout->addStretch(1);
    auto *chevron = new QLabel("▼");
    DPalette chevPl = chevron->palette();
    chevPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    chevron->setPalette(chevPl);
    headLayout->addWidget(chevron);

    auto *wrap = new QWidget;
    auto *layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(head);
    layout->addWidget(body);

    head->installEventFilter(new SectionToggle(this, body, chevron));

    section->root = wrap;
    section->body = body;
    section->chevron = chevron;
    return wrap;
}

QWidget *MainWindow::buildKeymapSection() {
    m_keymapRulesLayout = new QVBoxLayout;
    m_keymapRulesLayout->setContentsMargins(10, 8, 10, 8);
    m_keymapRulesLayout->setSpacing(8);

    auto *group = new DBackgroundGroup(m_keymapRulesLayout);

    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(group);

    return buildCollapsibleSection(&m_keymapSection, tr("自动键位映射"), QString(), body);
}

void MainWindow::rebuildRules() {
    auto *outer = m_keymapRulesLayout;
    if (!outer)
        return;
    while (QLayoutItem *item = outer->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    QVariantList rules;
    for (const QVariant &r : m_core.keymapRules()) {
        const QVariantMap m = r.toMap();
        if (m.value("from_key").toString() != QLatin1String("0x0000")
            && m.value("to_key").toString() != QLatin1String("0x0000"))
            rules.append(r);
    }

    // direction badge in the section head (design: "Windows → macOS")
    QString direction;
    for (const QVariant &v : m_core.devices()) {
        const QVariantMap d = v.toMap();
        if (d.value("connected").toBool()) {
            direction = tr("Linux → %1").arg(d.value("platform").toString());
            break;
        }
    }
    if (m_keymapBadge)
        m_keymapBadge->setText(direction.isEmpty() ? tr("热更新生效") : direction);
    m_keymapBadge->setVisible(true);
    if (rules.isEmpty())
        return;

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    const int shown = qMin(rules.size(), 6);
    for (int i = 0; i < shown; ++i) {
        const QVariantMap rule = rules.at(i).toMap();
        auto *column = new QVBoxLayout;
        column->setSpacing(5);

        auto *keys = new QHBoxLayout;
        keys->setSpacing(5);
        auto *fromKey = new Chip(Chip::Key, keyName(rule.value("from_key").toString()));
        auto *arrow = new QLabel("→");
        auto *toKey = new Chip(Chip::Key, keyName(rule.value("to_key").toString()));
        keys->addWidget(fromKey);
        keys->addWidget(arrow);
        keys->addWidget(toKey);
        keys->addStretch(1);

        auto *context = new QLabel(contextLabel(rule.value("context").toString()));
        DPalette ctxPl = context->palette();
        ctxPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        context->setPalette(ctxPl);

        column->addLayout(keys);
        column->addWidget(context);
        grid->addLayout(column, i / 3, i % 3);
    }
    outer->addLayout(grid);
}

QPushButton *MainWindow::modeOptionCard(const QString &name, const QString &desc, const QString &badgeText) {
    auto *card = new QPushButton;
    card->setCursor(Qt::PointingHandCursor);
    card->setCheckable(true);
    card->setMinimumHeight(64);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(3);
    auto *nameLabel = new QLabel(name);
    nameLabel->setFont(QFont(nameLabel->font().family(), -1, QFont::Bold));
    nameLabel->setAlignment(Qt::AlignCenter);
    auto *descLabel = new QLabel(desc);
    descLabel->setAlignment(Qt::AlignCenter);
    DPalette descPl = descLabel->palette();
    descPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    descLabel->setPalette(descPl);
    auto *badgeLabel = new QLabel(badgeText);
    badgeLabel->setAlignment(Qt::AlignCenter);
    DPalette badgePl = badgeLabel->palette();
    badgePl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::Highlight));
    badgeLabel->setPalette(badgePl);
    layout->addWidget(nameLabel);
    layout->addWidget(descLabel);
    layout->addWidget(badgeLabel);
    return card;
}

QWidget *MainWindow::buildModeSection() {
    auto *body = new QWidget;
    auto *outer = new QVBoxLayout(body);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *gridLayout = new QGridLayout;
    gridLayout->setContentsMargins(10, 8, 10, 8);
    gridLayout->setHorizontalSpacing(8);
    gridLayout->setVerticalSpacing(8);

    const struct { const char *name; const char *desc; const char *badge; } modes[] = {
        { "Auto", "智能路由", "Active" },
        { "NearLink", "BLE + Wi-Fi Direct", "首选" },
        { "DirectLink", "Raw Ethernet", "备用" },
    };
    for (int i = 0; i < 3; ++i) {
        auto *card = modeOptionCard(modes[i].name, modes[i].desc, modes[i].badge);
        card->setChecked(i == m_modeIndex);
        connect(card, &QPushButton::toggled, this, [this, i](bool checked) {
            if (!checked)
                return;
            m_modeIndex = i;
            for (int k = 0; k < m_modeButtons.size(); ++k)
                m_modeButtons[k]->setChecked(k == i);
            showToast(tr("已切换到 %1 模式").arg(m_modeNames.value(i)));
        });
        m_modeButtons.append(card);
        gridLayout->addWidget(card, 0, i);
    }

    auto *group = new QWidget;
    group->setLayout(gridLayout);
    outer->addWidget(group);
    return buildCollapsibleSection(&m_modeSection, tr("连接模式"), QString(), body);
}

QWidget *MainWindow::buildLayoutSection() {
    auto *body = new QWidget;
    auto *outer = new QVBoxLayout(body);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    auto *hint = new QLabel(
        tr("拖拽设备到网格位置 · 鼠标滑向屏幕边缘自动切换 · 快捷键 Ctrl+1/2/3"));
    DPalette hintPl = hint->palette();
    hintPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    hint->setPalette(hintPl);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto *gridHost = new QWidget;
    m_layoutGrid = new QGridLayout(gridHost);
    m_layoutGrid->setContentsMargins(0, 0, 0, 0);
    m_layoutGrid->setSpacing(8);
    outer->addWidget(gridHost);

    auto *legend = new QHBoxLayout;
    legend->setSpacing(12);
    auto *legendCurrent = new QLabel(tr("● 当前设备"));
    legendCurrent->setStyleSheet(QString("color:%1;").arg(Theme::kSuccess.name()));
    auto *legendEdge = new QLabel(tr("→ 鼠标边缘切换方向"));
    auto *legendKeys = new QLabel(tr("⌨ Ctrl+1/2/3 快速切换"));
    for (QLabel *l : { legendEdge, legendKeys }) {
        DPalette pl = l->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        l->setPalette(pl);
    }
    legend->addWidget(legendCurrent);
    legend->addWidget(legendEdge);
    legend->addWidget(legendKeys);
    legend->addStretch(1);
    outer->addLayout(legend);

    return buildCollapsibleSection(&m_layoutSection, tr("设备布局"), tr("拖拽排列"), body);
}

QWidget *MainWindow::switchCell(int gridX, int gridY) {
    QString deviceId;
    for (const LayoutSlot &s : m_layout)
        if (s.gridX == gridX && s.gridY == gridY)
            deviceId = s.deviceId;

    QString deviceName;
    if (!deviceId.isEmpty())
        for (const QVariant &v : m_core.devices()) {
            const QVariantMap d = v.toMap();
            if (d.value("id").toString() == deviceId) {
                deviceName = d.value("name").toString();
                break;
            }
        }

    auto *cell = new DFrame;
    cell->setMinimumSize(96, 96);
    cell->setProperty("gridX", gridX);
    cell->setProperty("gridY", gridY);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(6, 6, 6, 6);
    cellLayout->setSpacing(4);
    cellLayout->setAlignment(Qt::AlignCenter);

    if (!deviceId.isEmpty() && !deviceName.isEmpty()) {
        const bool isCurrent = deviceId == m_currentDeviceId;
        auto *tile = new Chip(Chip::Tile, deviceName.split(' ').value(0).left(2).toUpper());
        tile->setFixedSize(56, 56);
        if (isCurrent) {
            tile->setFgColor(Theme::applicationPalette().color(DPalette::HighlightedText));
            tile->setBgColor(Theme::applicationPalette().color(DPalette::Highlight));
        }
        auto *nameLabel = new QLabel(deviceName.split(' ').value(0));
        nameLabel->setAlignment(Qt::AlignCenter);
        cellLayout->addWidget(tile, 0, Qt::AlignHCenter);
        cellLayout->addWidget(nameLabel);
        if (isCurrent) {
            auto *current = new QLabel(tr("当前"));
            current->setAlignment(Qt::AlignCenter);
            current->setStyleSheet(QString("color:%1;").arg(Theme::kSuccess.name()));
            cellLayout->addWidget(current);
        }
        cell->setProperty("dragId", deviceId);
        cell->setCursor(Qt::ClosedHandCursor);
        cell->installEventFilter(new CellDragHandler(cell));
    } else {
        auto *plus = new QLabel("+");
        plus->setAlignment(Qt::AlignCenter);
        DPalette pl = plus->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        plus->setPalette(pl);
        cellLayout->addWidget(plus);
        cell->setAcceptDrops(true);
        cell->installEventFilter(new CellDropHandler(this, cell));
    }
    return cell;
}

void MainWindow::rebuildLayoutGrid() {
    if (!m_layoutGrid)
        return;
    while (QLayoutItem *item = m_layoutGrid->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    // default arrangement mirrors the design: first devices into the top-center,
    // middle-left, center slots
    if (m_layout.isEmpty()) {
        const QVariantList devices = m_core.devices();
        const QList<QPair<int, int>> positions = { { 1, 0 }, { 0, 1 }, { 1, 1 } };
        for (int i = 0; i < qMin(devices.size(), positions.size()); ++i)
            m_layout.append({ positions[i].first, positions[i].second,
                              devices.at(i).toMap().value("id").toString() });
    }
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            m_layoutGrid->addWidget(switchCell(x, y), y, x);
}


void MainWindow::switchToDeviceBySlot(int slot) {
    if (slot < 0 || slot >= m_layout.size())
        return;
    const LayoutSlot &s = m_layout.at(slot);
    if (s.deviceId.isEmpty())
        return;
    m_currentDeviceId = s.deviceId;
    rebuildLayoutGrid();
    for (const QVariant &v : m_core.devices()) {
        const QVariantMap d = v.toMap();
        if (d.value("id").toString() == s.deviceId) {
            showToast(tr("切换到 %1").arg(d.value("name").toString()));
            break;
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        bool ok = false;
        const int num = event->text().toInt(&ok);
        if (ok && num >= 1 && num <= 9) {
            switchToDeviceBySlot(num - 1);
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

QWidget *MainWindow::buildSettingsSection() {
    auto *header = new DHeaderLine;
    header->setTitle(tr("通用设置"));
    m_mainLayout->addWidget(header);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    const QVariantMap settings = m_core.settings();
    const std::pair<QString, QString> toggles[] = {
        { "keyMapping", tr("键位映射") },
        { "clipboardSync", tr("剪贴板同步") },
        { "dtls", tr("DTLS 加密") },
    };
    for (const auto &toggle : toggles) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(toggle.second);
        auto *switchButton = new DSwitchButton;
        switchButton->setChecked(settings.value(toggle.first, true).toBool());
        const QString key = toggle.first;
        connect(switchButton, &DSwitchButton::toggled, &m_core,
                [this, key, labelText = toggle.second](bool checked) {
                    m_core.setSetting(key, checked);
                    showToast(tr("%1已%2").arg(labelText, checked ? tr("开启") : tr("关闭")));
                });
        row->addWidget(label, 1);
        row->addWidget(switchButton);
        layout->addLayout(row);
    }

    auto *captureRow = new QHBoxLayout;
    auto *captureColumn = new QVBoxLayout;
    captureColumn->setSpacing(2);
    auto *captureLabel = new QLabel(tr("控制其他设备（Host 捕获）"));
    auto *captureHint = new QLabel(tr("开启后本机键鼠将转发给已连接设备"));
    DPalette hintPl = captureHint->palette();
    hintPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    captureHint->setPalette(hintPl);
    captureColumn->addWidget(captureLabel);
    captureColumn->addWidget(captureHint);
    auto *captureSwitch = new DSwitchButton;
    captureSwitch->setChecked(m_core.capturing());
    connect(captureSwitch, &DSwitchButton::toggled, this, [this](bool checked) {
        if (!m_core.setCapturing(checked)) {
            QSignalBlocker blocker(sender());
            if (auto *button = qobject_cast<DSwitchButton *>(sender()))
                button->setChecked(!checked);
            showTransientError(tr("无法开启捕获：请确认已加入 input 组或已安装 udev 规则"));
        }
    });
    captureRow->addLayout(captureColumn, 1);
    captureRow->addWidget(captureSwitch);
    layout->addLayout(captureRow);

    auto *group = new DBackgroundGroup(layout);
    group->setItemMargins(QMargins(10, 8, 10, 8));

    auto *wrap = new QWidget;
    auto *outer = new QVBoxLayout(wrap);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(group);
    return wrap;
}

void MainWindow::rebuildDeviceList() {
    const bool hasDevices = !m_core.devices().isEmpty();
    if (m_layoutSection.root)
        m_layoutSection.root->setVisible(hasDevices);
    while (QLayoutItem *item = m_deviceListLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QVariantList devices = m_core.devices();
    if (devices.isEmpty()) {
        auto *empty = new QLabel(tr("还没有已连接的设备\n点击「添加设备」发现附近的设备"));
        DPalette pl = empty->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        empty->setPalette(pl);
        empty->setAlignment(Qt::AlignCenter);
        empty->setMinimumHeight(72);
        m_deviceListLayout->addWidget(empty);
    }
    for (const QVariant &device : devices)
        m_deviceListLayout->addWidget(buildDeviceCard(device.toMap()));
    refreshConnectionStatus();
    rebuildLayoutGrid();
}

void MainWindow::refreshConnectionStatus() {
    QString connectedName;
    for (const QVariant &v : m_core.devices()) {
        const QVariantMap d = v.toMap();
        if (d.value("connected").toBool()) {
            connectedName = d.value("name").toString();
            break;
        }
    }
    const bool connected = !connectedName.isEmpty();
    m_statusDot->setStyleSheet(
        QString("background:%1;border-radius:4px;")
            .arg(connected ? Theme::kSuccess.name()
                           : Theme::applicationPalette().color(DPalette::TextTips).name()));
    m_statusLabel->setText(connected ? connectedName : tr("未连接任何设备"));
}

void MainWindow::showTransientError(const QString &message) {
    showToast(message);
}

void MainWindow::showToast(const QString &message) {
    DMessageManager::instance()->sendMessage(this, QIcon(), message);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    hide();
    event->ignore();
}

QString MainWindow::platformAbbrev(const QString &platform) {
    const QString p = platform.toLower();
    if (p.startsWith("win"))
        return "PC";
    if (p.startsWith("mac") || p.startsWith("ios"))
        return "MAC";
    if (p.startsWith("linux") || p.startsWith("android"))
        return "LX";
    return platform.left(2).toUpper();
}

QString MainWindow::keyName(const QString &hexCode) {
    static const QHash<QString, QString> names = {
        { "0x0004", "A" }, { "0x0006", "C" }, { "0x0013", "P" }, { "0x0014", "Q" },
        { "0x0019", "V" }, { "0x002B", "Tab" }, { "0x002C", "Space" },
        { "0x0028", "Enter" }, { "0x0029", "Esc" }, { "0x0039", "Caps" },
    };
    return names.value(hexCode.toLower(), hexCode);
}

QString MainWindow::contextLabel(const QString &context) {
    if (context == QLatin1String("global"))
        return tr("全局");
    if (context == QLatin1String("game"))
        return tr("游戏模式");
    if (context.startsWith("app:"))
        return context.mid(4);
    return context;
}
