// ---------------------------------------------------------------------------
// LoudnessMeter / LoudnessControl / TruePeak tests — BS.1770-4 behaviour:
//   * full-scale 997 Hz sine  ≈ -3 dB LUFS (textbook value)
//   * -20 dBFS sine           ≈ -23 LUFS (linear in dB)
//   * silence                 → -70 floor (gated)
//   * LoudnessControl steers toward the target, bounded by max gain
//   * TruePeak of a full-scale sine is slightly above 0 dBTP
// ---------------------------------------------------------------------------
#include "../src/dsp/LoudnessMeter.h"
#include "../src/dsp/LoudnessControl.h"
#include <QCoreApplication>
#include <QDebug>
#include <cmath>
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

static std::vector<float> sine997(double amp, int frames)
{
    std::vector<float> v(frames);
    for (int i = 0; i < frames; ++i)
        v[i] = float(amp * std::sin(2.0 * M_PI * 997.0 * i / FS));
    return v;
}

// run a mono signal through the meter and return momentary LUFS at the end
static float measureMomentary(const std::vector<float>& in)
{
    LoudnessMeter m;
    m.prepare(int(FS), 1);
    const int hop = int(FS * 0.1);
    for (int off = 0; off + hop <= int(in.size()); off += hop) {
        const float* ch[1] = { in.data() + off };   // advance per hop!
        m.process(ch, hop);
    }
    return m.momentary();
}

static void testFullScaleSine()
{
    // feed 6 s (needs > 400 ms window; use plenty)
    auto in = sine997(1.0, int(6 * FS));
    const float lufs = measureMomentary(in);
    qInfo("  Full-scale 997 Hz sine: %.1f LUFS (expect ~ -3)", lufs);
    CHECK(lufs > -6.0f && lufs < 0.0f,
          "full-scale sine lands near -3 LUFS");
}

static void testLevelLinearity()
{
    const float loud = measureMomentary(sine997(0.1, int(6 * FS)));  // -20 dB
    const float ref  = measureMomentary(sine997(1.0, int(6 * FS)));
    qInfo("  Linearity: %.1f vs %.1f LUFS", loud, ref);
    CHECK(std::fabs((ref - loud) - 20.0f) < 2.0f,
          "20 dB level drop reads ~20 LU less");
}

static void testSilence()
{
    auto in = std::vector<float>(int(4 * FS), 0.0f);
    const float lufs = measureMomentary(in);
    qInfo("  Silence: %.1f LUFS", lufs);
    CHECK(lufs <= -65.0f, "silence reads at/below the -70 gate floor");
}

static void testTruePeak()
{
    LoudnessMeter m;
    m.prepare(int(FS), 1);
    auto in = sine997(1.0, int(2 * FS));
    const float* ch[1] = { in.data() };
    m.process(ch, int(in.size()));
    const float tp = m.truePeakDb();
    qInfo("  True peak of full-scale sine: %.2f dBTP", tp);
    CHECK(tp > -1.5f && tp < 3.0f, "true peak slightly above 0 dBTP");
}

static void testGatedIntegrated()
{
    // 3 s of speech-like level then 3 s of silence:
    // integrated must stay governed by the loud part (gate).
    LoudnessMeter m;
    m.prepare(int(FS), 1);
    auto loud = sine997(0.25, int(3 * FS));
    auto sil  = std::vector<float>(int(3 * FS), 0.f);
    const int hop = int(FS * 0.1);
    for (int off = 0; off + hop <= int(loud.size()); off += hop) {
        const float* c = &loud[off];
        m.process(&c, hop);
    }
    for (int off = 0; off + hop <= int(sil.size()); off += hop) {
        const float* c = &sil[off];
        m.process(&c, hop);
    }
    const float integrated = m.integrated();
    qInfo("  Gated integrated after loud+silence: %.1f LUFS", integrated);
    CHECK(integrated > -30.0f, "gating ignores the trailing silence");
}

// LoudnessControl: steers a -23 LUFS source toward -16 LUFS, capped
static void testLoudnessControl()
{
    LoudnessControl lc;
    lc.prepare(FS, 1);
    lc.setParams(true, -16.0f, 12.0f);

    // 1 s warmup at the working level so the meter settles
    const double amp = std::pow(10.0, -20.0 / 20.0);   // ~ -23 LUFS source
    std::vector<float> in(int(8 * FS));
    for (int i = 0; i < int(in.size()); ++i)
        in[i] = float(amp * std::sin(2.0 * M_PI * 997.0 * i / FS));

    float lastGain = 0.f;
    const int block = 480;
    std::vector<float> out(in.size());
    for (int off = 0; off < int(in.size()); off += block) {
        float* ch[1] = { in.data() + off };
        lc.processBlock(ch, std::min(block, int(in.size()) - off));
        lastGain = lc.appliedGainDb();
        std::copy_n(in.data() + off, std::min(block, int(in.size()) - off),
                    out.begin() + off);
    }
    qInfo("  LoudnessControl: final gain %.2f dB (expect ~ +6..7)", lastGain);
    CHECK(lastGain > 4.0f && lastGain < 9.0f,
          "gain steers -23 LUFS source toward -16 LUFS");
    CHECK(lastGain <= 12.0f, "gain within the configured cap");
    bool bad = false;
    for (float x : out)
        if (!std::isfinite(x)) bad = true;
    CHECK(!bad, "no NaN in loudness-controlled output");
    // effective loudness after gain should approach the target
    const float afterLufs = measureMomentary(out);
    qInfo("  Output loudness after control: %.1f LUFS (target -16)", afterLufs);
    CHECK(afterLufs > -22.0f && afterLufs < -10.0f,
          "controlled output lands near the target loudness");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInfo("== Loudness test suite ==");
    testFullScaleSine();
    testLevelLinearity();
    testSilence();
    testTruePeak();
    testGatedIntegrated();
    testLoudnessControl();

    if (g_fail == 0) {
        qInfo("LOUDNESS TESTS: ALL PASSED");
        return 0;
    }
    qCritical("LOUDNESS TESTS: %d FAILURES", g_fail);
    return 1;
}
