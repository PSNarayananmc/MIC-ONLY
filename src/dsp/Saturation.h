#pragma once
#include <cmath>
#include <algorithm>

namespace vc {

// Subtle harmonic saturation (tanh soft-clip), wet/dry by drive amount,
// followed by a DC blocker. Optional stage — off in clean presets.
class Saturation {
public:
    void prepare(float sampleRate) { m_fs = sampleRate; }

    void setParams(bool enabled, float drive)
    {
        m_enabled = enabled;
        if (drive < 0) drive = 0;
        if (drive > 100) drive = 100;
        m_driveTarget = drive / 100.0f;
    }

    void reset() { m_dcX = m_dcY = 0; }

    void processBlock(float* io, int n)
    {
        if (!m_enabled) return;
        m_wet += 0.1f * (m_driveTarget - m_wet);
        const float drive = 1.0f + 3.0f * m_wet;         // tanh input gain
        const float norm  = 1.0f / std::tanh(drive);      // peak-normalised

        float x1 = m_dcX, y1 = m_dcY;
        const float R = 0.995f;                            // DC blocker pole
        for (int i = 0; i < n; ++i) {
            const float x = io[i];
            float y = std::tanh(x * drive) * norm;
            // gentle blend keeps the effect transparent at low drive
            y = m_wet * y + (1.0f - m_wet) * x;
            // DC blocker
            const float out = y - x1 + R * y1;
            x1 = y; y1 = out;
            io[i] = out;
        }
        m_dcX = x1; m_dcY = y1;
    }

private:
    float m_fs = 48000.0f;
    float m_wet = 0, m_driveTarget = 0.25f;
    float m_dcX = 0, m_dcY = 0;
    bool  m_enabled = false;
};

} // namespace vc
