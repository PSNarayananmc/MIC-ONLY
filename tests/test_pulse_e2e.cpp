// ---------------------------------------------------------------------------
// End-to-end PulseAudio integration test — the OBS contract.
//
// Requires a running PulseAudio daemon with a "FakeMicSink" null-sink whose
// monitor acts as the fake microphone. Verifies:
//   1. the virtual microphone "VoiceCraft Studio Microphone" is created and
//      visible to clients exactly as OBS will see it
//   2. audio pumped into the fake mic flows through the whole pipeline
//      (capture -> DSP -> null sink -> monitor -> virtual source) and is
//      received on the virtual microphone with correct tone content
//   3. stop() removes EVERYTHING (no leftover modules / devices)
//   4. five start/stop cycles leave no duplicates
//   5. "Repair Audio Connection" rebuilds destroyed modules while running
// ---------------------------------------------------------------------------
#include "../src/audio/AudioPipeline.h"
#include "../src/audio/PulseContext.h"
#include "../src/core/Settings.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <pulse/simple.h>
#include <cmath>
#include <atomic>
#include <thread>
#include <vector>

using namespace vc;

static int g_fail = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            qCritical("FAIL: %s (line %d)", msg, __LINE__);                  \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

static constexpr int      FS      = 48000;
static constexpr int      CH      = 2;
static const char*        FAKE_MIC_MONITOR = "FakeMicSink.monitor";
static const char*        VIRTUAL_SRC      = "VoiceCraft_Studio_Microphone";
static const char*        VIRTUAL_SINK     = "VoiceCraft_Output";

// --- helper: blocking pa_simple wrappers -----------------------------------
struct SimplePd {
    pa_simple* pd = nullptr;
    ~SimplePd() { if (pd) pa_simple_free(pd); }
};

static pa_sample_spec monoSpec() { return { PA_SAMPLE_FLOAT32LE, 48000, 2 }; }

static SimplePd openPlayback(const char* sink)
{
    SimplePd s;
    const pa_sample_spec ss = monoSpec();
    pa_buffer_attr attr{};
    attr.maxlength = uint32_t(-1);
    attr.tlength   = uint32_t(FS * sizeof(float) * CH / 25);  // 40 ms
    attr.prebuf    = uint32_t(attr.tlength);
    attr.minreq    = uint32_t(FS * sizeof(float) * CH / 50);
    s.pd = pa_simple_new(nullptr, "vc-e2e-play", PA_STREAM_PLAYBACK,
                         sink, "e2e playback", &ss, nullptr, &attr,
                         nullptr);
    return s;
}

static SimplePd openCapture(const char* source)
{
    SimplePd s;
    const pa_sample_spec ss = monoSpec();
    pa_buffer_attr attr{};
    attr.maxlength = uint32_t(-1);
    attr.fragsize  = uint32_t(FS * sizeof(float) * CH / 100); // 10 ms
    s.pd = pa_simple_new(nullptr, "vc-e2e-rec", PA_STREAM_RECORD,
                         source, "e2e capture", &ss, nullptr,
                         &attr, nullptr);
    return s;
}

// sine generator thread: continuously feeds the fake microphone
static std::atomic<bool> g_feederRun{false};

static void startFeeder(std::thread& t, SimplePd& play)
{
    g_feederRun = true;
    t = std::thread([&play] {
        std::vector<float> blk(480 * CH);
        int phase = 0;
        while (g_feederRun) {
            for (int i = 0; i < 480; ++i, ++phase)
                for (int c = 0; c < CH; ++c)
                    blk[std::size_t(i) * CH + c] =
                        float(0.20 * std::sin(2.0 * M_PI * 220.0 * phase / FS));
            if (pa_simple_write(play.pd, blk.data(),
                                blk.size() * sizeof(float), nullptr) < 0)
                break;
        }
        pa_simple_drain(play.pd, nullptr);
    });
}

static double rmsOf(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += double(x) * double(x);
    return std::sqrt(s / std::max<std::size_t>(1, v.size()));
}

static double dbfs(double r) { return 20.0 * std::log10(r + 1e-12); }

// fundamental frequency via zero crossings (mono mixdown)
static double freqOf(const std::vector<float>& v, int ch)
{
    int crossings = 0;
    double prev = 0;
    for (std::size_t i = 0; i < v.size(); i += ch) {
        const double s = double(v[i]) + double(v[i + 1]);
        if (prev < 0.0 && s >= 0.0) ++crossings;
        prev = s;
    }
    const double seconds = double(v.size() / ch) / FS;
    return crossings / 2.0 / seconds;
}

