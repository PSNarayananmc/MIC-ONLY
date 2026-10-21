#pragma once
#include <cmath>

namespace vc {

// Downward expander / noise gate with attack-hold-release envelope.
// rangeDb sets the maximum attenuation once the gate closes.
class NoiseGate {
public:
    void prepare(float sampleRate) { m_fs = sampleRate; }

    void setParams(bool enabled, float thresholdDb, float rangeDb,
                   float attackMs, float holdMs, float releaseMs)
    {
        m_enabled = enabled;
        m_thresholdDb = clamp(thresholdDb, -90.0f, 0.0f);
        m_rangeDb     = clamp(rangeDb, -72.0f, 0.0f);
        m_attackCoef  = coefFromMs(attackMs);
        m_releaseCoef = coefFromMs(releaseMs);
        m_holdSamples = int(holdMs * 0.001f * m_fs);
    }

    void reset() { m_env = 0; m_hold = 0; m_gainLin = 1.0f; }

    void processBlock(float* io, int n)
    {
        if (!m_enabled) { m_gainDb = 0.0f; return; }
        const float thrLin = std::pow(10.0f, m_thresholdDb / 20.0f);
        const float rangeLin = std::pow(10.0f, m_rangeDb / 20.0f);

        float env = m_env, gain = m_gainLin;
        int hold = m_hold;
        for (int i = 0; i < n; ++i) {
            const float absx = std::fabs(io[i]);
            // peak envelope follower
            if (absx > env) env = absx;
            else            env = m_releaseCoef * env + (1 - m_releaseCoef) * absx;

            if (env > thrLin) {
                hold = m_holdSamples;
                gain += m_attackCoef * (1.0f - gain);
            } else if (hold > 0) {
                --hold;
            } else {
                // expander glide towards range floor
                gain += 0.005f * (rangeLin - gain);
            }
            io[i] *= gain;
        }
        m_env = env; m_gainLin = gain; m_hold = hold;
        m_gainDb = 20.0f * std::log10(std::max(gain, 1e-6f));
    }

    float currentGainDb() const { return m_gainDb; }

private:
    static float clamp(float v, float lo, float hi)
    { return v < lo ? lo : (v > hi ? hi : v); }

    float coefFromMs(float ms) const
    {
        if (ms < 0.05f) ms = 0.05f;
        return std::exp(-1.0f / (ms * 0.001f * m_fs));
    }

    float m_fs = 48000.0f;
    float m_env = 0.0f, m_gainLin = 1.0f, m_gainDb = 0.0f;
    int   m_hold = 0, m_holdSamples = 0;
    float m_thresholdDb = -48, m_rangeDb = -18;
    float m_attackCoef = 0.6f, m_releaseCoef = 0.999f;
    bool  m_enabled = false;
};

} // namespace vc
