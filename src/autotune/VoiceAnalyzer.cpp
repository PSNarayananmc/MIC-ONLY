#include "VoiceAnalyzer.h"
#include "../dsp/LoudnessMeter.h"
#include "../dsp/Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vc {

float VoiceAnalyzer::percentileDb(std::vector<float>& dbs, float pct)
{
    if (dbs.empty())
        return -90.0f;
    std::sort(dbs.begin(), dbs.end());
    const std::size_t idx = std::min(dbs.size() - 1,
        std::size_t(pct / 100.0f * float(dbs.size())));
    return dbs[idx];
}

VoiceAnalysisResult VoiceAnalyzer::analyze(const float* interleaved,
                                           qint64 frames,
                                           int sampleRate, int channels)
{
    VoiceAnalysisResult r;
    if (!interleaved || frames < sampleRate / 2)   // need at least ~0.5 s
        return r;

    // downmix to mono
    std::vector<float> mono{std::size_t(frames), 0.0f};
    double dc = 0.0;
    for (qint64 i = 0; i < frames; ++i) {
        float v = 0.0f;
        for (int c = 0; c < channels; ++c)
            v += interleaved[i * channels + c];
        v /= float(channels);
        mono[std::size_t(i)] = v;
        dc += v;
    }
    r.dcOffset = float(dc / double(frames));

    // windowed RMS (50 ms hop / 50 ms window)
    const int win = sampleRate / 20;
    std::vector<float> windowDb;
    windowDb.reserve(std::size_t(frames / win) + 1);
    for (qint64 off = 0; off + win <= frames; off += win) {
        double sq = 0.0;
        for (int i = 0; i < win; ++i) {
            const float v = mono[std::size_t(off + i)];
            sq += v * v;
        }
        const float rms = float(std::sqrt(sq / double(win)));
        windowDb.push_back(20.0f * std::log10(std::max(rms, 1e-7f)));
    }
    if (windowDb.size() < 4)
        return r;

    r.noiseFloorDb = percentileDb(windowDb, 10.0f);
    r.speechLoudness = percentileDb(windowDb, 90.0f);

    // --- spectral ratios via one-pole-split bands (cheap, offline OK) ----
    Biquad lpRumble, hpRumble, bpSib;
    lpRumble.design(Biquad::Type::LowPass,  float(sampleRate), 80.0f, 0.7071f, 0);
    hpRumble.design(Biquad::Type::HighPass, float(sampleRate), 80.0f, 0.7071f, 0);
    bpSib.design(Biquad::Type::BandPass,    float(sampleRate), 7000.0f, 0.7f, 0);

    double total = 1e-12, low = 0.0, sib = 0.0;
    int clipped = 0, above = 0;
    for (qint64 i = 0; i < frames; ++i) {
        const float x = mono[std::size_t(i)];
        const float xl = lpRumble.process(x);
        const float xh = hpRumble.process(x);
        const float xs = bpSib.process(x);
        low += xl * xl;
        total += xh * xh;
        sib += xs * xs;
        if (std::fabs(x) > 0.995f) {
            ++clipped;
            if (++above > sampleRate / 200)
                r.clippingRisk = true;
        } else if (above > 0) {
            above = 0;
        }
    }
    r.clippedSamples = clipped;
    r.lowRumbleRatio = float(low / total);
    r.sibilanceRatio = float(sib / total);
    r.truePeakDb = 20.0f * std::log10(float(std::sqrt(total / double(frames))) * 2.0f + 1e-7f);

    // --- BS.1770-4 integrated loudness of the sample ----------------------
    LoudnessMeter meter;
    meter.prepare(sampleRate, 1);
    const float* chPtr = mono.data();
    meter.process(&chPtr, int(frames));
    r.integratedLufs = meter.integrated();

    // ---------------- conservative recommendation -------------------------
    DspParams p;   // Podcast-Studio-like baseline
    p.hpfEnabled = true;
    // rumble-driven high-pass: 65..110 Hz
    p.hpfFreqHz = 65.0f + 45.0f * std::min(1.0f, r.lowRumbleRatio * 8.0f);

    // noise suppression strength from measured floor
    if (r.noiseFloorDb > -40.0f)      p.nsAmount = 75.0f;
    else if (r.noiseFloorDb > -50.0f) p.nsAmount = 60.0f;
    else                              p.nsAmount = 45.0f;
    p.nsEnabled = r.noiseFloorDb > -58.0f;

    // gate only for genuinely noisy rooms
    p.gateEnabled = r.noiseFloorDb > -42.0f;
    p.gateThresholdDb = std::max(-60.0f, r.noiseFloorDb + 9.0f);

    // dynamics: louder voices need less compression
    const float speechRange = r.speechLoudness - r.noiseFloorDb;
    if (speechRange > 30.0f) {
        p.compThresholdDb = -20.0f; p.compRatio = 4.0f; p.compMakeupDb = 5.0f;
    } else if (speechRange > 20.0f) {
        p.compThresholdDb = -18.0f; p.compRatio = 3.0f; p.compMakeupDb = 4.0f;
    } else {
        p.compThresholdDb = -15.0f; p.compRatio = 2.0f; p.compMakeupDb = 2.0f;
    }

    // de-esser driven by measured sibilance share
    p.deessEnabled = r.sibilanceRatio > 0.006f;
    p.deessAmount = std::min(70.0f, 25.0f + r.sibilanceRatio * 4000.0f);

    // loudness: conservative targets
    p.loudEnabled = true;
    p.loudTargetLufs = -16.0f;
    p.loudMaxGainDb = r.integratedLufs < -30.0f ? 8.0f : 12.0f;

    p.limEnabled = true;
    p.limCeilingDb = r.clippingRisk ? -3.0f : -1.5f;
    p.outputGainDb = 0.0f;
    r.recommended = p;

    // ---------------- human-readable notes --------------------------------
    if (r.clippingRisk)
        r.notes << QStringLiteral("Input is clipping — lower the mic gain; "
                                  "limiter ceiling set to -3 dBTP for safety.");
    else
        r.notes << QStringLiteral("No clipping detected.");
    r.notes << QStringLiteral("Noise floor: %1 dBFS, speech level: %2 dBFS.")
               .arg(r.noiseFloorDb, 0, 'f', 1).arg(r.speechLoudness, 0, 'f', 1);
    if (r.lowRumbleRatio > 0.12f)
        r.notes << QStringLiteral("Significant low-frequency rumble — "
                                  "high-pass set to %1 Hz.").arg(p.hpfFreqHz, 0, 'f', 0);
    else
        r.notes << QStringLiteral("Low-frequency content is clean; gentle "
                                  "high-pass at %1 Hz.").arg(p.hpfFreqHz, 0, 'f', 0);
    if (r.sibilanceRatio > 0.02f)
        r.notes << QStringLiteral("Strong sibilance detected — de-esser at %1%.")
                   .arg(p.deessAmount, 0, 'f', 0);
    r.notes << QStringLiteral("Sample loudness: %1 LUFS → target -16 LUFS "
                              "(podcast standard).").arg(r.integratedLufs, 0, 'f', 1);
    r.notes << QStringLiteral("Starting parameters are conservative. "
                              "Fine-tune in the Mixer tab while listening.");
    r.valid = true;
    return r;
}

} // namespace vc
