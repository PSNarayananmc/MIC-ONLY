#include "WaveformView.h"
#include "Theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace vc {

WaveformView::WaveformView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    m_liveBuf.assign(LIVE_COLS, {0.0f, 0.0f});
}

void WaveformView::setLive(bool live)
{
    m_liveMode = live;
    if (live) {
        m_doc.clear();
        m_selStart = m_selEnd = -1;
        std::fill(m_liveBuf.begin(), m_liveBuf.end(), QPair<float, float>{0.0f, 0.0f});
    }
    update();
}

void WaveformView::pushBlock(const float* interleaved, int frames, int channels)
{
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) {
            const float v = interleaved[std::size_t(i) * channels + c];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    // simple AGC so quiet mics are visible (never expand above 4x)
    const float peak = std::max(std::fabs(lo), std::fabs(hi));
    if (peak > 0.0f && peak < 0.5f)
        m_liveScale = std::min(4.0f, std::max(m_liveScale, 0.9f / peak));
    else if (peak > 0.95f)
        m_liveScale = std::max(0.2f, m_liveScale * 0.98f);

    m_liveBuf.pop_front();
    m_liveBuf.push_back({lo * m_liveScale, hi * m_liveScale});
    update();
}

void WaveformView::setDocument(const QVector<float>& interleaved, int channels,
                               int sampleRate)
{
    m_doc = interleaved;
    m_docChannels = channels < 1 ? 1 : channels;
    m_docRate = sampleRate;
    m_zoom = 1.0;
    m_viewStart = 0;
    setLive(false);
    rebuildCache();
    update();
}

void WaveformView::clearDocument()
{
    m_doc.clear();
    m_selStart = m_selEnd = -1;
    m_playhead = -1;
    m_zoom = 1.0;
    m_viewStart = 0;
    update();
}

void WaveformView::setSelection(qint64 startFrame, qint64 endFrame)
{
    m_selStart = startFrame;
    m_selEnd = endFrame;
    update();
}

void WaveformView::setPlayheadFrame(qint64 frame)
{
    m_playhead = frame;
    update();
}

void WaveformView::setZoom(double factor)
{
    m_zoom = std::clamp(factor, 1.0, 2000.0);
    m_viewStart = std::clamp<qint64>(m_viewStart, 0,
        std::max<qint64>(0, qint64(m_doc.size()) / m_docChannels - visibleFrames()));
    update();
}

void WaveformView::setScrollFraction(double f)
{
    const qint64 total = qint64(m_doc.size()) / std::max(1, m_docChannels);
    const qint64 vis = visibleFrames();
    m_viewStart = std::clamp<qint64>(qint64(f * double(total - vis)), 0,
                                     std::max<qint64>(0, total - vis));
    update();
}

qint64 WaveformView::visibleFrames() const
{
    const qint64 total = qint64(m_doc.size()) / std::max(1, m_docChannels);
    return std::max<qint64>(64, qint64(double(total) / m_zoom));
}

void WaveformView::rebuildCache()
{
    // nothing cached yet — ranges computed per column in paint (fast enough
    // for minutes-long documents with min/max scanning)
}

