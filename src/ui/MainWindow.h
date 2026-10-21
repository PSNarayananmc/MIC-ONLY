#pragma once
#include "../audio/AudioPipeline.h"
#include "../core/Settings.h"
#include "../core/PresetManager.h"
#include "../recorder/Recorder.h"
#include "../recorder/AudioDocument.h"
#include <QMainWindow>
#include <QTimer>
#include <memory>

class QTabWidget;
class QLabel;
namespace Ui { } // no uic: tabs are built in code

namespace vc {

class StudioTab;
class RecordingTab;
class MixerTab;
class PresetsTab;
class SettingsTab;
class AutoVoiceDialog;

// Application shell: owns the engine, wires tabs together and drives the
// 30 Hz UI poll. Audio never calls into the GUI directly.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // helpers for tabs
    AudioPipeline& engine() { return m_pipeline; }
    SettingsStore& settings() { return m_settings; }
    PresetManager& presets() { return m_presets; }
    DspParams currentParams() const { return m_params; }
    void publishParams(const DspParams& p);
    void applyPresetByName(const QString& name);
    void pushStatus(const QString& text, bool isError = false);

    // recording plumbing (RecordingTab + StudioTab recorder tap)
    Recorder& recorder() { return m_recorder; }
    void setRecordingTapEnabled(bool on);      // tap the processed stream
    AudioDocument& currentDocument() { return m_doc; }
    void loadDocumentFromFile(const QString& path);
    void openAutoVoice();

public slots:
    void startEngine();
    void stopEngine();
    void repairAudio();
    void refreshDevices();

private:
    void buildUi();
    void wireConnections();
    void poll();
    void onPipelineError(const QString& msg);
    void onSourceLost();

    SettingsStore m_settings;
    PresetManager m_presets;
    AudioPipeline m_pipeline;
    Recorder m_recorder;
    AudioDocument m_doc;
    DspParams m_params{};

    QTabWidget* m_tabs = nullptr;
    StudioTab* m_studioTab = nullptr;
    RecordingTab* m_recordingTab = nullptr;
    MixerTab* m_mixerTab = nullptr;
    PresetsTab* m_presetsTab = nullptr;
    SettingsTab* m_settingsTab = nullptr;
    QTimer m_pollTimer;
    QLabel* m_status = nullptr;
    QLabel* m_statusRight = nullptr;
    std::unique_ptr<AutoVoiceDialog> m_autoDialog;
    qint64 m_lastDocFrames = 0;
};

} // namespace vc
