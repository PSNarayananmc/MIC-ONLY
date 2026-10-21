#pragma once
#include <QWidget>
#include <QVector>
#include <deque>

namespace vc {

// Dual-purpose waveform widget:
//  1) live scrolling scope fed by polled atomics-driven pushSampleBlock()
//  2) static document display (recording workspace) with zoom + selection
class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);

    // live scope mode
    void pushBlock(const float* interleaved, int frames, int channels);
    void setLive(bool live);

    // document mode
    void setDocument(const QVector<float>& interleaved, int channels,
                     int sampleRate);
    void clearDocument();
    void setSelection(qint64 startFrame, qint64 endFrame);  // -1 = none
    void setPlayheadFrame(qint64 frame);

    // zoom for document mode: 1.0 = whole file, >1 zoomed
    void setZoom(double factor);
    double zoom() const { return m_zoom; }
    void setScrollFraction(double f);       // 0..1 horizontal position
    qint64 viewStartFrame() const { return m_viewStart; }
    qint64 visibleFrames() const;

    signals:
    // user clicked at a frame (document mode)
    void frameClicked(qint64 frame);
    void selectionChanged(qint64 startFrame, qint64 endFrame);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    QSize minimumSizeHint() const override { return {240, 120}; }

private:
    void rebuildCache();
    float rangeAt(int x, float& lo, float& hi) const;

    bool m_liveMode = true;
    // live ring of recent min/max pairs
    static constexpr int LIVE_COLS = 512;
    std::deque<QPair<float, float>> m_liveBuf;
    float m_liveScale = 1.0f;

    // document mode
    QVector<float> m_doc;
    int m_docChannels = 1, m_docRate = 48000;
    double m_zoom = 1.0;
    qint64 m_viewStart = 0;
    qint64 m_selStart = -1, m_selEnd = -1;
    qint64 m_playhead = -1;
    bool m_dragging = false;
    qint64 m_dragAnchor = -1;
};

} // namespace vc
