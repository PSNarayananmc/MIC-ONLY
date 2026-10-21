#pragma once
#include "Biquad.h"
#include "TruePeak.h"
#include <atomic>
#include <cmath>
#include <vector>

namespace vc {

// ITU-R BS.1770-4 loudness meter:
//  - K-weighting (high-shelf + RLB high-pass) per channel
//  - sliding 400 ms window, evaluated every 100 ms hop -> momentary
//  - sliding 3 s window                                -> short-term
//  - two-stage gated accumulation (absolute -70 / relative -10 LU)
//  - 4x oversampled true peak                          -> dBTP
class LoudnessMeter {
public:
    void prepare(int sampleRate, int channels)
    {
        m_fs = sampleRate;
        m_nch = channels > 0 ? (channels > 2 ? 2 : channels) : 1;

        m_shelf.assign(m_nch, Biquad{});
        m_rlb.assign(m_nch, Biquad{});
        for (int c = 0; c < m_nch; ++c) {
            // stage 1: +4 dB high shelf @ ~1682 Hz (K-weighting shelf)
            m_shelf[c].design(Biquad::Type::HighShelf, float(sampleRate),
                              1681.97f, 0.7072f, 3.99984f);
            // stage 2: RLB high-pass @ ~38 Hz
            m_rlb[c].design(Biquad::Type::HighPass, float(sampleRate),
                            38.13f, 0.7071f, 0.0f);
        }
        m_truePeak.prepare();

        m_win3s = int(sampleRate * 3.0);
        m_win400 = int(sampleRate * 0.4);
        m_hop = int(sampleRate * 0.1 + 0.5);
        m_ring.assign(std::size_t(m_nch) * m_win3s, 0.0f);
        m_idx = 0;
        m_count = 0;
        m_sum400.assign(m_nch, 0.0f);
        m_sum3s.assign(m_nch, 0.0f);
        m_sinceHop = 0;
        resetIntegrated();
    }

    // chans: non-interleaved channel pointers, n samples per channel
    void process(const float* const* chans, int n)
    {
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < m_nch; ++c) {
                const float v = m_rlb[c].process(m_shelf[c].process(chans[c][i]));
                const float sq = v * v;
                const std::size_t slot = std::size_t(c) * m_win3s + m_idx;
                const float old3 = m_ring[slot];
                m_sum3s[c] += sq - old3;
                m_ring[slot] = sq;
                const std::size_t slot400 =
                    (m_count >= m_win400)
                        ? std::size_t(c) * m_win3s
                          + ((m_idx + m_win3s - m_win400) % std::size_t(m_win3s))
                        : 0;
                if (m_count >= m_win400)
                    m_sum400[c] += sq - m_ring[slot400];
                else
                    m_sum400[c] += sq;
            }
            m_idx = (m_idx + 1) % std::size_t(m_win3s);
            if (m_count < m_win3s) ++m_count;
            ++m_sinceHop;

            if (m_sinceHop >= m_hop) {
                m_sinceHop = 0;
                evaluateBlock();
            }
        }

        // true peak (channel max, evaluated over the whole incoming block)
        float tp = 0.0f;
        for (int c = 0; c < m_nch; ++c) {
            const float p = m_truePeak.process(chans[c], n);
            if (p > tp) tp = p;
        }
        if (tp > 0.0f)
            m_truePeakDb.store(std::max(m_truePeakDb.load(), 20.0f * std::log10(tp)));
    }

    float momentary()  const { return m_momentary; }
    float shortTerm()  const { return m_shortTerm; }
    float integrated() const { return m_integrated; }
    float truePeakDb() const { return m_truePeakDb; }

    void resetIntegrated()
    {
        m_absSum = 0; m_absCount = 0;
        m_relSum = 0; m_relCount = 0;
        m_integrated = -70.0f;
        m_truePeakDb = -99.0f;
    }

private:
    void evaluateBlock()
    {
        // window mean-squares (G = 1.0 for mono / L / R)
        double sum400 = 0.0, sum3s = 0.0;
        const double div400 = double(std::min(m_count, std::size_t(m_win400)));
        const double div3s  = double(std::min(m_count, std::size_t(m_win3s)));
        for (int c = 0; c < m_nch; ++c) {
            sum400 += m_sum400[c] / div400;
            sum3s  += m_sum3s[c] / div3s;
        }

        const double mom = -0.691 + 10.0 * std::log10(std::max(sum400, 1e-20));
        m_momentary.store(float(mom));

        const double st = -0.691 + 10.0 * std::log10(std::max(sum3s, 1e-20));
        m_shortTerm.store(float(st));

        // BS.1770-4 gating uses 400 ms blocks with 75 % overlap, i.e. one
        // block per 100 ms hop — exactly this evaluation.
        if (mom > -70.0) {
            m_absSum += sum400;
            ++m_absCount;
            const double absMean = m_absSum / double(std::max(1, m_absCount));
            const double relThr =
                -0.691 + 10.0 * std::log10(std::max(absMean, 1e-20)) - 10.0;
            if (mom > relThr) {
                m_relSum += sum400;
                ++m_relCount;
                const double gatedMean = m_relSum / double(std::max(1, m_relCount));
                m_integrated.store(float(-0.691 + 10.0 * std::log10(std::max(gatedMean, 1e-20))));
            }
        }
    }

    std::vector<Biquad> m_shelf, m_rlb;
    TruePeakEstimator m_truePeak;
    std::vector<float> m_ring;       // per-channel ring of K-weighted squares (3 s)
    std::vector<float> m_sum400, m_sum3s;
    std::size_t m_idx = 0, m_count = 0;
    int m_fs = 48000, m_nch = 1;
    int m_win3s = 0, m_win400 = 0;
    int m_hop = 4800;                // 100 ms @ 48 kHz, adjusted in prepare
    int m_sinceHop = 0;

    double m_absSum = 0; int m_absCount = 0;
    double m_relSum = 0; int m_relCount = 0;

    std::atomic<float> m_momentary{-70.0f}, m_shortTerm{-70.0f};
    std::atomic<float> m_integrated{-70.0f}, m_truePeakDb{-99.0f};
};

} // namespace vc
