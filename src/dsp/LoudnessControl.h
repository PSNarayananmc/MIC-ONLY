#pragma once
#include "LoudnessMeter.h"
#include <cmath>

namespace vc {

// Broadcast-style loudness control: measures the short-term LUFS of the
// programme and trims the gain towards the user target. The applied gain is
// heavily smoothed (~0.8 s) and hard-capped so quiet rooms never explode.
class LoudnessControl {
public:
    void prepare(float sampleRate, int channels)
    {
        m_meter.prepare(int(sampleRate), channels);
        m_nch = channels > 0 ? (channels > 2 ? 2 : channels) : 1;
        m_holdCounter = 0;
        m_gainDb = 0.0f;
    }

    void setParams(bool enabled, float targetLufs, float maxGainDb)
    {
        m_enabled = enabled;
        if (targetLufs < -30.0f) targetLufs = -30.0f;
        if (targetLufs > -8.0f) targetLufs = -8.0f;
        m_targetLufs = targetLufs;
        if (maxGainDb < 0.0f) maxGainDb = 0.0f;
        if (maxGainDb > 18.0f) maxGainDb = 18.0f;
        m_maxGainDb = maxGainDb;
    }

    // measures only (no gain change) — used by the UI/analysis paths
    void measure(const float* const* chans, int n) { m_meter.process(chans, n); }

    void reset() { m_gainDb = 0.0f; m_holdCounter = 0; m_meter.resetIntegrated(); }

    void processBlock(float* const* chans, int n)
    {
        // measure the input
        m_meter.process(chans, n);

        if (!m_enabled) { m_gainDb = 0.0f; return; }

        const float st = m_meter.shortTerm();
        float targetGain = m_gainDb;
        if (st < -60.0f) {
            // programme silence: freeze gain (avoid wind-up), decay home after ~3 s
            if (++m_holdCounter > 300)
                targetGain = 0.0f;
        } else {
            m_holdCounter = 0;
            targetGain = m_targetLufs - st;
            if (targetGain > m_maxGainDb) targetGain = m_maxGainDb;
            if (targetGain < -m_maxGainDb) targetGain = -m_maxGainDb;
        }
        // ~0.8 s one-pole towards target gain
        m_gainDb += 0.008f * (targetGain - m_gainDb);

        const float g = std::pow(10.0f, m_gainDb / 20.0f);
        for (int c = 0; c < m_nch; ++c)
            for (int i = 0; i < n; ++i)
                chans[c][i] *= g;
    }

    LoudnessMeter& meter() { return m_meter; }
    float appliedGainDb() const { return m_gainDb; }

private:
    LoudnessMeter m_meter;
    float m_targetLufs = -16.0f, m_maxGainDb = 12.0f, m_gainDb = 0.0f;
    int m_holdCounter = 0;
    int m_nch = 1;
    bool m_enabled = true;
};

} // namespace vc
