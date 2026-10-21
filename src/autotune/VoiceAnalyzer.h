#pragma once
#include "../dsp/DspParams.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace vc {

// "AUTO STUDIO VOICE" — one-shot analysis of a short spoken sample
// (user-recorded after consent). Produces conservative starting parameters.
// Fully offline: no cloud APIs, no downloads; plain DSP only.
struct VoiceAnalysisResult {
    // raw measurements
    float noiseFloorDb   = -90.0f;   // 10th percentile of windowed RMS
    float speechLoudness = -70.0f;   // 90th percentile of windowed RMS (dBFS)
    float integratedLufs = -70.0f;   // BS.1770-4 integrated
    float truePeakDb     = -99.0f;
    float lowRumbleRatio = 0.0f;     // energy < 80 Hz / total (0..1)
    float sibilanceRatio = 0.0f;     // energy 5-9 kHz / total (0..1)
    float dcOffset       = 0.0f;
    int   clippedSamples = 0;
    bool  clippingRisk   = false;
    bool  valid          = false;

    // derived recommendation
    DspParams recommended;
    QStringList notes;               // human-readable findings for the UI
};

class VoiceAnalyzer {
public:
    // samples: interleaved (ch used: 0..nch-1, downmixed internally)
    VoiceAnalysisResult analyze(const float* interleaved, qint64 frames,
                                int sampleRate, int channels);

private:
    static float percentileDb(std::vector<float>& dbs, float pct);
};

} // namespace vc
