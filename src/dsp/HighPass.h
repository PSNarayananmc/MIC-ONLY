#pragma once
#include "Biquad.h"

namespace vc {

// 2nd-order Butterworth high-pass (voice rumble / plosive control).
class HighPass {
public:
    void prepare(float sampleRate)
    {
        m_fs = sampleRate;
        apply(m_freq);
    }

    void setParams(bool enabled, float freqHz)
    {
        m_enabled = enabled;
        if (freqHz < 20.0f) freqHz = 20.0f;
        if (freqHz > 300.0f) freqHz = 300.0f;
        // smooth cutoff at block rate (~30 ms to converge) to avoid zipper noise
        m_freqTarget = freqHz;
    }

    void reset() { m_hp.reset(); }

    void processBlock(float* io, int n)
    {
        if (!m_enabled) return;
        m_smoother.step(m_freq, m_freqTarget);
        if (m_dirty || std::fabs(m_freq - m_freqTarget) > 0.01f) {
            apply(m_freq);
            m_dirty = false;
        }
        m_hp.processBlock(io, n);
    }

    float freq() const { return m_freq; }

private:
    void apply(float f)
    {
        m_hp.design(Biquad::Type::HighPass, m_fs, f, 0.7071f, 0.0f);
        m_dirty = true;
    }
    Biquad m_hp;
    ParamSmoother m_smoother;
    float m_fs = 48000.0f;
    float m_freq = 80.0f, m_freqTarget = 80.0f;
    bool m_enabled = true;
    bool m_dirty = true;
};

} // namespace vc