// capture nFrames from the virtual mic (blocking)
static bool captureBlock(pa_simple* rec, std::vector<float>& out, int nFrames)
{
    out.resize(std::size_t(nFrames) * CH);
    return pa_simple_read(rec, out.data(),
                          out.size() * sizeof(float), nullptr) >= 0;
}

// --------------------------------------------------------------------------
static void testServerAvailable(AudioPipeline& pipe)
{
    CHECK(pipe.ensureConnected(), "PulseAudio daemon reachable");
    const auto sources = pipe.inputSources();
    bool fakeMicSeen = false;
    for (const auto& s : sources)
        if (s.name == FAKE_MIC_MONITOR) fakeMicSeen = true;
    CHECK(fakeMicSeen, "FakeMicSink.monitor present (fake mic fixture)");
}

// 1) OBS visibility contract
static void testVirtualMicVisible(AudioPipeline& pipe)
{
    AppSettings s;
    s.sourceName = FAKE_MIC_MONITOR;
    CHECK(pipe.start(s), qPrintable(
        QStringLiteral("engine starts: %1").arg(pipe.lastError())));

    const auto sources = pipe.inputSources();
    bool seen = false;
    QString desc;
    bool monitorFlag = true;
    for (const auto& src : sources) {
        if (src.name == VIRTUAL_SRC) {
            seen = true;
            desc = src.description;
            monitorFlag = src.monitor;
        }
    }
    CHECK(seen, "virtual source listed by the server");
    CHECK(desc == QStringLiteral("VoiceCraft Studio Microphone"),
          qPrintable(QStringLiteral("description is OBS-ready: '%1'")
                         .arg(desc)));
    CHECK(!monitorFlag, "not flagged as a monitor (looks like a real mic)");

    CHECK(pipe.virtualMic().exists(), "vmic reports existence");
    CHECK(pipe.virtualMic().sourceVisibleToClients(),
          "vmic source visible to clients");

    // audit: exactly one sink module + one source module, nothing stray
    const auto audit = pipe.virtualMic().auditAndClean(false);
    CHECK(audit.sinkModules == 1 && audit.sourceModules == 1,
          qPrintable(QStringLiteral("audit clean: %1")
                         .arg(audit.report)));
}

// 2) audio actually flows and is intact
static void testAudioFlows()
{
    SimplePd play = openPlayback("FakeMicSink");
    CHECK(play.pd != nullptr, "playback client into fake mic opens");

    std::thread feeder;
    startFeeder(feeder, play);

    // capture from the virtual microphone while the engine is live
    SimplePd rec = openCapture(VIRTUAL_SRC);
    CHECK(rec.pd != nullptr, "capture client on virtual mic opens");

    std::vector<float> captured;
    captured.reserve(std::size_t(FS) * CH);
    const int chunks = FS / 480;   // ~1 s
    for (int i = 0; i < chunks; ++i) {
        std::vector<float> blk;
        if (!captureBlock(rec.pd, blk, 480)) break;
        captured.insert(captured.end(), blk.begin(), blk.end());
    }
    g_feederRun = false;
    if (feeder.joinable()) feeder.join();

    CHECK(captured.size() >= std::size_t(FS / 2) * CH, "captured 0.5 s+");

    // skip the first 200 ms (latency + gate adaptation), analyse the rest
    const std::size_t skip = std::size_t(0.2 * FS) * CH;
    const std::vector<float> body(captured.begin() + long(skip),
                                  captured.end());
    const double r = rmsOf(body);
    const double f = freqOf(body, CH);
    qInfo("  E2E: captured RMS %.1f dBFS, tone %.1f Hz (sent 220 Hz)",
          dbfs(r), f);
    CHECK(dbfs(r) > -60.0, "audio flows through the complete chain");
    CHECK(f > 180.0 && f < 260.0, "220 Hz tone arrives intact");

    pa_simple_flush(rec.pd, nullptr);
}

// 3) full stop cleans up everything
static void testStopCleansUp(AudioPipeline& pipe)
{
    pipe.stop();
    CHECK(!pipe.virtualMic().exists(), "vmic reports gone after stop");

    QThread::msleep(150);
    const auto sources = pipe.inputSources();
    for (const auto& s : sources) {
        CHECK(!s.name.startsWith("VoiceCraft"),
              qPrintable(QStringLiteral("no leftover device: %1").arg(s.name)));
    }
    const auto modules = pipe.pulse().listModules();
    for (const auto& m : modules) {
        CHECK(!m.second.first.contains("null-sink") ||
                  !m.second.second.contains("VoiceCraft"),
              qPrintable(QStringLiteral("no leftover module: #%1 %2 %3")
                             .arg(m.first).arg(m.second.first,
                                               m.second.second)));
    }
}

