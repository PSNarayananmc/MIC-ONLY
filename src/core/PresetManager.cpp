#include "PresetManager.h"
#include "Log.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace vc {

// ----------------------------- JSON codec --------------------------------
QJsonObject Preset::toJson() const
{
    QJsonObject o;
    o["name"] = name;
    o["description"] = description;
    const QJsonObject d {
        { "hpfEnabled", params.hpfEnabled },   { "hpfFreqHz", double(params.hpfFreqHz) },
        { "nsEnabled", params.nsEnabled },     { "nsAmount", double(params.nsAmount) },
        { "gateEnabled", params.gateEnabled }, { "gateThresholdDb", double(params.gateThresholdDb) },
        { "gateRangeDb", double(params.gateRangeDb) },
        { "gateAttackMs", double(params.gateAttackMs) },
        { "gateHoldMs", double(params.gateHoldMs) },
        { "gateReleaseMs", double(params.gateReleaseMs) },
        { "eqEnabled", params.eqEnabled },
        { "eqWarmGainDb", double(params.eqWarmGainDb) },
        { "eqMudFreqHz", double(params.eqMudFreqHz) },
        { "eqMudGainDb", double(params.eqMudGainDb) }, { "eqMudQ", double(params.eqMudQ) },
        { "eqPresenceFreqHz", double(params.eqPresenceFreqHz) },
        { "eqPresenceGainDb", double(params.eqPresenceGainDb) },
        { "eqPresenceQ", double(params.eqPresenceQ) },
        { "eqClarityFreqHz", double(params.eqClarityFreqHz) },
        { "eqClarityGainDb", double(params.eqClarityGainDb) },
        { "eqClarityQ", double(params.eqClarityQ) },
        { "eqAirGainDb", double(params.eqAirGainDb) },
        { "deessEnabled", params.deessEnabled },
        { "deessFreqHz", double(params.deessFreqHz) },
        { "deessAmount", double(params.deessAmount) },
        { "compEnabled", params.compEnabled },
        { "compThresholdDb", double(params.compThresholdDb) },
        { "compRatio", double(params.compRatio) },
        { "compKneeDb", double(params.compKneeDb) },
        { "compAttackMs", double(params.compAttackMs) },
        { "compReleaseMs", double(params.compReleaseMs) },
        { "compMakeupDb", double(params.compMakeupDb) },
        { "satEnabled", params.satEnabled }, { "satDrive", double(params.satDrive) },
        { "loudEnabled", params.loudEnabled },
        { "loudTargetLufs", double(params.loudTargetLufs) },
        { "loudMaxGainDb", double(params.loudMaxGainDb) },
        { "limEnabled", params.limEnabled },
        { "limCeilingDb", double(params.limCeilingDb) },
        { "limReleaseMs", double(params.limReleaseMs) },
        { "outputGainDb", double(params.outputGainDb) }
    };
    o["dsp"] = d;
    return o;
}

Preset Preset::fromJson(const QJsonObject& o)
{
    Preset p;
    p.name = o["name"].toString();
    p.description = o["description"].toString();
    const QJsonObject d = o["dsp"].toObject();
    auto f = [&d](const char* k, float def) {
        return d.contains(k) ? float(d[k].toDouble()) : def;
    };
    auto b = [&d](const char* k, bool def) {
        return d.contains(k) ? d[k].toBool() : def;
    };
    p.params.hpfEnabled = b("hpfEnabled", true);
    p.params.hpfFreqHz = f("hpfFreqHz", 80);
    p.params.nsEnabled = b("nsEnabled", true);
    p.params.nsAmount = f("nsAmount", 65);
    p.params.gateEnabled = b("gateEnabled", false);
    p.params.gateThresholdDb = f("gateThresholdDb", -48);
    p.params.gateRangeDb = f("gateRangeDb", -18);
    p.params.gateAttackMs = f("gateAttackMs", 5);
    p.params.gateHoldMs = f("gateHoldMs", 120);
    p.params.gateReleaseMs = f("gateReleaseMs", 180);
    p.params.eqEnabled = b("eqEnabled", true);
    p.params.eqWarmGainDb = f("eqWarmGainDb", 0);
    p.params.eqMudFreqHz = f("eqMudFreqHz", 320);
    p.params.eqMudGainDb = f("eqMudGainDb", 0);
    p.params.eqMudQ = f("eqMudQ", 1.2f);
    p.params.eqPresenceFreqHz = f("eqPresenceFreqHz", 3000);
    p.params.eqPresenceGainDb = f("eqPresenceGainDb", 0);
    p.params.eqPresenceQ = f("eqPresenceQ", 1.0f);
    p.params.eqClarityFreqHz = f("eqClarityFreqHz", 6500);
    p.params.eqClarityGainDb = f("eqClarityGainDb", 0);
    p.params.eqClarityQ = f("eqClarityQ", 0.9f);
    p.params.eqAirGainDb = f("eqAirGainDb", 0);
    p.params.deessEnabled = b("deessEnabled", true);
    p.params.deessFreqHz = f("deessFreqHz", 6500);
    p.params.deessAmount = f("deessAmount", 45);
    p.params.compEnabled = b("compEnabled", true);
    p.params.compThresholdDb = f("compThresholdDb", -18);
    p.params.compRatio = f("compRatio", 3);
    p.params.compKneeDb = f("compKneeDb", 6);
    p.params.compAttackMs = f("compAttackMs", 12);
    p.params.compReleaseMs = f("compReleaseMs", 140);
    p.params.compMakeupDb = f("compMakeupDb", 0);
    p.params.satEnabled = b("satEnabled", false);
    p.params.satDrive = f("satDrive", 25);
    p.params.loudEnabled = b("loudEnabled", true);
    p.params.loudTargetLufs = f("loudTargetLufs", -16);
    p.params.loudMaxGainDb = f("loudMaxGainDb", 12);
    p.params.limEnabled = b("limEnabled", true);
    p.params.limCeilingDb = f("limCeilingDb", -1.5f);
    p.params.limReleaseMs = f("limReleaseMs", 80);
    p.params.outputGainDb = f("outputGainDb", 0);
    return p;
}

