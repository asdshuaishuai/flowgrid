// Rounded chip widget (tiles / pills / key-caps / badges) used across the
// deepin UI. Paints a rounded rect with DPalette semantic colors pulled LIVE
// from Theme::applicationPalette(), so it follows dde light/dark themes with
// zero QSS. Replaces square DLabels that lacked the dde rounded look.
#ifndef FLOWGRID_CHIP_H
#define FLOWGRID_CHIP_H

#include <QWidget>

#include "theme.h"

#include <DPalette>
#include <DLabel>

#include <QFontMetrics>
#include <QPainter>

using DTK_WIDGET_NAMESPACE::DLabel;

class Chip : public QWidget {
public:
    enum Kind { Tile, Pill, Key, Badge };

    Chip(Kind kind, const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_kind(kind), m_text(text) {
        setAutoFillBackground(false);
        switch (m_kind) {
        case Tile:
            setFixedSize(42, 42);
            break;
        case Pill:
            setFixedSize(58, 26);
            break;
        case Key:
            setMinimumSize(34, 24);
            break;
        case Badge:
            setFixedHeight(20);
            break;
        }
        setFgColor(fgFor(m_text));
    }

    void setText(const QString &text) {
        m_text = text;
        m_fgColor = fgFor(text);
        updateGeometry();
        update();
    }

    // Explicit foreground override (e.g. semantic latency colors).
    void setFgColor(const QColor &color) {
        m_fgColor = color;
        update();
    }

    // Explicit background override (e.g. current-device highlight).
    void setBgColor(const QColor &color) {
        m_bgColor = color;
        update();
    }

    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        int w = fm.horizontalAdvance(m_text) + (m_kind == Key ? 14 : 16);
        const int h = (m_kind == Tile) ? 42 : (m_kind == Pill) ? 26 : (m_kind == Key) ? 24 : 20;
        if (m_kind == Tile)
            w = qMax(w, 42);
        return QSize(qMax(w, minimumWidth()), h);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const DTK_GUI_NAMESPACE::DPalette pal = Theme::applicationPalette();
        const QColor bg = m_bgColor.isValid()
            ? m_bgColor : pal.color(DTK_GUI_NAMESPACE::DPalette::ItemBackground);
        int radius = 8;
        QColor fg = m_fgColor;

        switch (m_kind) {
        case Tile:
            radius = 10;
            if (!m_fgColor.isValid())
                fg = pal.color(DTK_GUI_NAMESPACE::DPalette::Highlight);
            break;
        case Pill:
            radius = height() / 2;
            if (!m_fgColor.isValid())
                fg = pal.color(DTK_GUI_NAMESPACE::DPalette::Highlight);
            break;
        case Key:
            radius = 6;
            fg = pal.color(DTK_GUI_NAMESPACE::DPalette::WindowText);
            break;
        case Badge:
            radius = height() / 2;
            fg = pal.color(DTK_GUI_NAMESPACE::DPalette::Highlight);
            break;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(rect(), radius, radius);

        painter.setPen(fg);
        QFont f = font();
        f.setBold(m_kind == Tile || m_kind == Pill || m_kind == Badge);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QColor fgFor(const QString &text) const {
        Q_UNUSED(text);
        return QColor(); // invalid = resolve from palette at paint time
    }

    Kind m_kind;
    QString m_text;
    QColor m_fgColor;
    QColor m_bgColor; // invalid = ItemBackground from palette
};
#endif // FLOWGRID_CHIP_H
