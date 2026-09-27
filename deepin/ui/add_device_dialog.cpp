#include "add_device_dialog.h"

#include "theme.h"

#include <DPalette>
#include <DSpinner>
#include <DSuggestButton>
#include <QPushButton>
#include <QShowEvent>
#include <QRandomGenerator>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace DTK_WIDGET_NAMESPACE;
using namespace DTK_GUI_NAMESPACE;

namespace {

// Style an existing step-indicator dot (index 1-3) for the active step.
// Colors come from the native palette (Highlight = active, success = done).
void setStep(QLabel *dot, int index, int activeStep) {
    const bool done = activeStep > index;
    const bool active = activeStep == index;
    dot->setText(done ? "✓" : QString::number(index));
    DPalette pl = dot->palette();
    const QColor bg = done ? Theme::kSuccess
                    : active ? pl.color(DPalette::Highlight)
                             : pl.color(QPalette::AlternateBase);
    const QColor fg = done || active ? pl.color(QPalette::BrightText)
                                     : Theme::applicationPalette().color(DPalette::TextTips);
    pl.setColor(DPalette::ItemBackground, bg);
    pl.setColor(DPalette::WindowText, fg);
    dot->setPalette(pl);
    dot->setAutoFillBackground(true);
    dot->setAlignment(Qt::AlignCenter);
}

QLabel *makeStepDot() {
    auto *dot = new QLabel;
    dot->setFixedSize(18, 18);
    dot->setAlignment(Qt::AlignCenter);
    return dot;
}

} // namespace

AddDeviceDialog::AddDeviceDialog(CoreClient &core, QWidget *parent)
    : DDialog(parent)
    , m_core(core) {
    setWindowTitle(tr("添加设备"));
    setFixedSize(360, 420);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildDiscoverPage());
    m_stack->addWidget(buildSuccessPage());
    addContent(m_stack);

    connect(&m_core, &CoreClient::devicesChanged, this, &AddDeviceDialog::rebuildCandidates);
    connect(&m_core, &CoreClient::scanStateChanged, this, [this](bool scanning) {
        if (m_spinner)
            scanning ? m_spinner->start() : m_spinner->stop();
    });
}

QWidget *AddDeviceDialog::buildDiscoverPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 6, 18, 12);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignHCenter);

    auto *steps = new QHBoxLayout;
    steps->setSpacing(8);
    steps->setAlignment(Qt::AlignHCenter);
    m_step1 = makeStepDot();
    m_step2 = makeStepDot();
    m_step3 = makeStepDot();
    steps->addWidget(m_step1);
    steps->addWidget(m_step2);
    steps->addWidget(m_step3);
    layout->addLayout(steps);

    m_titleLabel = new QLabel(tr("正在扫描附近设备…"));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont tf = m_titleLabel->font();
    tf.setBold(true);
    m_titleLabel->setFont(tf);
    layout->addWidget(m_titleLabel);

    m_spinner = new DSpinner;
    m_spinner->setFixedSize(64, 64);
    layout->addWidget(m_spinner, 0, Qt::AlignHCenter);

    // scan progress (design: .progress-bar under the scanner)
    m_progress = new DProgressBar;
    m_progress->setFixedHeight(4);
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    layout->addWidget(m_progress);

    m_listHost = new QWidget;
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    layout->addWidget(m_listHost, 1);

    auto *connectButton = new DSuggestButton(tr("连接"));
    connect(connectButton, &QPushButton::clicked, this, [this, connectButton] {
        if (m_selectedId.isEmpty())
            return;
        // Step 2: connecting — button disabled until the device shows up connected.
        setStep(m_step1, 1, 2);
        setStep(m_step2, 2, 2);
        setStep(m_step3, 3, 2);
        m_titleLabel->setText(tr("正在连接…"));
        connectButton->setEnabled(false);
        m_core.connectDevice(m_selectedId);
    });
    m_connectButtonId = addButton(tr("连接"));
    getButton(m_connectButtonId)->setEnabled(false);
    auto *cancelViaDialog = getButton(addButton(tr("取消")));
    connect(cancelViaDialog, &QPushButton::clicked, this, &DDialog::reject);

    return page;
}