float WaveformView::rangeAt(int, float&, float&) const { return 0.0f; }

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#0a0c10"));
    p.setPen(QPen(QColor(Theme::BORDER), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // centre line
    p.setPen(QPen(QColor(28, 32, 42), 1));
    p.drawLine(QPoint(0, height() / 2), QPoint(width(), height() / 2));

    const int w = width(), h = height();
    const float midY = h / 2.0f;

    if (m_liveMode && m_doc.isEmpty()) {
        const int cols = std::min<int>(m_liveBuf.size(), w);
        p.setPen(QPen(QColor(Theme::ACCENT), 1.2));
        for (int x = 0; x < cols; ++x) {
            const auto& range = m_liveBuf[m_liveBuf.size() - cols + x];
            const float lo = std::clamp(range.first, -1.0f, 1.0f);
            const float hi = std::clamp(range.second, -1.0f, 1.0f);
            p.drawLine(QPointF(x, midY - hi * midY * 0.92f),
                       QPointF(x, midY - lo * midY * 0.92f));
        }
        // subtle grid
        p.setPen(QPen(QColor(30, 34, 44), 1, Qt::DotLine));
        for (int q = 1; q < 4; ++q) {
            const int y = int(q * h / 4.0);
            p.drawLine(QPoint(0, y), QPoint(w, y));
        }
        return;
    }

    // document mode
    const int channels = std::max(1, m_docChannels);
    const qint64 total = qint64(m_doc.size()) / channels;
    if (total == 0) {
        p.setPen(QColor(Theme::MUTED));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("no audio loaded"));
        return;
    }
    const qint64 vis = visibleFrames();
    const double framesPerCol = double(vis) / double(std::max(1, w));

    // selection shading
    if (m_selStart >= 0 && m_selEnd > m_selStart) {
        const double s = (double(m_selStart - m_viewStart)) / framesPerCol;
        const double e = (double(m_selEnd - m_viewStart)) / framesPerCol;
        p.fillRect(QRectF(s, 0.0, e - s, h), QColor(57, 196, 143, 36));
    }

    p.setPen(QPen(QColor(Theme::ACCENT), 1.0));
    for (int x = 0; x < w; ++x) {
        const qint64 f0 = m_viewStart + qint64(double(x) * framesPerCol);
        const qint64 f1 = m_viewStart + qint64(double(x + 1) * framesPerCol);
        if (f0 >= total) break;
        float lo = 0.0f, hi = 0.0f;
        for (qint64 f = f0; f < std::min(f1, total); ++f)
            for (int c = 0; c < channels; ++c) {
                const float v = m_doc[std::size_t(f) * channels + c];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        p.drawLine(QPointF(float(x), midY - hi * midY * 0.92f),
                   QPointF(float(x), midY - lo * midY * 0.92f));
    }

    // playhead
    if (m_playhead >= 0) {
        const double x = double(m_playhead - m_viewStart) / framesPerCol;
        if (x >= 0 && x <= w) {
            p.setPen(QPen(QColor(Theme::ACCENT_2), 2));
            p.drawLine(QPointF(x, 0), QPointF(x, h));
        }
    }
}

void WaveformView::mousePressEvent(QMouseEvent* e)
{
    if (m_liveMode || m_doc.isEmpty())
        return;
    const qint64 total = qint64(m_doc.size()) / m_docChannels;
    const double framesPerCol = double(visibleFrames()) / double(std::max(1, width()));
    const qint64 f = m_viewStart + qint64(e->x() * framesPerCol);
    if (f >= 0 && f <= total) {
        m_dragging = true;
        m_dragAnchor = f;
        m_selStart = f;
        m_selEnd = f;
        emit frameClicked(f);
        update();
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging || m_doc.isEmpty())
        return;
    const qint64 total = qint64(m_doc.size()) / m_docChannels;
    const double framesPerCol = double(visibleFrames()) / double(std::max(1, width()));
    const qint64 f = std::clamp<qint64>(
        m_viewStart + qint64(e->x() * framesPerCol), 0, total);
    m_selStart = std::min(m_dragAnchor, f);
    m_selEnd = std::max(m_dragAnchor, f);
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging) {
        m_dragging = false;
        emit selectionChanged(m_selStart, m_selEnd);
    }
}

void WaveformView::wheelEvent(QWheelEvent* e)
{
    if (m_doc.isEmpty())
        return;
    const double oldZoom = m_zoom;
    if (e->angleDelta().y() > 0)
        setZoom(m_zoom * 1.25);
    else
        setZoom(m_zoom / 1.25);
    // keep the pointer roughly anchored
    if (m_zoom > oldZoom)
        m_viewStart += qint64(double(visibleFrames()) * 0.06);
    update();
}

} // namespace vc
