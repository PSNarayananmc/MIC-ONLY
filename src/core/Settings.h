#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace vc {

struct AppSettings {
    // Audio
    QString sourceName;            // empty = system default source
    int     sampleRate = 48000;
    int     blockSize  = 480;      // 10 ms @ 48 kHz
    bool    lowCpuMode = false;    // compatibility mode for J1800-class CPUs
    bool    rtAudioThread = true;  // try SCHED_FIFO for the DSP thread
    int     rtPriority = 40;

    // Behaviour
    bool    startWithEngine = false;
    bool    rememberLastPreset = true;
    QString lastPreset = QStringLiteral("Podcast Studio");
    bool    neverTouchDefaultSource = true; // safety interlock, always on

    // Recording
    QString recordingDir;          // default ~/VoiceCraftStudio/Recordings
    QString recordingFormat = QStringLiteral("FLAC"); // WAV | FLAC
    bool    recordingMono = false;

    // Monitoring (zero-latency style local monitoring is off by default to
    // avoid feedback; users enable when wearing headphones)
    bool    localMonitor = false;
    float   monitorVolume = 0.5f;

    void load(QSettings& s);
    void save(QSettings& s) const;
};

// Thin QObject wrapper that owns QSettings and emits change notifications.
class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(QObject* parent = nullptr);
    const AppSettings& get() const { return m_data; }
    void update(const AppSettings& next);

    QString settingsFilePath() const;

signals:
    void changed();

private:
    QSettings m_q;
    AppSettings m_data;
};

} // namespace vc
