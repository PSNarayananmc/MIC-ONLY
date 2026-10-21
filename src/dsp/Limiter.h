#pragma once
#include "TruePeak.h"
#include <cmath>
#include <vector>
#include <deque>

namespace vc {

// Feed-forward look-ahead true-peak limiter.
// Signal runs through a 5 ms delay; the required gain is computed from the
// 4x-oversampled (true-peak) signal in the lookahead window (sliding min via
// monotonic deque), then a smoothed release glides the gain back up.
class Limiter {
public:
    void prepare(float sampleRate, int channels)
    {
        m_fs = sampleRate;
        m_nch = channels > 0 ? (channels > 2 ? 2 : channels) : 1;
        m_lookahead = int(sampleRate * 0.005f); // 5 ms
        m_est.prepare();
        m_delay.assign(std::size_t(m_nch) * m_lookahead, 0.0f);
        m_delayIdx = 0;
        m_gain = 1.0f;
        m_releaseCoef = std::exp(-1.0f / (0.080f * sampleRate));
    }

    void setParams(bool enabled, float ceilingDbtp, float releaseMs)
    {
        m_enabled = enabled;
        if (ceilingDbtp > 0.0f) ceilingDbtp = 0.0f;
        if (ceilingDbtp < -12.0f) ceilingDbtp = -12.0f;
        m_ceilingLin = std::pow(10.0f, ceilingDbtp / 20.0f);
        if (releaseMs < 10.0f) releaseMs = 10.0f;
        if (releaseMs > 500.0f) releaseMs = 500.0f;
        m_releaseCoef = std::exp(-1.0f / (releaseMs * 0.001f * m_fs));
    }

    void reset()
    {
        std::fill(m_delay.begin(), m_delay.end(), 0.0f);
        m_deque.clear();
        m_gain = 1.0f;
    }

    // chans: non-interleaved, in-place
    void processBlock(float* const* chans, int n)
    {
        if (!m_enabled) return;

        // 1) per-sample required gain from true-peak estimate (in -> deque)
        for (int i = 0; i < n; ++i) {
            float peak = 0.0f;
            for (int c = 0; c < m_nch; ++c) {
                const float p = m_est.process(&chans[c][i], 1);
                if (p > peak) peak = p;
            }
            const float req = peak > 1e-6f
                ? std::min(m_ceilingLin / peak, 1.0f)   // a limiter only
                : 1.0f;                                 // attenuates, never boosts
            pushRequired(req);
        }

        // 2) advance through the block: delayed audio * sliding-min gain
        for (int i = 0; i < n; ++i) {
            const float g = popRequired();            // gain for sample leaving delay
            // release smoothing (attack is instantaneous through the min window)
            m_gain = g < m_gain ? g : m_releaseCoef * m_gain + (1 - m_releaseCoef) * g;

            for (int c = 0; c < m_nch; ++c) {
                const float delayed = m_delay[std::size_t(c) * m_lookahead + m_delayIdx];
                m_delay[std::size_t(c) * m_lookahead + m_delayIdx] = chans[c][i];
                chans[c][i] = delayed * m_gain;
            }
            m_delayIdx = (m_delayIdx + 1) % m_lookahead;
        }
    }

    float currentGainLin() const { return m_gain; }

private:
    // monotonic deque for sliding-window minimum over the lookahead
    void pushRequired(float g)
    {
        while (!m_deque.empty() && m_deque.back().value >= g)
            m_deque.pop_back();
        m_deque.push_back({g, m_pushCount});
        ++m_pushCount;
        // drop expired entries lazily on pop
    }

    float popRequired()
    {
        while (!m_deque.empty() &&
               m_pushCount - m_deque.front().age > std::size_t(m_lookahead))
            m_deque.pop_front();
        return m_deque.empty() ? 1.0f : m_deque.front().value;
    }

    struct GainNode { float value; std::size_t age; };

    TruePeakEstimator m_est;
    std::vector<float> m_delay;
    std::deque<GainNode> m_deque;
    std::size_t m_pushCount = 0;
    std::size_t m_delayIdx = 0;
    float m_fs = 48000.0f, m_ceilingLin = 0.84139f, m_gain = 1.0f, m_releaseCoef = 0.999f;
    int m_nch = 1, m_lookahead = 240;
    bool m_enabled = true;
};

} // namespace vc
