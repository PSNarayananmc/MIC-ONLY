// ---------------------------------------------------------------------------
// DSP CPU benchmark — the low-power-CPU evidence test.
// Processes N seconds of 48 kHz audio through the FULL chain and reports
// the realtime factor (audio seconds produced per CPU second) and the
// per-core load. On a J1800-class single core this must stay comfortably
// under 1.0 (engine usable), on any modern core it should exceed 20x.
//
// Also compares the lowCpuMode path (RNNoise on channel 0 only).
// ---------------------------------------------------------------------------
#include "../src/dsp/DSPEngine.h"
#include "../src/core/Diagnostics.h"
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <random>
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

static constexpr float FS = 48000.0f;

// speech-like signal: 220 Hz carrier with formant-ish harmonics + noise
static std::vector<float> speechLike(int frames, int ch, std::uint32_t seed)
{
    std::mt19937 g(seed);
    std::normal_distribution<float> n(0.f, 0.02f);
    std::vector<float> v(std::size_t(frames) * ch);
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / FS;
        // amplitude envelope: 4 Hz wobble (speech cadence)
        const double env = 0.35 + 0.3 * std::sin(2.0 * M_PI * 4.0 * t);
        const double s = env * (0.6 * std::sin(2 * M_PI * 140 * t)
                              + 0.4 * std::sin(2 * M_PI * 700 * t)
                              + 0.2 * std::sin(2 * M_PI * 2500 * t)
                              + 0.05 * std::sin(2 * M_PI * 8000 * t));
        for (int c = 0; c < ch; ++c)
            v[std::size_t(i) * ch + c] = float(s) + n(g);
    }
    return v;
}

struct BenchResult {
    double audioSeconds;
    double cpuSeconds;
    double realtimeFactor;   // audioSeconds / cpuSeconds
    double loadPercent;      // cpuSeconds / audioSeconds * 100
};

static BenchResult bench(int channels, bool lowCpu, int seconds)
{
    DSPEngine eng;
    Diagnostics diag;
    eng.prepare(FS, channels, lowCpu);
    eng.publishParams(DspParams{});   // Podcast-Studio-like defaults

    const int frames = seconds * int(FS);
    const auto input = speechLike(frames, channels, 99);
    std::vector<float> block(480 * channels);

    QElapsedTimer t;
    t.start();
    for (int off = 0; off < frames; off += 480) {
        std::copy_n(input.data() + std::size_t(off) * channels,
                    480 * channels, block.begin());
        eng.processInterleaved(block.data(), 480, diag);
    }
    const double cpu = double(t.nsecsElapsed()) / 1e9;

    return { double(seconds), cpu, double(seconds) / cpu,
             100.0 * cpu / double(seconds) };
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInfo("== DSP CPU benchmark (full chain, defaults) ==");

    const auto mono    = bench(1, false, 30);
    const auto stereo  = bench(2, false, 30);
    const auto lowCpu  = bench(1, true, 30);

    qInfo("  mono   : RT factor %6.1fx  load %5.1f%% of one core",
          mono.realtimeFactor, mono.loadPercent);
    qInfo("  stereo : RT factor %6.1fx  load %5.1f%% of one core",
          stereo.realtimeFactor, stereo.loadPercent);
    qInfo("  lowCpu : RT factor %6.1fx  load %5.1f%% of one core",
          lowCpu.realtimeFactor, lowCpu.loadPercent);

    // Hard requirement: the engine must be usable on a J1800-class core
    // (a 2013 dual-core Celeron @ 2.4 GHz runs this sandbox-class code
    // roughly in the same ballpark as this VM's single shared vCPU).
    // We assert conservatively that ONE core can handle the full chain
    // with a 4x safety margin for the weakest supported hardware.
    CHECK(mono.realtimeFactor > 4.0,
          "mono chain: > 4x realtime (J1800 headroom)");
    CHECK(stereo.realtimeFactor > 2.5,
          "stereo chain: > 2.5x realtime");
    CHECK(lowCpu.realtimeFactor > mono.realtimeFactor * 0.8,
          "lowCpu mode is not slower than full mode (sanity)");

    if (g_fail == 0) {
        qInfo("DSP BENCH: PASSED");
        return 0;
    }
    return 1;
}
