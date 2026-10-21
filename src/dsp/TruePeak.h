#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <cstring>

namespace vc {

// 4x oversampling polyphase FIR for true-peak estimation (BS.1770-4 style).
// Windowed-sinc lowpass, cutoff ~0.45 of the original Nyquist.
class TruePeakEstimator {
public:
    static constexpr int FACTOR = 4;
    static constexpr int TAPS_PER_PHASE = 16;

    void prepare()
    {
        // Fractional-delay polyphase design (textbook windowed sinc):
        // phase p interpolates at delay d = p/FACTOR samples around a
        // symmetric centre. Blackman window keeps the stopband deep so
        // inter-sample peaks are not underestimated; each phase is then
        // normalised to DC gain 1.
        for (int p = 0; p < FACTOR; ++p) {
            const double d = double(p) / double(FACTOR);
            const double centre = (TAPS_PER_PHASE - 1) / 2.0;
            double sum = 0.0;
            for (int k = 0; k < TAPS_PER_PHASE; ++k) {
                const double x = (double(k) - centre) - d;
                const double sinc = (std::fabs(x) < 1e-12)
                    ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
                const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * k
                                       / (TAPS_PER_PHASE - 1))
                               + 0.08 * std::cos(4.0 * M_PI * k
                                       / (TAPS_PER_PHASE - 1));
                m_phase[p][k] = static_cast<float>(sinc * w);
                sum += m_phase[p][k];
            }
            if (std::fabs(sum) > 1e-9)
                for (int k = 0; k < TAPS_PER_PHASE; ++k)
                    m_phase[p][k] = static_cast<float>(m_phase[p][k] / sum);
        }
    }

    // Returns interpolated |true peak| max over the block; state kept across
    // calls so consecutive blocks interpolate seamlessly.
    float process(const float* x, int n)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            push(x[i]);
            for (int p = 0; p < FACTOR; ++p) {
                float acc = 0.0f;
                const auto& taps = m_phase[p];
                for (int k = 0; k < TAPS_PER_PHASE; ++k)
                    acc += taps[k] * m_hist[(m_histIdx - k) & HIST_MASK];
                const float v = std::fabs(acc);
                if (v > peak) peak = v;
            }
        }
        return peak;
    }

private:
    static constexpr int HIST_BITS = 6; // 64 entries
    static constexpr int HIST_MASK = (1 << HIST_BITS) - 1;

    void push(float v)
    {
        m_hist[m_histIdx] = v;
        m_histIdx = (m_histIdx + 1) & HIST_MASK;
    }

    std::array<std::array<float, TAPS_PER_PHASE>, FACTOR> m_phase{};
    std::array<float, 1 << HIST_BITS> m_hist{};
    int m_histIdx = 0;
};

} // namespace vc
