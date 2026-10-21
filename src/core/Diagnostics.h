#pragma once
#include <QObject>
#include <atomic>
#include <cstdint>

namespace vc {

// Live engine diagnostics, exchanged between the realtime thread and the UI
// exclusively through atomics (no locks, no signals from the audio side).
// The UI polls at ~30 Hz with a QTimer.
struct Diagnostics {
    // DSP thread CPU consumption, normalised to one core (0..1+)
    std::atomic<double>   cpuLoad{0.0};
    // ring buffer events since engine start
    std::atomic<std::uint64_t> xruns{0};       // capture underrun + output overrun
    // peak absolute sample since last UI poll (0..1)
    std::atomic<float>    inputPeak{0.0f};
    std::atomic<float>    outputPeak{0.0f};
    std::atomic<float>    inputRms{0.0f};
    std::atomic<float>    outputRms{0.0f};
    // measured round-trip contribution of the app (ms)
    std::atomic<float>    latencyMs{0.0f};
    // loudness measurements
    std::atomic<float>    momentaryLufs{-70.0f};
    std::atomic<float>    shortTermLufs{-70.0f};
    std::atomic<float>    integratedLufs{-70.0f};
    std::atomic<float>    truePeakDb{-99.0f};
    // current gain applied by loudness control (dB)
    std::atomic<float>    loudnessGainDb{0.0f};
    // gate / compression activity for UI display
    std::atomic<float>    gateGainDb{0.0f};
    std::atomic<float>    compGainDb{0.0f};
    // RNNoise activity estimate (0..1)
    std::atomic<float>    vadActivity{0.0f};

    void reset() {
        xruns.store(0);
        integratedLufs.store(-70.0f);
    }
};

} // namespace vc
