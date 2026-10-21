#pragma once
#include <QWidget>
#include <QElapsedTimer>

class QLabel;
class QPushButton;

namespace vc {

class MainWindow;
class WaveformView;

// Recording workspace: captures the processed stream, shows the waveform,
// supports pause/resume, zoom, trim/split, undo/redo and export.
class RecordingTab : public QWidget {
    Q_OBJECT
public:
    explicit RecordingTab(MainWindow* win, QWidget* parent = nullptr);

    void refreshWaveform();
    void tickRecordingTime();

private slots:
    void recStart();
    void recPause();
    void recStop();
    void onRecorderFinished(const QString& path, double seconds);
    void exportRecording();
    void trimToSelection();
    void splitAtPlayhead();
    void undoEdit();
    void redoEdit();

private:
    void updateButtonStates();
    MainWindow* m_win;
    WaveformView* m_wave = nullptr;
    QPushButton* m_recBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_trimBtn = nullptr;
    QPushButton* m_splitBtn = nullptr;
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_redoBtn = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_selInfo = nullptr;
    QElapsedTimer m_recTimer;
    qint64 m_pauseFrames = 0;
    bool m_paused = false;
    bool m_engineWasRunning = false;
    qint64 m_selStartFrame = -1, m_selEndFrame = -1;
    qint64 m_playheadFrame = -1;
};

} // namespace vc
