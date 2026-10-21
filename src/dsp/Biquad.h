#pragma once
#include <cmath>

namespace vc {

// Second-order IIR (TDF2 form), RBJ cookbook designs.
// One instance per channel. Coefficients are recomputed at most once per
// processing block from already-smoothed parameters (see modules below).
class Biquad {
public:
    enum class Type { HighPass, LowShelf, Peaking, HighShelf, LowPass, BandPass, Notch };

    void design(Type type, float fs, float f0, float Q, float gainDb)
    {
        if (f0 < 1.0f) f0 = 1.0f;
        if (f0 > fs * 0.495f) f0 = fs * 0.495f;
        if (Q < 0.05f) Q = 0.05f;
        m_type = type;
        m_fs = fs; m_f0 = f0; m_Q = Q; m_gainDb = gainDb;

        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * float(M_PI) * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * Q);
        float b0, b1, b2, a0, a1, a2;

        switch (type) {
        case Type::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw);      b2 = (1 + cw) / 2;
            a0 = 1 + alpha;    a1 = -2 * cw;        a2 = 1 - alpha;
            break;
        case Type::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw;         b2 = (1 - cw) / 2;
            a0 = 1 + alpha;    a1 = -2 * cw;        a2 = 1 - alpha;
            break;
        case Type::BandPass:
            b0 = alpha;        b1 = 0;              b2 = -alpha;
            a0 = 1 + alpha;    a1 = -2 * cw;        a2 = 1 - alpha;
            break;
        case Type::Notch:
            b0 = 1;            b1 = -2 * cw;        b2 = 1;
            a0 = 1 + alpha;    a1 = -2 * cw;        a2 = 1 - alpha;
            break;
        case Type::Peaking: {
            const float sq = alpha * A;
            b0 = 1 + sq;       b1 = -2 * cw;        b2 = 1 - sq;
            a0 = 1 + sq / A;   a1 = -2 * cw;        a2 = 1 - sq / A;
            break;
        }
        case Type::LowShelf: {
            const float sqA = 2 * std::sqrt(A) * alpha;
            b0 =    A * ((A + 1) - (A - 1) * cw + sqA);
            b1 =  2 * A * ((A - 1) - (A + 1) * cw);
            b2 =    A * ((A + 1) - (A - 1) * cw - sqA);
            a0 =        (A + 1) + (A - 1) * cw + sqA;
            a1 =   -2 * ((A - 1) + (A + 1) * cw);
            a2 =        (A + 1) + (A - 1) * cw - sqA;
            break;
        }
        case Type::HighShelf: {
            const float sqA = 2 * std::sqrt(A) * alpha;
            b0 =    A * ((A + 1) + (A - 1) * cw + sqA);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 =    A * ((A + 1) + (A - 1) * cw - sqA);
            a0 =        (A + 1) - (A - 1) * cw + sqA;
            a1 =    2 * ((A - 1) - (A + 1) * cw);
            a2 =        (A + 1) - (A - 1) * cw - sqA;
            break;
        }
        default:
            b0 = 1; b1 = 0; b2 = 0; a0 = 1; a1 = 0; a2 = 0;
            break;
        }
        const float inv = 1.0f / a0;
        m_b0 = b0 * inv; m_b1 = b1 * inv; m_b2 = b2 * inv;
        m_a1 = a1 * inv; m_a2 = a2 * inv;
    }

    // TDF2 transposed direct form II step
    inline float process(float x)
    {
        const float y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

    inline void processBlock(float* io, int n)
    {
        const float b0 = m_b0, b1 = m_b1, b2 = m_b2, a1 = m_a1, a2 = m_a2;
        float z1 = m_z1, z2 = m_z2;
        for (int i = 0; i < n; ++i) {
            const float x = io[i];
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            io[i] = y;
        }
        m_z1 = z1; m_z2 = z2;
    }

    void reset() { m_z1 = m_z2 = 0.0f; }
    Type type() const { return m_type; }
    float freq() const { return m_f0; }
    float q() const { return m_Q; }
    float gain() const { return m_gainDb; }

private:
    float m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    float m_z1 = 0, m_z2 = 0;
    Type m_type = Type::Peaking;
    float m_fs = 48000, m_f0 = 1000, m_Q = 1, m_gainDb = 0;
};

// One-pole parameter smoother (block-rate, ~realtime safe).
class ParamSmoother {
public:
    void setTimeConstant(float blocks) { m_a = std::exp(-1.0f / (blocks < 0.5f ? 0.5f : blocks)); }
    // returns true when the value has essentially reached the target
    bool step(float& current, float target)
    {
        current += m_a * (target - current);
        return std::fabs(target - current) < 0.002f * (std::fabs(target) + 1.0f);
    }
private:
    float m_a = 0.8f;
};

} // namespace vc
