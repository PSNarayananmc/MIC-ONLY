#pragma once
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace vc {

// ---------------------------------------------------------------------------
// Parameter model. POD only: it is shared between the control thread and the
// realtime DSP thread through a seqlock (never a mutex, never an allocation).
// ---------------------------------------------------------------------------

struct EqBand {
    float freqHz;
    float gainDb;
    float q;
};

struct DspParams {
    // --- High-pass ---------------------------------------------------------
    bool  hpfEnabled = true;
    float hpfFreqHz  = 80.0f;          // 20 .. 300

    // --- Noise suppression (RNNoise) ---------------------------------------
    bool  nsEnabled  = true;
    float nsAmount   = 65.0f;          // 0 .. 100 (% suppression strength)

    // --- Noise gate / expander ----------------------------------------------
    bool  gateEnabled   = false;
    float gateThresholdDb = -48.0f;    // -80 .. -20
    float gateRangeDb     = -18.0f;    // 0 .. -60 (max reduction)
    float gateAttackMs    = 5.0f;
    float gateHoldMs      = 120.0f;
    float gateReleaseMs   = 180.0f;

    // --- Parametric EQ (voice-oriented, 6 controls) --------------------------
    bool  eqEnabled = true;
    float eqWarmGainDb     = 1.5f;     // low shelf @ 120 Hz   (-6..+6)
    float eqMudFreqHz      = 320.0f;   // 200 .. 500
    float eqMudGainDb      = -2.0f;    // -9..+6
    float eqMudQ           = 1.2f;
    float eqPresenceFreqHz = 3000.0f;  // 1500 .. 6000
    float eqPresenceGainDb = 2.0f;     // -6..+8
    float eqPresenceQ      = 1.0f;
    float eqClarityFreqHz  = 6500.0f;  // 3000 .. 12000
    float eqClarityGainDb  = 1.0f;     // -6..+8
    float eqClarityQ       = 0.9f;
    float eqAirGainDb      = 1.0f;     // high shelf @ 10 kHz  (-6..+6)

    // --- De-esser (split band) ----------------------------------------------
    bool  deessEnabled = true;
    float deessFreqHz  = 6500.0f;      // 4000 .. 9000
    float deessAmount  = 45.0f;        // 0 .. 100 (% max reduction)

    // --- Compressor -----------------------------------------------------------
    bool  compEnabled     = true;
    float compThresholdDb = -18.0f;    // -60 .. 0
    float compRatio       = 3.0f;      // 1 .. 20
    float compKneeDb      = 6.0f;      // 0 .. 24
    float compAttackMs    = 12.0f;     // 0.5 .. 100
    float compReleaseMs   = 140.0f;    // 10 .. 1000
    float compMakeupDb    = 4.0f;      // 0 .. 24

    // --- Saturation (optional harmonic warmth) ---------------------------------
    bool  satEnabled = false;
    float satDrive   = 25.0f;          // 0 .. 100

    // --- Loudness control -------------------------------------------------------
    bool  loudEnabled   = true;
    float loudTargetLufs = -16.0f;     // -24 .. -12 (podcast / OBS streaming)
    float loudMaxGainDb  = 12.0f;      // safety cap for the applied gain

    // --- True-peak limiter --------------------------------------------------------
    bool  limEnabled    = true;
    float limCeilingDb  = -1.5f;       // -12 .. 0 dBTP
    float limReleaseMs  = 80.0f;

    // --- Output trim -----------------------------------------------------------
    float outputGainDb = 0.0f;         // -24 .. +24
};

static_assert(std::is_trivially_copyable<DspParams>::value,
              "DspParams must be trivially copyable for the realtime seqlock");

// ---------------------------------------------------------------------------
// Seqlock: single-writer / many-reader transfer of a POD snapshot.
// Writer = control thread (preset switches, slider moves).
// Reader = DSP thread at block boundaries. A torn read simply returns false
// and the DSP keeps its previous snapshot for one block — no locks, no wait.
// ---------------------------------------------------------------------------
template <typename T>
class SeqlockParam {
public:
    void store(const T& v) {
        const uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_relaxed);          // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        m_value = v;
        std::atomic_thread_fence(std::memory_order_release);
        m_seq.store(s + 2, std::memory_order_relaxed);          // even: stable
    }

    bool load(T& out) const {
        const uint32_t s0 = m_seq.load(std::memory_order_acquire);
        if (s0 & 1u) return false;          // writer mid-write
        out = m_value;
        std::atomic_thread_fence(std::memory_order_acquire);
        return m_seq.load(std::memory_order_relaxed) == s0;
    }

private:
    std::atomic<uint32_t> m_seq{0};
    T m_value{};
};

} // namespace vc
