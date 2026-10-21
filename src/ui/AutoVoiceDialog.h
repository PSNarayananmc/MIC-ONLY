#pragma once
#include <QDialog>
#include <QElapsedTimer>
#include "../autotune/VoiceAnalyzer.h"
#include <atomic>
#include <vector>

class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;

namespace vc {

class MainWindow;
class AudioSource;

// AUTO STUDIO VOICE flow:
//   consent explanation -> record ~6 s -> analyse -> show findings
//   -> apply conservative recommendation (user confirms each step)
class AutoVoiceDialog : public QDialog {
    Q_OBJECT
public:
    explicit AutoVoiceDialog(MainWindow* win, QWidget* parent = nullptr);
    ~AutoVoiceDialog() override;

signals:
    void recommendationReady(const vc::DspParams& p);

private slots:
    void beginSample();
    void finishSample();
    void applyRecommendation();

private:
    enum class Stage { Intro, Recording, Done };
    void setStage(Stage s);

    MainWindow* m_win;
    QLabel* m_text = nullptr;
    QPushButton* m_actionBtn = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_elapsed;
    std::vector<float> m_samples;      // preallocated raw sink
    std::atomic<qint64> m_rawCount{0};
    AudioSource* m_sampleSource = nullptr;
    VoiceAnalysisResult m_result;
    Stage m_stage = Stage::Intro;
};

} // namespace vc
