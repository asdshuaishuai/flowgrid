#include "latency_panel.h"

#include "core_client.h"
#include "theme.h"

#include <DPalette>
#include <DFrame>
#include <DLabel>
#include <dfontmanager.h>

#include <QDateTime>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QVBoxLayout>

using namespace DTK_WIDGET_NAMESPACE;
using namespace DTK_GUI_NAMESPACE;

namespace {

constexpr int kBarCount = 24;
constexpr double kScaleMax = 5.0;

class BarChart : public QWidget {
public:
    explicit BarChart(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void setSamples(const QList<double> &samples) {
        m_samples = samples;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // dashed grid lines at 5/3/1 ms with labels (design: chart-grid)
        QPen gridPen(Theme::applicationPalette().color(DPalette::TextTips));
        gridPen.setStyle(Qt::DashLine);
        const QStringList labels = { "5ms", "3ms", "1ms" };
        const double lineValues[] = { 5.0, 3.0, 1.0 };
        for (int i = 0; i < 3; ++i) {
            const qreal y = height() * (1.0 - lineValues[i] / kScaleMax);
            painter.setPen(gridPen);
            painter.drawLine(0, int(y), width(), int(y));
            painter.setPen(Theme::applicationPalette().color(DPalette::TextTips));
            painter.drawText(QRect(0, int(y) - 12, 30, 12), Qt::AlignLeft, labels.at(i));
        }

        if (m_samples.isEmpty())
            return;
        const int gap = 3;
        const double barWidth =
            qMax(2.0, (width() - gap * (m_samples.size() - 1)) / double(m_samples.size()));
        for (int i = 0; i < m_samples.size(); ++i) {
            const double value = m_samples.at(i);
            const double h = qMax(6.0, qMin(1.0, value / kScaleMax) * (height() - 8));
            const qreal x = i * (barWidth + gap);
            const qreal y = height() - h;
            const qreal r = qMin(4.0, barWidth / 2);
            QPainterPath path;
            path.moveTo(x, height());
            path.lineTo(x, y + r);
            path.quadTo(x, y, x + r, y);
            path.lineTo(x + barWidth - r, y);
            path.quadTo(x + barWidth, y, x + barWidth, y + r);
            path.lineTo(x + barWidth, height());
            path.closeSubpath();
            const bool recent = i >= m_samples.size() - 3;
            painter.fillPath(path, recent ? palette().color(QPalette::Highlight)
                                          : Theme::latencyColor(value));
        }
    }

private:
    QList<double> m_samples;
};

QWidget *statCard(QLabel **valueLabel, const QString &label) {
    auto *card = new DFrame;
    card->setFixedHeight(58);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(4);
    *valueLabel = new DLabel("--");
    (*valueLabel)->setAlignment(Qt::AlignCenter);
    QFont f = (*valueLabel)->font();
    f.setBold(true);
    (*valueLabel)->setFont(f);
    auto *caption = new DLabel(label);
    caption->setAlignment(Qt::AlignCenter);
    DPalette pl = caption->palette();
    pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    caption->setPalette(pl);
    layout->addWidget(*valueLabel);
    layout->addWidget(caption);
    return card;
}

} // namespace

LatencyPanel::LatencyPanel(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("延迟监控"));
    resize(420, 460);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto *heroRow = new QHBoxLayout;
    m_hero = new DLabel("--");
    {
        // design hero 42px ≈ DTK T1; DFontManager follows system font scaling
        m_fontManager = new DTK_GUI_NAMESPACE::DFontManager(this);
        m_hero->setFont(m_fontManager->get(DTK_GUI_NAMESPACE::DFontManager::T1,
                                           m_hero->font()));
        QPalette pl = m_hero->palette();
        pl.setColor(QPalette::WindowText, Theme::kSuccess);
        m_hero->setPalette(pl);
    }
    heroRow->addWidget(m_hero);
    heroRow->addStretch(1);
    auto *sideColumn = new QVBoxLayout;
    sideColumn->setSpacing(2);
    m_minLabel = new QLabel(tr("最小 --ms"));
    m_avgLabel = new QLabel(tr("平均 --ms"));
    m_p95Label = new QLabel(tr("95th --ms"));
    for (QLabel *label : { m_minLabel, m_avgLabel, m_p95Label }) {
        label->setAlignment(Qt::AlignRight);
        DPalette pl = label->palette();
        pl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
        label->setPalette(pl);
        sideColumn->addWidget(label);
    }
    heroRow->addLayout(sideColumn);
    layout->addLayout(heroRow);

    m_chart = new BarChart;
    m_chart->setFixedHeight(90);
    layout->addWidget(m_chart);

