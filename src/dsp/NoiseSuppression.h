#pragma once
#include <rnnoise.h>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace vc {

// RNNoise wrapper (48 kHz mono, 480-sample frames internally).
// nsAmount (0..100) blends between original and fully denoised signal so the
// effect can be dialled back to avoid processed-artifacts.
class NoiseSuppression {
public:
    ~NoiseSuppression() { destroyState(); }

    void prepare(int sampleRate)
    {
        // RNNoise 0.2 operates on 48 kHz / 480-frame blocks.
        m_fsOk = (sampleRate == 48000);
        if (m_fsOk && !m_state)
            m_state = rnnoise_create(nullptr);
        if (!m_fsOk && m_state)
            destroyState();
    }

    void setParams(bool enabled, float amount)
    {
        m_enabled = enabled;
        if (amount < 0) amount = 0;
        if (amount > 100) amount = 100;
        m_amountTarget = amount / 100.0f;
    }

    void reset()
    {
        if (m_state) {
            destroyState();
            m_state = rnnoise_create(nullptr);
        }
    }

    bool active() const { return m_enabled && m_fsOk && m_state; }

    // io: block of n samples (n should be a multiple of 480 for best results;
    // smaller trailing partial blocks are processed by internal buffering).
    void processBlock(float* io, int n)
    {
        if (!active()) return;

        // smooth the blend factor
        m_amount += 0.15f * (m_amountTarget - m_amount);

        int idx = 0;
        while (idx < n) {
            const int remain = n - idx;
            if (m_bufFill == 0 && remain >= RN_FRAME) {
                processFrame(io + idx);
                idx += RN_FRAME;
            } else {
                const int take = std::min(RN_FRAME - m_bufFill, remain);
                std::memcpy(m_frame + m_bufFill, io + idx, take * sizeof(float));
                m_bufFill += take;
                idx += take;
                if (m_bufFill == RN_FRAME) {
                    processFrame(m_frame);
                    std::memcpy(io + idx - RN_FRAME, m_frame, RN_FRAME * sizeof(float));
                    m_bufFill = 0;
                }
            }
        }
    }

    // rough voice-activity estimate for the UI (0..1)
    float vad() const { return m_vad; }

private:
    static constexpr int RN_FRAME = 480;

    void processFrame(float* x)
    {
        float copy[RN_FRAME];
        if (m_amount < 0.999f)
            std::memcpy(copy, x, RN_FRAME * sizeof(float));

        // RNNoise (0.1 and 0.2 alike) expects samples at raw int16 scale
        // (±32768), exactly as its own demo feeds shorts. Normalised floats
        // look like digital silence to the network and bypass everything.
        // Scale in / out here so the rest of the engine stays in [-1, 1].
        for (int i = 0; i < RN_FRAME; ++i)
            x[i] *= 32768.0f;
        rnnoise_process_frame(m_state, x, x);
        for (int i = 0; i < RN_FRAME; ++i)
            x[i] *= 1.0f / 32768.0f;

        // VAD proxy: energy the network kept (speech) vs. suppressed
        float denE = 0, inE = 1e-9f;
        for (int i = 0; i < RN_FRAME; ++i) {
            denE += x[i] * x[i];
            inE  += copy[i] * copy[i];
        }
        const float ratio = std::sqrt(denE / inE);
        m_vad = m_vad * 0.8f + std::min(1.0f, ratio * 1.4f) * 0.2f;

        if (m_amount < 0.999f) {
            for (int i = 0; i < RN_FRAME; ++i)
                x[i] = m_amount * x[i] + (1.0f - m_amount) * copy[i];
        }
    }

    void destroyState()
    {
        if (m_state) {
            rnnoise_destroy(m_state);
            m_state = nullptr;
        }
    }

    DenoiseState* m_state = nullptr;
    float m_frame[RN_FRAME] = {};
    int   m_bufFill = 0;
    float m_amount = 0.65f, m_amountTarget = 0.65f;
    float m_vad = 0.0f;
    bool  m_enabled = true;
    bool  m_fsOk = false;
};

} // namespace vc
