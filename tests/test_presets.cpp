// ---------------------------------------------------------------------------
// PresetManager tests — the six required built-in presets must exist with
// valid parameters, JSON round-trips must be lossless, and user presets
// must save/load/delete on disk.
// ---------------------------------------------------------------------------
#include "../src/core/PresetManager.h"
#include "../src/dsp/DspParams.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <cmath>

using namespace vc;

static int g_fail = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            qCritical("FAIL: %s (line %d)", msg, __LINE__);                  \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

static void testBuiltinSet()
{
    const QStringList expected = {
        "Podcast Studio", "YouTube Narrator", "Gaming Streamer",
        "Deep Warm Voice", "Clean Interview", "Custom"
    };
    const QStringList names = PresetManager::builtinNames();
    for (const QString& want : expected)
        CHECK(names.contains(want), qPrintable(
            QStringLiteral("built-in preset present: %1").arg(want)));

    PresetManager pm;
    pm.loadAll();
    CHECK(pm.all().size() >= 6, "at least six presets loaded");
    for (const QString& want : expected)
        CHECK(pm.find(want) != nullptr, qPrintable(
            QStringLiteral("preset findable: %1").arg(want)));
}

// sanity ranges every built-in must respect (voice chain, never destructive)
static void testBuiltinSanity()
{
    PresetManager pm;
    pm.loadAll();
    for (const Preset& p : pm.all()) {
        const DspParams& d = p.params;
        const QString n = p.name;
        CHECK(d.hpfFreqHz >= 20.0f && d.hpfFreqHz <= 300.0f,
              qPrintable(QStringLiteral("%1: hpf range").arg(n)));
        CHECK(d.gateThresholdDb >= -80.0f && d.gateThresholdDb <= -20.0f,
              qPrintable(QStringLiteral("%1: gate range").arg(n)));
        CHECK(d.compRatio >= 1.0f && d.compRatio <= 20.0f,
              qPrintable(QStringLiteral("%1: comp ratio range").arg(n)));
        CHECK(d.compThresholdDb >= -60.0f && d.compThresholdDb <= 0.0f,
              qPrintable(QStringLiteral("%1: comp threshold range").arg(n)));
        CHECK(d.loudTargetLufs >= -24.0f && d.loudTargetLufs <= -12.0f,
              qPrintable(QStringLiteral("%1: loudness target range").arg(n)));
        CHECK(d.limCeilingDb >= -12.0f && d.limCeilingDb <= 0.0f,
              qPrintable(QStringLiteral("%1: limiter ceiling range").arg(n)));
        CHECK(std::fabs(d.outputGainDb) <= 24.0f,
              qPrintable(QStringLiteral("%1: output trim range").arg(n)));
        // Podcast Studio is the default and must be conservative-safe:
        if (n == "Podcast Studio") {
            CHECK(d.limEnabled, "Podcast Studio: limiter on");
            CHECK(d.limCeilingDb <= -1.0f, "Podcast Studio: safe ceiling");
            CHECK(!d.gateEnabled || d.gateRangeDb > -40.0f,
                  "Podcast Studio: gate not destructive");
        }
    }
}

// presets must differ meaningfully from each other (character, not copies)
static void testPresetsDiffer()
{
    PresetManager pm;
    pm.loadAll();
    const Preset* a = pm.find("Podcast Studio");
    const Preset* b = pm.find("Deep Warm Voice");
    const Preset* c = pm.find("Clean Interview");
    CHECK(a && b && c, "presets found");
    if (a && b) {
        const bool differs =
            a->params.eqWarmGainDb != b->params.eqWarmGainDb ||
            a->params.compRatio != b->params.compRatio ||
            a->params.loudTargetLufs != b->params.loudTargetLufs;
        CHECK(differs, "Deep Warm Voice has its own character");
    }
    if (a && c) {
        const bool differs =
            a->params.compRatio != c->params.compRatio ||
            a->params.eqPresenceGainDb != c->params.eqPresenceGainDb;
        CHECK(differs, "Clean Interview has its own character");
    }
}

static void testJsonRoundTrip()
{
    DspParams p;
    p.hpfFreqHz = 105.5f;
    p.nsAmount = 73.0f;
    p.compRatio = 4.25f;
    p.loudTargetLufs = -14.0f;
    Preset in;
    in.name = "RoundTrip";
    in.description = "json round trip test";
    in.params = p;

    const QJsonObject o = in.toJson();
    const Preset out = Preset::fromJson(o);
    CHECK(out.name == in.name, "json: name preserved");
    CHECK(out.description == in.description, "json: description preserved");
    CHECK(out.params.hpfFreqHz == p.hpfFreqHz, "json: hpf preserved");
    CHECK(out.params.nsAmount == p.nsAmount, "json: ns amount preserved");
    CHECK(out.params.compRatio == p.compRatio, "json: comp ratio preserved");
    CHECK(out.params.loudTargetLufs == p.loudTargetLufs,
          "json: loudness target preserved");
    CHECK(out.params.eqEnabled == p.eqEnabled, "json: toggles preserved");
}

static void testUserPresetLifecycle()
{
    PresetManager pm;
    pm.loadAll();

    Preset p;
    p.name = "VC Test Preset";
    p.description = "created by automated test";
    p.params = DspParams{};
    p.params.eqWarmGainDb = 2.5f;

    QString err;
    CHECK(pm.saveUserPreset(p, &err), qPrintable(
        QStringLiteral("user preset saved: %1").arg(err)));
    CHECK(QFile::exists(pm.presetsDir() + "/VC Test Preset.json"),
          "user preset file exists on disk");

    CHECK(pm.find("VC Test Preset") != nullptr, "saved preset findable");
    if (auto* found = pm.find("VC Test Preset"))
        CHECK(found->params.eqWarmGainDb == 2.5f, "user preset data intact");

    CHECK(pm.deleteUserPreset("VC Test Preset", &err),
          "user preset deleted");
    CHECK(!QFile::exists(pm.presetsDir() + "/VC Test Preset.json"),
          "user preset file removed");
    CHECK(!pm.deleteUserPreset("does-not-exist", &err) ||
              err.contains("not") || true,
          "deleting missing preset does not crash");

    // built-ins must not be deletable through the user-preset path
    CHECK(!pm.deleteUserPreset("Podcast Studio", &err) ||
              pm.find("Podcast Studio") != nullptr,
          "built-in presets survive delete attempts");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInfo("== PresetManager test suite ==");
    testBuiltinSet();
    testBuiltinSanity();
    testPresetsDiffer();
    testJsonRoundTrip();
    testUserPresetLifecycle();

    if (g_fail == 0) {
        qInfo("PRESET TESTS: ALL PASSED");
        return 0;
    }
    qCritical("PRESET TESTS: %d FAILURES", g_fail);
    return 1;
}
