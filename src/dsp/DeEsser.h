#pragma once
#include "Biquad.h"

namespace vc {

// Split-band de-esser: 2nd-order crossover at sibilance frequency.
// The HF band runs through a fast envelope-driven gain reduction,
// the LF band passes untouched, bands are recombined in phase.
class DeEsser {
public:
    void prepare(float sampleRate) { m_fs = sampleRate; apply(); }

    void setParams(bool enabled, float freqHz, float amount)
    {
        m_enabled = enabled;
        if (freqHz < 4000) freqHz = 4000;
        if (freqHz > 9000) freqHz = 9000;
        m_freqTarget = freqHz;
        if (amount < 0) amount = 0;
        if (amount > 100) amount = 100;
        m_maxReduction = std::pow(10.0f, (-30.0f * amount / 100.0f) / 20.0f);
    }

    void reset()
    {
        m_hp1.reset(); m_hp2.reset(); m_lp1.reset(); m_lp2.reset();
        m_env = 0; m_gain = 1;
    }

    void processBlock(float* io, int n)
    {
        if (!m_enabled) return;
        m_freq += 0.12f * (m_freqTarget - m_freq);
        if (m_dirty || std::fabs(m_freq - m_freqTarget) > 0.5f) { apply(); m_dirty = false; }

        for (int i = 0; i < n; ++i) {
            const float x = io[i];
            const float hf = m_hp2.process(m_hp1.process(x));
            const float lf = m_lp2.process(m_lp1.process(x));

            // fast peak envelope on HF band
            const float absHf = std::fabs(hf);
            if (absHf > m_env) m_env += 0.35f * (absHf - m_env);   // ~1.5 ms attack
            else               m_env += 0.0008f * (absHf - m_env); // ~60 ms release

            // threshold: HF energy above -26 dBFS triggers reduction
            const float thr = 0.05f;
            float target = 1.0f;
            if (m_env > thr) {
                const float over = (m_env - thr) / thr;
                target = 1.0f - over * (1.0f - m_maxReduction);
                if (target < m_maxReduction) target = m_maxReduction;
            }
            m_gain += (target - m_gain) * (target < m_gain ? 0.4f : 0.03f);
            io[i] = lf + hf * m_gain;
        }
    }

private:
    void apply()
    {
        m_hp1.design(Biquad::Type::HighPass, m_fs, m_freq, 0.7071f, 0);
        m_hp2.design(Biquad::Type::HighPass, m_fs, m_freq, 0.7071f, 0);
        m_lp1.design(Biquad::Type::LowPass,  m_fs, m_freq, 0.7071f, 0);
        m_lp2.design(Biquad::Type::LowPass,  m_fs, m_freq, 0.7071f, 0);
        m_dirty = true;
    }

    Biquad m_hp1, m_hp2, m_lp1, m_lp2;
    ParamSmoother m_smoother;
    float m_fs = 48000.0f;
    float m_freq = 6500, m_freqTarget = 6500;
    float m_env = 0, m_gain = 1, m_maxReduction = 0.1f;
    bool  m_enabled = true, m_dirty = true;
};

} // namespace vc