// ------------------------------ presets ------------------------------------
Preset PresetManager::makePodcastStudio()
{
    Preset p;
    p.name = QStringLiteral("Podcast Studio");
    p.description = QStringLiteral(
        "Balanced broadcast voice: warm lows, present mids, controlled sibilance. "
        "Default for talking shows and interviews.");
    p.params.hpfEnabled = true;      p.params.hpfFreqHz = 80;
    p.params.nsEnabled = true;       p.params.nsAmount = 60;
    p.params.gateEnabled = false;
    p.params.eqEnabled = true;
    p.params.eqWarmGainDb = 1.5f;
    p.params.eqMudFreqHz = 320;      p.params.eqMudGainDb = -2.5f; p.params.eqMudQ = 1.2f;
    p.params.eqPresenceFreqHz = 3000;p.params.eqPresenceGainDb = 2.0f; p.params.eqPresenceQ = 1.0f;
    p.params.eqClarityFreqHz = 6500; p.params.eqClarityGainDb = 1.0f;  p.params.eqClarityQ = 0.9f;
    p.params.eqAirGainDb = 1.0f;
    p.params.deessEnabled = true;    p.params.deessFreqHz = 6500; p.params.deessAmount = 45;
    p.params.compEnabled = true;     p.params.compThresholdDb = -18; p.params.compRatio = 3;
    p.params.compKneeDb = 6;         p.params.compAttackMs = 12; p.params.compReleaseMs = 140;
    p.params.compMakeupDb = 4;
    p.params.satEnabled = false;
    p.params.loudEnabled = true;     p.params.loudTargetLufs = -16; p.params.loudMaxGainDb = 12;
    p.params.limEnabled = true;      p.params.limCeilingDb = -1.5; p.params.limReleaseMs = 80;
    return p;
}

Preset PresetManager::makeYouTubeNarrator()
{
    Preset p;
    p.name = QStringLiteral("YouTube Narrator");
    p.description = QStringLiteral(
        "Forward, energetic narration voice with tighter lows for small speakers.");
    p.params.hpfEnabled = true;      p.params.hpfFreqHz = 95;
    p.params.nsEnabled = true;       p.params.nsAmount = 70;
    p.params.gateEnabled = true;     p.params.gateThresholdDb = -45; p.params.gateRangeDb = -24;
    p.params.gateAttackMs = 4;       p.params.gateHoldMs = 150; p.params.gateReleaseMs = 200;
    p.params.eqEnabled = true;
    p.params.eqWarmGainDb = 0.5f;
    p.params.eqMudFreqHz = 350;      p.params.eqMudGainDb = -3.5f; p.params.eqMudQ = 1.4f;
    p.params.eqPresenceFreqHz = 3500;p.params.eqPresenceGainDb = 3.0f; p.params.eqPresenceQ = 0.9f;
    p.params.eqClarityFreqHz = 7000; p.params.eqClarityGainDb = 1.5f;  p.params.eqClarityQ = 0.9f;
    p.params.eqAirGainDb = 1.5f;
    p.params.deessEnabled = true;    p.params.deessFreqHz = 6800; p.params.deessAmount = 50;
    p.params.compEnabled = true;     p.params.compThresholdDb = -20; p.params.compRatio = 4;
    p.params.compKneeDb = 4;         p.params.compAttackMs = 8;  p.params.compReleaseMs = 120;
    p.params.compMakeupDb = 5;
    p.params.satEnabled = true;      p.params.satDrive = 18;
    p.params.loudEnabled = true;     p.params.loudTargetLufs = -14; p.params.loudMaxGainDb = 12;
    p.params.limEnabled = true;      p.params.limCeilingDb = -1.0; p.params.limReleaseMs = 60;
    return p;
}

