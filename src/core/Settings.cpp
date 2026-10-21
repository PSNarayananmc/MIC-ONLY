#include "Settings.h"
#include <QStandardPaths>
#include <QDir>

namespace vc {

void AppSettings::load(QSettings& s) {
    sourceName        = s.value("audio/sourceName", sourceName).toString();
    sampleRate        = s.value("audio/sampleRate", sampleRate).toInt();
    blockSize         = s.value("audio/blockSize", blockSize).toInt();
    lowCpuMode        = s.value("audio/lowCpuMode", lowCpuMode).toBool();
    rtAudioThread     = s.value("audio/rtAudioThread", rtAudioThread).toBool();
    rtPriority        = s.value("audio/rtPriority", rtPriority).toInt();
    startWithEngine   = s.value("behaviour/startWithEngine", startWithEngine).toBool();
    rememberLastPreset= s.value("behaviour/rememberLastPreset", rememberLastPreset).toBool();
    lastPreset        = s.value("behaviour/lastPreset", lastPreset).toString();
    neverTouchDefaultSource = s.value("safety/neverTouchDefaultSource", true).toBool();
    recordingDir      = s.value("recording/dir", recordingDir).toString();
    recordingFormat   = s.value("recording/format", recordingFormat).toString();
    recordingMono     = s.value("recording/mono", recordingMono).toBool();
    localMonitor      = s.value("monitor/enabled", localMonitor).toBool();
    monitorVolume     = s.value("monitor/volume", monitorVolume).toFloat();
}

void AppSettings::save(QSettings& s) const {
    s.setValue("audio/sourceName", sourceName);
    s.setValue("audio/sampleRate", sampleRate);
    s.setValue("audio/blockSize", blockSize);
    s.setValue("audio/lowCpuMode", lowCpuMode);
    s.setValue("audio/rtAudioThread", rtAudioThread);
    s.setValue("audio/rtPriority", rtPriority);
    s.setValue("behaviour/startWithEngine", startWithEngine);
    s.setValue("behaviour/rememberLastPreset", rememberLastPreset);
    s.setValue("behaviour/lastPreset", lastPreset);
    s.setValue("safety/neverTouchDefaultSource", neverTouchDefaultSource);
    s.setValue("recording/dir", recordingDir);
    s.setValue("recording/format", recordingFormat);
    s.setValue("recording/mono", recordingMono);
    s.setValue("monitor/enabled", localMonitor);
    s.setValue("monitor/volume", monitorVolume);
}

SettingsStore::SettingsStore(QObject* parent)
    : QObject(parent)
    , m_q(QStringLiteral("VoiceCraftStudio"), QStringLiteral("voicecraft-studio"))
{
    if (m_q.value("recording/dir").isNull()) {
        const QString def = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                            + QStringLiteral("/VoiceCraftStudio/Recordings");
        m_data.recordingDir = def;
    }
    m_data.load(m_q);
}

QString SettingsStore::settingsFilePath() const {
    return m_q.fileName();
}

void SettingsStore::update(const AppSettings& next) {
    m_data = next;
    m_data.save(m_q);
    m_q.sync();
    emit changed();
}

} // namespace vc