    auto *stats = new QHBoxLayout;
    stats->setSpacing(8);
    stats->addWidget(statCard(&m_statCurrent, tr("当前")));
    stats->addWidget(statCard(&m_statMin, tr("最小")));
    stats->addWidget(statCard(&m_statMax, tr("最大")));
    stats->addWidget(statCard(&m_statJitter, tr("抖动")));
    layout->addLayout(stats);

    // spike alert banner (>5ms, design: yellow warning strip)
    m_alert = new QLabel(tr("⚠ 检测到延迟尖峰（> 5ms）"));
    m_alert->setVisible(false);
    m_alert->setAlignment(Qt::AlignCenter);
    {
        QPalette pl = m_alert->palette();
        pl.setColor(QPalette::Window, QColor(0xFF, 0xF3, 0xC7));
        pl.setColor(QPalette::WindowText, QColor(0x92, 0x40, 0x0E));
        m_alert->setPalette(pl);
        m_alert->setAutoFillBackground(true);
    }
    layout->addWidget(m_alert);

    auto *historyTitle = new QLabel(tr("近期事件"));
    historyTitle->setFont(QFont(historyTitle->font().family(), -1, QFont::Bold));
    layout->addWidget(historyTitle);
    auto *historyHost = new QWidget;
    m_historyLayout = new QVBoxLayout(historyHost);
    m_historyLayout->setContentsMargins(0, 0, 0, 0);
    m_historyLayout->setSpacing(2);
    layout->addWidget(historyHost);
    layout->addStretch(1);
}

void LatencyPanel::setLatencyText(const QString &text) {
    bool ok = false;
    const double ms = text.toDouble(&ok);
    if (!ok)
        return;
    const double delta = m_hasSample ? qAbs(ms - m_prevMs) : 0.0;
    m_prevMs = ms;
    if (!m_hasSample || ms < m_minMs)
        m_minMs = ms;
    if (!m_hasSample || ms > m_maxMs)
        m_maxMs = ms;
    m_hasSample = true;
    m_history.append(ms);
    while (m_history.size() > kBarCount)
        m_history.removeFirst();

    m_hero->setText(text);
    QPalette heroPl = m_hero->palette();
    heroPl.setColor(QPalette::WindowText, Theme::latencyColor(ms));
    m_hero->setPalette(heroPl);

    m_statCurrent->setText(text);
    QPalette curPl = m_statCurrent->palette();
    curPl.setColor(QPalette::WindowText, Theme::latencyColor(ms));
    m_statCurrent->setPalette(curPl);
    m_statMin->setText(QString("%1ms").arg(m_minMs, 0, 'f', 1));
    m_statMax->setText(QString("%1ms").arg(m_maxMs, 0, 'f', 1));
    m_statJitter->setText(QString("%1ms").arg(delta, 0, 'f', 2));

    double sum = 0;
    for (double v : m_history)
        sum += v;
    m_avgLabel->setText(tr("平均 %1ms").arg(sum / m_history.size(), 0, 'f', 1));
    m_minLabel->setText(tr("最小 %1ms").arg(m_minMs, 0, 'f', 1));

    QList<double> sorted = m_history;
    std::sort(sorted.begin(), sorted.end());
    const int p95Index = qMax(0, int(sorted.size() * 0.95) - 1);
    const double p95 = sorted.value(p95Index, ms);
    m_p95Label->setText(tr("95th %1ms").arg(p95, 0, 'f', 1));

    m_alert->setVisible(ms > 5.0);

    if (auto *chart = static_cast<BarChart *>(m_chart))
        chart->setSamples(m_history);

    pushHistory(ms);
}

void LatencyPanel::pushHistory(double ms) {
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 4, 0, 4);
    auto *timeLabel = new QLabel(QDateTime::currentDateTime().toString("HH:mm:ss"));
    DPalette timePl = timeLabel->palette();
    timePl.setColor(DPalette::WindowText, Theme::applicationPalette().color(DPalette::TextTips));
    timeLabel->setPalette(timePl);
    auto *valueLabel = new QLabel(QString("%1ms").arg(ms, 0, 'f', 1));
    valueLabel->setFont(QFont(valueLabel->font().family(), -1, QFont::Bold));
    QPalette valuePl = valueLabel->palette();
    valuePl.setColor(QPalette::WindowText, Theme::latencyColor(ms));
    valueLabel->setPalette(valuePl);
    rowLayout->addWidget(timeLabel);
    rowLayout->addStretch(1);
    rowLayout->addWidget(valueLabel);

    // newest first, keep 5 rows
    m_historyLayout->insertWidget(0, row);
    while (m_historyLayout->count() > 5) {
        QLayoutItem *item = m_historyLayout->takeAt(m_historyLayout->count() - 1);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void LatencyPanel::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    setLatencyText(CoreClient::instance().latency());
}