Preset PresetManager::makeGamingStreamer()
{
    Preset p;
    p.name = QStringLiteral("Gaming Streamer");
    p.description = QStringLiteral(
        "Cut-through voice over game audio, fast gate for keyboard clacks, "
        "maximum intelligibility.");
    p.params.hpfEnabled = true;      p.params.hpfFreqHz = 100;
    p.params.nsEnabled = true;       p.params.nsAmount = 75;
    p.params.gateEnabled = true;     p.params.gateThresholdDb = -42; p.params.gateRangeDb = -30;
    p.params.gateAttackMs = 2;       p.params.gateHoldMs = 100; p.params.gateReleaseMs = 150;
    p.params.eqEnabled = true;
    p.params.eqWarmGainDb = 0.0f;
    p.params.eqMudFreqHz = 400;      p.params.eqMudGainDb = -4.0f; p.params.eqMudQ = 1.5f;
    p.params.eqPresenceFreqHz = 4000;p.params.eqPresenceGainDb = 4.0f; p.params.eqPresenceQ = 0.8f;
    p.params.eqClarityFreqHz = 8000; p.params.eqClarityGainDb = 2.0f;  p.params.eqClarityQ = 0.8f;
    p.params.eqAirGainDb = 2.0f;
    p.params.deessEnabled = true;    p.params.deessFreqHz = 7000; p.params.deessAmount = 55;
    p.params.compEnabled = true;     p.params.compThresholdDb = -22; p.params.compRatio = 6;
    p.params.compKneeDb = 3;         p.params.compAttackMs = 4;  p.params.compReleaseMs = 100;
    p.params.compMakeupDb = 6;
    p.params.satEnabled = true;      p.params.satDrive = 30;
    p.params.loudEnabled = true;     p.params.loudTargetLufs = -14; p.params.loudMaxGainDb = 14;
    p.params.limEnabled = true;      p.params.limCeilingDb = -1.0; p.params.limReleaseMs = 50;
    return p;
}

Preset PresetManager::makeDeepWarmVoice()
{
    Preset p;
    p.name = QStringLiteral("Deep Warm Voice");
    p.description = QStringLiteral(
        "Low, intimate radio voice. Keeps chest resonance, tames harshness.");
    p.params.hpfEnabled = true;      p.params.hpfFreqHz = 65;
    p.params.nsEnabled = true;       p.params.nsAmount = 55;
    p.params.gateEnabled = false;
    p.params.eqEnabled = true;
    p.params.eqWarmGainDb = 3.5f;
    p.params.eqMudFreqHz = 300;      p.params.eqMudGainDb = -1.5f; p.params.eqMudQ = 1.0f;
    p.params.eqPresenceFreqHz = 2500;p.params.eqPresenceGainDb = 1.0f; p.params.eqPresenceQ = 1.1f;
    p.params.eqClarityFreqHz = 6000; p.params.eqClarityGainDb = 0.5f;  p.params.eqClarityQ = 0.9f;
    p.params.eqAirGainDb = 0.5f;
    p.params.deessEnabled = true;    p.params.deessFreqHz = 6200; p.params.deessAmount = 40;
    p.params.compEnabled = true;     p.params.compThresholdDb = -20; p.params.compRatio = 2.5f;
    p.params.compKneeDb = 10;        p.params.compAttackMs = 18; p.params.compReleaseMs = 220;
    p.params.compMakeupDb = 3;
    p.params.satEnabled = true;      p.params.satDrive = 22;
    p.params.loudEnabled = true;     p.params.loudTargetLufs = -17; p.params.loudMaxGainDb = 10;
    p.params.limEnabled = true;      p.params.limCeilingDb = -1.5; p.params.limReleaseMs = 100;
    return p;
}

