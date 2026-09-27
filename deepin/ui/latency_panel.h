// Latency monitor, follows design/mac/latency.html: 42px hero number with
// good/warn/bad color, 24-bar chart with 5/3/1ms grid lines and highlighted
// recent bars, current/min/max/jitter stat cards, spike alert banner and a
// recent-events list. Visuals native DTK + palette.
#ifndef FLOWGRID_LATENCY_PANEL_H
#define FLOWGRID_LATENCY_PANEL_H

#include <QDialog>
#include <QLabel>
#include <QList>

class QVBoxLayout;

namespace Dtk { namespace Gui { class DFontManager; } }

class LatencyPanel : public QDialog {
    Q_OBJECT

public:
    explicit LatencyPanel(QWidget *parent = nullptr);
    void setLatencyText(const QString &text);

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *buildStatCard(QLabel **valueLabel, const QString &label);
    void pushHistory(double ms);

    QLabel *m_hero = nullptr;
    QLabel *m_minLabel = nullptr;
    QLabel *m_avgLabel = nullptr;
    QLabel *m_p95Label = nullptr;
    QLabel *m_statCurrent = nullptr;
    QLabel *m_statMin = nullptr;
    QLabel *m_statMax = nullptr;
    QLabel *m_statJitter = nullptr;
    QLabel *m_alert = nullptr;
    QWidget *m_chart = nullptr;
    QVBoxLayout *m_historyLayout = nullptr;
    QList<double> m_history; // chart window (last 24)
    double m_minMs = 0;
    double m_maxMs = 0;
    double m_prevMs = 0;
    bool m_hasSample = false;
    Dtk::Gui::DFontManager *m_fontManager = nullptr;
};

#endif // FLOWGRID_LATENCY_PANEL_H
