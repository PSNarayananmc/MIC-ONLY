#pragma once
#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;
class QComboBox;
class QTimer;

namespace vc {

class LevelMeter;
class WaveformView;
class MainWindow;
struct PaSourceInfo;

// Main studio panel: device selection, START/STOP, live scope, meters,
// loudness readouts, latency, diagnostics, AUTO STUDIO VOICE and repair.
class StudioTab : public QWidget {
    Q_OBJECT
public:
    explicit StudioTab(MainWindow* win, QWidget* parent = nullptr);

    void refreshDeviceList(const QVector<PaSourceInfo>& sources);
    void updateStatus();
    void updateMeters();
    void updateLoudness();

signals:
    void startRequested();
    void stopRequested();

private slots:
    void onDeviceChosen(const QString& name);

private:
    MainWindow* m_win;
    QComboBox* m_deviceCombo = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_repairBtn = nullptr;
    QPushButton* m_autoVoiceBtn = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_obsLabel = nullptr;
    QLabel* m_latencyLabel = nullptr;
    QLabel* m_xrunLabel = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_lufsM = nullptr;
    QLabel* m_lufsS = nullptr;
    QLabel* m_lufsI = nullptr;
    QLabel* m_truePeak = nullptr;
    QLabel* m_rmsLabel = nullptr;
    WaveformView* m_scope = nullptr;
    LevelMeter* m_inMeter = nullptr;
    LevelMeter* m_outMeter = nullptr;
};

} // namespace vc
