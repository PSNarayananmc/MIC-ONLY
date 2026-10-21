#pragma once
#include "Biquad.h"

namespace vc {

// Voice-oriented 5-filter parametric EQ:
//   low shelf @120 Hz   -> warmth
//   peaking  @mud       -> mud reduction (200-500 Hz)
//   peaking  @presence  -> presence / intelligibility (1.5-6 kHz)
//   peaking  @clarity   -> clarity / articulation (3-12 kHz)
//   high shelf @10 kHz  -> air / brightness
class ParametricEQ {
public:
    void prepare(float sampleRate) { m_fs = sampleRate; applyAll(); }

    struct Params {
        bool  enabled = true;
        float warmGainDb = 0;
        float mudFreqHz = 320, mudGainDb = 0, mudQ = 1.2f;
        float presenceFreqHz = 3000, presenceGainDb = 0, presenceQ = 1.0f;
        float clarityFreqHz = 6500, clarityGainDb = 0, clarityQ = 0.9f;
        float airGainDb = 0;
    };

    void setParams(const Params& p) { m_target = p; }

    void reset() { for (auto& f : m_bands) f.reset(); }

    void processBlock(float* io, int n)
    {
        if (!m_target.enabled && m_current.enabled) {
            // keep running with zero-gain bands to avoid clicks while disabling
            Params flat; flat.enabled = true;
            m_current = flat; applyAll();
        }
        if (!m_target.enabled) return;

        // smooth gains/frequencies at block rate (one-pole ~40 ms)
        m_current.warmGainDb     += 0.12f * (m_target.warmGainDb - m_current.warmGainDb);
        m_current.mudFreqHz      += 0.12f * (m_target.mudFreqHz - m_current.mudFreqHz);
        m_current.mudGainDb      += 0.12f * (m_target.mudGainDb - m_current.mudGainDb);
        m_current.presenceFreqHz += 0.12f * (m_target.presenceFreqHz - m_current.presenceFreqHz);
        m_current.presenceGainDb += 0.12f * (m_target.presenceGainDb - m_current.presenceGainDb);
        m_current.clarityFreqHz  += 0.12f * (m_target.clarityFreqHz - m_current.clarityFreqHz);
        m_current.clarityGainDb  += 0.12f * (m_target.clarityGainDb - m_current.clarityGainDb);
        m_current.airGainDb      += 0.12f * (m_target.airGainDb - m_current.airGainDb);
        applyAll();

        for (auto& f : m_bands) f.processBlock(io, n);
    }

private:
    void applyAll()
    {
        m_bands[0].design(Biquad::Type::LowShelf,  m_fs, 120.0f, 0.7071f, m_current.warmGainDb);
        m_bands[1].design(Biquad::Type::Peaking,   m_fs, m_current.mudFreqHz, m_current.mudQ, m_current.mudGainDb);
        m_bands[2].design(Biquad::Type::Peaking,   m_fs, m_current.presenceFreqHz, m_current.presenceQ, m_current.presenceGainDb);
        m_bands[3].design(Biquad::Type::Peaking,   m_fs, m_current.clarityFreqHz, m_current.clarityQ, m_current.clarityGainDb);
        m_bands[4].design(Biquad::Type::HighShelf, m_fs, 10000.0f, 0.7071f, m_current.airGainDb);
    }

    Biquad m_bands[5];
    Params m_current, m_target;
    float m_fs = 48000.0f;
};

} // namespace vc
