#pragma once
#include <cmath>

namespace vc {

// Feed-forward compressor with soft knee, smooth peak detection,
// attack / release / makeup. Gain computer runs per sample on the
// rectified signal; gain smoothing uses branch-coefficient smoothing.
class Compressor {
public:
    void prepare(float sampleRate) { m_fs = sampleRate; }

    void setParams(bool enabled, float thresholdDb, float ratio, float kneeDb,
                   float attackMs, float releaseMs, float makeupDb)
    {
        m_enabled = enabled;
        m_thresholdDb = clamp(thresholdDb, -60.0f, 0.0f);
        m_ratio = clamp(ratio, 1.0f, 20.0f);
        m_kneeDb = clamp(kneeDb, 0.0f, 24.0f);
        m_attackMs = clamp(attackMs, 0.5f, 100.0f);
        m_releaseMs = clamp(releaseMs, 10.0f, 1000.0f);
        m_makeupDb = clamp(makeupDb, 0.0f, 24.0f);
    }

    void reset() { m_env = 0; m_gainDb = 0; }

    void processBlock(float* io, int n)
    {
        if (!m_enabled) { m_gainDb = 0; return; }
        const float atkCoef = std::exp(-1.0f / (m_attackMs * 0.001f * m_fs));
        const float relCoef = std::exp(-1.0f / (m_releaseMs * 0.001f * m_fs));
        const float makeup = std::pow(10.0f, m_makeupDb / 20.0f);

        float env = m_env, gainDb = m_gainDb;
        for (int i = 0; i < n; ++i) {
            const float absx = std::fabs(io[i]);
            if (absx > env) env = atkCoef * env + (1 - atkCoef) * absx;
            else            env = relCoef * env + (1 - relCoef) * absx;

            const float envDb = 20.0f * std::log10(std::max(env, 1e-7f));
            const float target = computeGainDb(envDb);
            // ~5 ms gain smoothing at block/sample level
            gainDb += 0.06f * (target - gainDb);
            io[i] *= std::pow(10.0f, gainDb / 20.0f) * makeup;
        }
        m_env = env; m_gainDb = gainDb;
        m_grDb = -gainDb;
    }

    float currentGainReductionDb() const { return m_grDb; }

private:
    static float clamp(float v, float lo, float hi)
    { return v < lo ? lo : (v > hi ? hi : v); }

    // soft-knee gain computer (classic quadratic knee):
    //   x < T - W/2            -> y = x
    //   |x - T| <= W/2         -> y = x + (1/R - 1)(x - T + W/2)^2 / (2W)
    //   x > T + W/2            -> y = T + (x - T)/R
    float computeGainDb(float levelDb) const
    {
        const float w = m_kneeDb;
        const float lo = m_thresholdDb - w * 0.5f;
        const float hi = m_thresholdDb + w * 0.5f;
        if (levelDb <= lo)
            return 0.0f;
        if (levelDb >= hi)
            return m_thresholdDb + (levelDb - m_thresholdDb) / m_ratio - levelDb;
        const float slewed = levelDb + (1.0f / m_ratio - 1.0f)
                             * std::pow(levelDb - lo, 2.0f) / (2.0f * w);
        return slewed - levelDb;
    }

    float m_fs = 48000.0f;
    float m_env = 0, m_gainDb = 0, m_grDb = 0;
    float m_thresholdDb = -18, m_ratio = 3, m_kneeDb = 6;
    float m_attackMs = 12, m_releaseMs = 140, m_makeupDb = 0;
    bool  m_enabled = true;
};

} // namespace vc