QWidget *AddDeviceDialog::buildSuccessPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignHCenter);

    auto *icon = new QLabel("✓");
    icon->setFixedSize(48, 48);
    icon->setAlignment(Qt::AlignCenter);
    {
        DPalette pl = icon->palette();
        pl.setColor(DPalette::ItemBackground, Theme::kSuccess);
        pl.setColor(DPalette::WindowText, pl.color(QPalette::BrightText));
        icon->setPalette(pl);
        icon->setAutoFillBackground(true);
    }
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    auto *title = new QLabel(tr("设备已连接"));
    title->setAlignment(Qt::AlignCenter);
    {
        QFont tf = title->font();
        tf.setBold(true);
        title->setFont(tf);
    }
    layout->addWidget(title);

    m_successDetail = new QLabel(tr("设备已配对并就绪"));
    m_successDetail->setAlignment(Qt::AlignCenter);
    {
        DPalette pl = m_successDetail->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        m_successDetail->setPalette(pl);
    }
    layout->addWidget(m_successDetail);

    auto *doneButton = new DSuggestButton(tr("完成"));
    connect(doneButton, &QPushButton::clicked, this, &DDialog::accept);
    layout->addWidget(doneButton, 0, Qt::AlignHCenter);
    return page;
}

void AddDeviceDialog::rebuildCandidates() {
    if (m_stack->currentIndex() != 0)
        return;
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    QVariantList candidates;
    QString connectedName;
    for (const QVariant &value : m_core.devices()) {
        const QVariantMap device = value.toMap();
        if (device.value("id").toString() == m_selectedId && device.value("connected").toBool()) {
            // The selected device finished pairing — jump to the success page.
            connectedName = device.value("name").toString();
            break;
        }
        if (!device.value("connected").toBool())
            candidates.append(value);
    }
    if (!connectedName.isEmpty()) {
        setStep(m_step1, 1, 3);
        setStep(m_step2, 2, 3);
        setStep(m_step3, 3, 3);
        m_successDetail->setText(tr("%1 已配对并就绪").arg(connectedName));
        // detail rows (design: Device/Protocol/Latency/Encryption)
        while (QLayoutItem *item = m_detailLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        QString transport, latency, platform;
        for (const QVariant &v : m_core.devices()) {
            const QVariantMap d = v.toMap();
            if (d.value("id").toString() == m_selectedId) {
                transport = d.value("transport").toString();
                latency = d.value("latency").toString();
                platform = d.value("platform").toString();
                break;
            }
        }
        const auto addRow = [this](const QString &k, const QString &v) {
            auto *rowW = new QWidget;
            auto *rowLayout = new QHBoxLayout(rowW);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            auto *key = new QLabel(k);
            DPalette keyPl = key->palette();
            keyPl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
            key->setPalette(keyPl);
            auto *value = new QLabel(v);
            value->setFont(QFont(value->font().family(), -1, QFont::Bold));
            rowLayout->addWidget(key, 1);
            rowLayout->addWidget(value);
            m_detailLayout->addWidget(rowW);
        };
        addRow(tr("设备"), connectedName);
        addRow(tr("协议"), transport.isEmpty() ? QStringLiteral("NearLink") : transport);
        addRow(tr("延迟"), latency);
        addRow(tr("平台"), platform);
        m_stack->setCurrentIndex(1);
        return;
    }

    m_spinner->setVisible(candidates.isEmpty());
    m_titleLabel->setText(candidates.isEmpty() ? tr("正在扫描附近设备…") : tr("发现附近设备"));
    if (!candidates.isEmpty() && m_progress->value() < 100) {
        m_scanTimer.stop();
        m_progress->setValue(100);
        QTimer::singleShot(250, m_progress, &QProgressBar::hide);
    }

    for (const QVariant &value : candidates) {
        const QVariantMap device = value.toMap();
        auto *card = new QPushButton(device.value("name").toString());
        card->setCheckable(true);
        card->setFixedHeight(50);
        connect(card, &QPushButton::clicked, this, [this, card, device] {
            m_selectedId = device.value("id").toString();
            const QList<QPushButton *> cards = m_listHost->findChildren<QPushButton *>();
            for (QPushButton *other : cards)
                other->setChecked(other == card);
            getButton(m_connectButtonId)->setEnabled(true);
        });
        m_listLayout->addWidget(card);
    }
    m_listLayout->addStretch(1);
}

void AddDeviceDialog::showEvent(QShowEvent *event) {
    DDialog::showEvent(event);
    m_progress->show();
    m_progress->setValue(0);
    connect(&m_scanTimer, &QTimer::timeout, this, [this] {
        const int next = qMin(100, m_progress->value() + 8 + (QRandomGenerator::global()->bounded(12)));
        m_progress->setValue(next);
        if (next >= 100)
            m_scanTimer.stop();
    });
    m_scanTimer.start(220);
    m_selectedId.clear();
    m_stack->setCurrentIndex(0);
    setStep(m_step1, 1, 1);
    setStep(m_step2, 2, 1);
    setStep(m_step3, 3, 1);
    m_titleLabel->setText(tr("正在扫描附近设备…"));
    m_spinner->start();
    getButton(m_connectButtonId)->setEnabled(false);
    rebuildCandidates();
    m_core.scan();
}