Preset PresetManager::makeCleanInterview()
{
    Preset p;
    p.name = QStringLiteral("Clean Interview");
    p.description = QStringLiteral(
        "Transparent, almost untouched voice for two-person interviews "
        "and accurate documentation.");
    p.params.hpfEnabled = true;      p.params.hpfFreqHz = 75;
    p.params.nsEnabled = true;       p.params.nsAmount = 40;
    p.params.gateEnabled = false;
    p.params.eqEnabled = true;
    p.params.eqWarmGainDb = 0.5f;
    p.params.eqMudFreqHz = 330;      p.params.eqMudGainDb = -1.0f; p.params.eqMudQ = 1.2f;
    p.params.eqPresenceFreqHz = 3200;p.params.eqPresenceGainDb = 1.0f; p.params.eqPresenceQ = 1.0f;
    p.params.eqClarityFreqHz = 6800; p.params.eqClarityGainDb = 0.5f;  p.params.eqClarityQ = 0.9f;
    p.params.eqAirGainDb = 0.5f;
    p.params.deessEnabled = true;    p.params.deessFreqHz = 6500; p.params.deessAmount = 30;
    p.params.compEnabled = true;     p.params.compThresholdDb = -16; p.params.compRatio = 2;
    p.params.compKneeDb = 8;         p.params.compAttackMs = 15; p.params.compReleaseMs = 180;
    p.params.compMakeupDb = 2;
    p.params.satEnabled = false;
    p.params.loudEnabled = true;     p.params.loudTargetLufs = -18; p.params.loudMaxGainDb = 8;
    p.params.limEnabled = true;      p.params.limCeilingDb = -1.5; p.params.limReleaseMs = 90;
    return p;
}

Preset PresetManager::makeCustom()
{
    Preset p;
    p.name = QStringLiteral("Custom");
    p.description = QStringLiteral("Your own starting point — edit any control, "
                                   "then save as a user preset.");
    return p; // sensible neutral defaults from DspParams
}

QStringList PresetManager::builtinNames()
{
    return { "Podcast Studio", "YouTube Narrator", "Gaming Streamer",
             "Deep Warm Voice", "Clean Interview", "Custom" };
}

Preset PresetManager::builtin(const QString& name) const
{
    if (name == "YouTube Narrator")    return makeYouTubeNarrator();
    if (name == "Gaming Streamer")     return makeGamingStreamer();
    if (name == "Deep Warm Voice")     return makeDeepWarmVoice();
    if (name == "Clean Interview")     return makeCleanInterview();
    if (name == "Custom")              return makeCustom();
    return makePodcastStudio();
}

// ------------------------------ storage ------------------------------------
PresetManager::PresetManager(QObject* parent)
    : QObject(parent)
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/presets");
}

void PresetManager::loadAll()
{
    m_presets.clear();
    m_presets << makePodcastStudio() << makeYouTubeNarrator()
              << makeGamingStreamer() << makeDeepWarmVoice()
              << makeCleanInterview() << makeCustom();

    QDir d(m_dir);
    if (!d.exists())
        QDir().mkpath(m_dir);
    const auto files = d.entryList({ "*.json" }, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        QFile file(d.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        Preset p = Preset::fromJson(doc.object());
        if (p.name.isEmpty())
            p.name = f;
        p.name.replace(QStringLiteral(".json"), QString(), Qt::CaseInsensitive);
        // built-ins cannot be overridden by files of the same name
        if (!builtinNames().contains(p.name))
            m_presets << p;
    }
    emit presetsChanged();
}

const Preset* PresetManager::find(const QString& name) const
{
    for (const auto& p : m_presets)
        if (p.name == name)
            return &p;
    return nullptr;
}

bool PresetManager::saveUserPreset(const Preset& p, QString* err)
{
    if (p.name.isEmpty()) {
        if (err) *err = QStringLiteral("preset name is empty");
        return false;
    }
    if (builtinNames().contains(p.name)) {
        if (err) *err = QStringLiteral("cannot overwrite a built-in preset");
        return false;
    }
    QDir().mkpath(m_dir);
    QFile file(m_dir + QStringLiteral("/") + p.name + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = file.errorString();
        return false;
    }
    file.write(QJsonDocument(p.toJson()).toJson(QJsonDocument::Indented));
    file.close();   // flush BEFORE loadAll() re-reads the file
    loadAll();
    return true;
}

bool PresetManager::deleteUserPreset(const QString& name, QString* err)
{
    if (builtinNames().contains(name)) {
        if (err) *err = QStringLiteral("built-in presets cannot be deleted");
        return false;
    }
    const QString path = m_dir + QStringLiteral("/") + name + QStringLiteral(".json");
    if (!QFile::exists(path)) {
        if (err) *err = QStringLiteral("preset file not found");
        return false;
    }
    if (!QFile::remove(path)) {
        if (err) *err = QStringLiteral("failed to delete %1").arg(path);
        return false;
    }
    loadAll();
    return true;
}

} // namespace vc
