#include "LevelMeter.h"
#include "Theme.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace vc {

LevelMeter::LevelMeter(Orientation o, QWidget* parent)
    : QWidget(parent), m_ori(o)
{
    setMinimumSize(o == Vertical ? QSize(22, 120) : QSize(120, 22));
    m_segmentDecay.fill(0.0f);
}

void LevelMeter::setValue(float peakLin, float rmsLin)
{
    m_peak = std::clamp(peakLin, 0.0f, 1.2f);
    m_rms = std::clamp(rmsLin, 0.0f, 1.0f);
    if (m_peak >= m_peakHold) {
        m_peakHold = m_peak;
        m_peakHoldAge = 0;
    } else if (++m_peakHoldAge > 30) {           // ~1 s hold, then fall
        m_peakHold *= 0.93f;
    }
    if (m_peak > 0.999f) { m_clip = true; m_clipAge = 0; }
    else if (++m_clipAge > 60) m_clip = false;
    update();
}

void LevelMeter::clearPeaks()
{
    m_peak = m_rms = m_peakHold = 0.0f;
    m_clip = false;
    update();
}

QSize LevelMeter::minimumSizeHint() const
{
    return m_ori == Vertical ? QSize(26, 160) : QSize(160, 26);
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(2, 2, -2, -2);
    // body
    p.setPen(QPen(QColor(Theme::BORDER), 1));
    p.setBrush(QColor("#0a0c10"));
    p.drawRoundedRect(r, 4, 4);

    const float db = 20.0f * std::log10(std::max(m_peak, 1e-6f));
    // map -60..0 dBFS to 0..1
    const float norm = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    const float rmsNorm = std::clamp(
        (20.0f * std::log10(std::max(m_rms, 1e-6f)) + 60.0f) / 60.0f, 0.0f, 1.0f);

    // gradient: green -> amber -> red
    auto levelColor = [](float t) {
        if (t < 0.6f) return QColor::fromHslF(0.36f, 0.72f, 0.42f + 0.1f * t);
        if (t < 0.85f) return QColor::fromHslF(0.10f, 0.85f, 0.5f);
        return QColor::fromHslF(0.0f, 0.78f, 0.5f);
    };

    const int segs = 24;
    float w = m_ori == Vertical ? float(r.height()) : float(r.width());
    float segW = (w - float(segs - 1) * 2.0f) / float(segs);

    for (int i = 0; i < segs; ++i) {
        const float t0 = float(i) / float(segs);        // segment bottom
        const bool lit = t0 < norm;
        const bool rmsLit = t0 < rmsNorm;
        QRectF seg = m_ori == Vertical
            ? QRectF(float(r.x() + 2), float(r.y() + r.height() - 2 - segW - i * (segW + 2)),
                     float(r.width() - 4), segW)
            : QRectF(float(r.x() + 2 + i * (segW + 2)), float(r.y() + 2),
                     segW, float(r.height() - 4));
        QColor c = levelColor(t0);
        if (!lit) c = QColor(30, 34, 44);
        else if (!rmsLit) c = c.darker(150);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(seg, 2, 2);
    }

    // peak-hold marker
    const float holdNorm = std::clamp(
        (20.0f * std::log10(std::max(m_peakHold, 1e-6f)) + 60.0f) / 60.0f, 0.0f, 1.0f);
    p.setPen(QPen(QColor(Theme::TEXT), 2));
    if (m_ori == Vertical) {
        const float y = r.y() + r.height() - 2 - holdNorm * (r.height() - 4);
        p.drawLine(QPointF(r.x() + 2, y), QPointF(r.x() + r.width() - 2, y));
    } else {
        const float x = r.x() + 2 + holdNorm * (r.width() - 4);
        p.drawLine(QPointF(x, r.y() + 2), QPointF(x, r.y() + r.height() - 2));
    }

    // 0 dBFS tick
    p.setPen(QPen(QColor(Theme::MUTED), 1));
    if (m_ori == Vertical) {
        const float yTop = float(r.y() + r.height() - 2);
        p.drawText(QRectF(0, yTop - 12, r.x() - 2, 12), Qt::AlignRight, "0");
        p.drawText(QRectF(0, yTop - (r.height() - 4) - 6, r.x() - 2, 12), Qt::AlignRight, "-60");
    }

    // clip LED
    if (m_clip) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Theme::RED));
        const float d = 6.0f;
        QRectF led = m_ori == Vertical
            ? QRectF(r.x() + 2, r.y() + 2, d, d)
            : QRectF(r.x() + r.width() - d - 2, r.y() + 2, d, d);
        p.drawEllipse(led);
    }
}

} // namespace vc
