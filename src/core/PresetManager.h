#pragma once
#include "../dsp/DspParams.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QVector>

namespace vc {

struct Preset {
    QString   name;
    QString   description;
    DspParams params;

    QJsonObject toJson() const;
    static Preset fromJson(const QJsonObject& o);
};

// Owns the six built-in presets plus user presets stored as JSON files in
// ~/.config/VoiceCraftStudio/presets/. Switching presets publishes a new
// parameter snapshot; the DSP smooths the transition at block rate, so
// there are no clicks when a stream is live.
class PresetManager : public QObject {
    Q_OBJECT
public:
    explicit PresetManager(QObject* parent = nullptr);

    static QStringList builtinNames();

    void loadAll();
    const QVector<Preset>& all() const { return m_presets; }
    const Preset* find(const QString& name) const;
    Preset builtin(const QString& name) const;

    bool saveUserPreset(const Preset& p, QString* err = nullptr);
    bool deleteUserPreset(const QString& name, QString* err = nullptr);

    QString presetsDir() const { return m_dir; }

signals:
    void presetsChanged();

private:
    static Preset makePodcastStudio();
    static Preset makeYouTubeNarrator();
    static Preset makeGamingStreamer();
    static Preset makeDeepWarmVoice();
    static Preset makeCleanInterview();
    static Preset makeCustom();

    QVector<Preset> m_presets;
    QString m_dir;
};

} // namespace vc