// 4) five start/stop cycles: no duplicates, each round fully cleaned
static void testStartStopCycles(AudioPipeline& pipe)
{
    for (int cycle = 0; cycle < 5; ++cycle) {
        AppSettings s;
        s.sourceName = FAKE_MIC_MONITOR;
        CHECK(pipe.start(s),
              qPrintable(QStringLiteral("cycle %d: start").arg(cycle)));
        CHECK(pipe.virtualMic().exists(),
              qPrintable(QStringLiteral("cycle %d: vmic present").arg(cycle)));

        const auto during = pipe.virtualMic().auditAndClean(false);
        CHECK(during.sinkModules == 1 && during.sourceModules == 1,
              qPrintable(QStringLiteral("cycle %1: exactly one module pair "
                                        "(%2)")
                             .arg(cycle).arg(during.report)));

        pipe.stop();
        QThread::msleep(120);
        const auto sources = pipe.inputSources();
        int strays = 0;
        for (const auto& src : sources)
            if (src.name.startsWith("VoiceCraft")) ++strays;
        CHECK(strays == 0,
              qPrintable(QStringLiteral("cycle %d: clean teardown")
                             .arg(cycle)));
    }
}

// 5) repair: kill the modules underneath a running engine, then repair
static void testRepair(AudioPipeline& pipe)
{
    AppSettings s;
    s.sourceName = FAKE_MIC_MONITOR;
    CHECK(pipe.start(s), "repair test: engine starts");

    // externally destroy our modules (simulates the crash/leak scenario).
    // Unload the SOURCE first: removing the sink auto-unloads a dependent
    // module-virtual-source, which would make the second unload a no-op.
    const uint32_t sinkMod = pipe.virtualMic().sinkModuleIndex();
    const uint32_t srcMod  = pipe.virtualMic().sourceModuleIndex();
    CHECK(pipe.pulse().unloadModule(srcMod), "external source module unload");
    CHECK(pipe.pulse().unloadModule(sinkMod), "external sink module unload");
    QThread::msleep(150);
    CHECK(!pipe.virtualMic().exists(), "vmic gone after external destroy");

    const QString report = pipe.repair();
    qInfo("  Repair report: %s", qPrintable(report));
    CHECK(!report.contains("Could not"), "repair reports success");
    CHECK(pipe.virtualMic().exists(), "vmic rebuilt by repair");

    const auto audit = pipe.virtualMic().auditAndClean(false);
    CHECK(audit.sinkModules == 1 && audit.sourceModules == 1,
          "repair leaves exactly one module pair");

    // audio must still flow after the repair
    SimplePd play = openPlayback("FakeMicSink");
    std::thread feeder;
    startFeeder(feeder, play);
    SimplePd rec = openCapture(VIRTUAL_SRC);
    CHECK(rec.pd != nullptr, "capture works again after repair");
    std::vector<float> captured;
    for (int i = 0; i < FS / 480; ++i) {
        std::vector<float> blk;
        if (!captureBlock(rec.pd, blk, 480)) break;
        captured.insert(captured.end(), blk.begin(), blk.end());
    }
    g_feederRun = false;
    if (feeder.joinable()) feeder.join();
    const std::size_t skip = std::size_t(0.2 * FS) * CH;
    const std::vector<float> body(captured.begin() + long(std::min(skip, captured.size())),
                                  captured.end());
    qInfo("  Post-repair RMS: %.1f dBFS", dbfs(rmsOf(body)));
    CHECK(dbfs(rmsOf(body)) > -60.0, "audio flows again after repair");

    pipe.stop();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInfo("== PulseAudio E2E test (requires running daemon + FakeMicSink) ==");

    AudioPipeline pipe;
    testServerAvailable(pipe);
    testVirtualMicVisible(pipe);
    testAudioFlows();
    pipe.stop();
    testStopCleansUp(pipe);
    testStartStopCycles(pipe);
    testRepair(pipe);

    if (g_fail == 0) {
        qInfo("PULSE E2E TESTS: ALL PASSED");
        return 0;
    }
    qCritical("PULSE E2E TESTS: %d FAILURES", g_fail);
    return 1;
}
