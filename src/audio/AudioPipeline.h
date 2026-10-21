#pragma once
#include "PulseContext.h"
#include "AudioSource.h"
#include "VirtualMicrophone.h"
#include "../dsp/DSPEngine.h"
#include "../core/Diagnostics.h"
#include "../core/Settings.h"
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vc {

// Engine orchestrator:
//   capture thread (PulseAudio)  ->  capture ring  ->  DSP thread (ours)
//   ->  output ring  ->  playback thread (PulseAudio)  ->  null sink
//   ->  monitor source  ->  OBS
//
// Realtime rules enforced here: the DSP thread does no locks (only the
// seqlock latch), no allocation after prepare(), no filesystem, no UI.
class AudioPipeline : public QObject {
    Q_OBJECT
public:
    explicit AudioPipeline(QObject* parent = nullptr);
    ~AudioPipeline() override;

    bool ensureConnected();           // PulseAudio session
    PulseContext& pulse() { return m_pa; }
    Diagnostics& diagnostics() { return m_diag; }

    bool isRunning() const { return m_running; }
    QString stateText() const;

    bool start(const AppSettings& s);
    void stop(bool keepModules = false);

    // "Repair Audio Connection": audit modules, rebuild what is broken,
    // rebind streams, restart the DSP thread if dead. Never touches the
    // system default source/sink.
    QString repair();

    void publishParams(const DspParams& p) { m_engine.publishParams(p); }

    // recording tap: when set, the processed (pre-virtual-mic) stream is
    // copied to this recorder by the DSP thread. Pointer cleared on stop.
    void setRecordingTap(class Recorder* tap) { m_tap = tap; }
    void setRecordingTapEnabled(class Recorder* tap) { m_tap = tap; }

    // capture device listing for the UI
    QVector<PaSourceInfo> inputSources() { return m_pa.listSources(); }

    AudioSource& source() { return m_source; }
    VirtualMicrophone& virtualMic() { return m_vmic; }

    QString lastError() const { return m_lastError; }

signals:
    void started();
    void stopped();
    void error(QString reason);
    void devicesChanged();           // hotplug events (queued to UI)
    void sourceLost();               // capture device disappeared

private:
    bool createPlaybackStream(int channels);
    void destroyPlaybackStream();
    void dspThreadFunc(int blockSize);
    void setThreadRealtime(int priority);
    static void writeCbStatic(pa_stream* s, std::size_t nbytes, void* userdata);
    void writeCb(pa_stream* s, std::size_t nbytes);
    static void playStateCbStatic(pa_stream* s, void* userdata);
    void handlePlayFailure();
    bool verifyAndRebind(bool autoSelect);

    PulseContext m_pa;
    AudioSource m_source;
    VirtualMicrophone m_vmic;
    DSPEngine m_engine;
    Diagnostics m_diag;

    pa_stream* m_playStream = nullptr;
    RingBuffer m_outputRing;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequest{false};
    std::thread m_dspThread;
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    QString m_lastError;
    int m_channels = 1;
    int m_sampleRate = 48000;
    int m_blockSize = 480;
    bool m_lowCpu = false;
    QString m_chosenSource;
    std::vector<float> m_playScratch;   // preallocated, PA-thread only
    class Recorder* m_tap = nullptr;
};

} // namespace vc
