#pragma once
#include <QWidget>
#include <atomic>
#include <array>

namespace vc {

// Professional segmented peak meter (PPM style) with peak-hold marker,
// dB scale and clip indicator. Value fed from an atomic (polled by timer);
// the widget itself never talks to the audio thread.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    enum Orientation { Vertical, Horizontal };

    explicit LevelMeter(Orientation o = Vertical, QWidget* parent = nullptr);

    void setValue(float peakLin, float rmsLin);   // 0..1 linear
    void setLevelSource(std::atomic<float>* peak) { m_atomicPeak = peak; }
    void clearPeaks();

protected:
    void paintEvent(QPaintEvent*) override;
    QSize minimumSizeHint() const override;

private:
    Orientation m_ori;
    std::atomic<float>* m_atomicPeak = nullptr;
    float m_peak = 0.0f, m_rms = 0.0f;
    float m_peakHold = 0.0f;
    int   m_peakHoldAge = 0;
    bool  m_clip = false;
    int   m_clipAge = 0;
    std::array<float, 8> m_segmentDecay{};
};

} // namespace vc
