#include "AudioPipeline.h"
#include "../core/Log.h"
#include "../recorder/Recorder.h"
#include <QDeadlineTimer>
#include <pthread.h>
#include <cmath>
#ifdef __SSE2__
#include <xmmintrin.h>
#endif

namespace vc {

AudioPipeline::AudioPipeline(QObject* parent)
    : QObject(parent)
    , m_source(m_pa)
    , m_vmic(m_pa)
{
    connect(&m_pa, &PulseContext::deviceListChanged, this,
            [this]() { emit devicesChanged(); }, Qt::QueuedConnection);
}

AudioPipeline::~AudioPipeline()
{
    stop();
}

bool AudioPipeline::ensureConnected()
{
    if (m_pa.isConnected())
        return true;
    if (!m_pa.connectToServer()) {
        m_lastError = m_pa.lastError();
        emit error(m_lastError);
        return false;
    }
    return true;
}

QString AudioPipeline::stateText() const
{
    if (!m_pa.isConnected())
        return QStringLiteral("PulseAudio: not connected");
    if (!m_running)
        return QStringLiteral("Stopped");
    return QStringLiteral("Running — %1 ch @ %2 Hz")
        .arg(m_channels).arg(m_sampleRate);
}

bool AudioPipeline::start(const AppSettings& s)
{
    if (m_running)
        stop();

    if (!ensureConnected())
        return false;

    m_sampleRate = s.sampleRate > 0 ? s.sampleRate : 48000;
    m_blockSize = s.blockSize > 0 ? s.blockSize : 480;
    m_lowCpu = s.lowCpuMode;

    // --- 1) real microphone capture -----------------------------------
    // Determine channel count from the chosen source (fallback default).
    const QVector<PaSourceInfo> sources = m_pa.listSources();
    QString chosen = s.sourceName;
    int channels = 1;
    bool found = false;
    for (const auto& src : sources) {
        if (src.name == chosen) {
            channels = src.channels > 2 ? 2 : (src.channels < 1 ? 1 : src.channels);
            found = true;
            break;
        }
    }
    if (!found) {
        // fall back to the server default source, never to a monitor first
        for (const auto& src : sources) {
            if (src.name.endsWith(QStringLiteral(".monitor")))
                continue;
            chosen = src.name;
            channels = src.channels > 2 ? 2 : (src.channels < 1 ? 1 : src.channels);
            found = true;
            break;
        }
    }
    if (!found) {
        m_lastError = QStringLiteral("No input device found. "
            "Check that a microphone is connected and not suspended.");
        emit error(m_lastError);
        return false;
    }
    m_channels = channels;
    m_chosenSource = chosen;

    m_source.setRingCapacity(std::size_t(m_blockSize) * 8);
    m_source.setLevelSink(&m_diag.inputPeak, &m_diag.inputRms, &m_diag.xruns);
    if (!m_source.start(chosen, m_sampleRate, m_channels)) {
        m_lastError = QStringLiteral("Failed to open capture stream from '%1'").arg(chosen);
        emit error(m_lastError);
        return false;
    }

    // --- 2) virtual microphone ------------------------------------------
    if (!m_vmic.create(m_channels, m_sampleRate)) {
        m_lastError = QStringLiteral("Failed to create the virtual microphone: %1")
                          .arg(m_pa.lastError());
        m_source.stop();
        emit error(m_lastError);
        return false;
    }

    // --- 3) playback stream into our own sink -----------------------------
    m_outputRing.reset(std::size_t(m_blockSize) * 16);
    if (!createPlaybackStream(m_channels)) {
        m_lastError = QStringLiteral("Failed to open the virtual microphone stream: %1")
                          .arg(m_pa.lastError());
        m_vmic.destroy();
        m_source.stop();
        emit error(m_lastError);
        return false;
    }

    // --- 4) DSP engine + thread --------------------------------------------
    m_engine.prepare(float(m_sampleRate), m_channels, m_lowCpu);
    m_diag.latencyMs.store(m_engine.latencyMs()
                           + m_source.latencyMs());

    m_stopRequest = false;
    m_running = true;
    m_dspThread = std::thread(&AudioPipeline::dspThreadFunc, this, m_blockSize);

    VCINFO(QStringLiteral("Engine started: src=%1 ch=%2 fs=%3 block=%4 lowCpu=%5")
           .arg(chosen).arg(m_channels).arg(m_sampleRate)
           .arg(m_blockSize).arg(m_lowCpu));
    emit started();
    return true;
}

void AudioPipeline::stop(bool keepModules)
{
    if (m_running) {
        m_stopRequest = true;
        m_wake.notify_all();
        if (m_dspThread.joinable())
            m_dspThread.join();
        m_running = false;
    }
    destroyPlaybackStream();
    m_source.stop();
    if (!keepModules)
        m_vmic.destroy();
    m_diag.reset();
    VCINFO(QStringLiteral("Engine stopped"));
    emit stopped();
}

// ---------------------------------------------------------------------------
// playback side: we play the processed audio into our own null sink
// ---------------------------------------------------------------------------
bool AudioPipeline::createPlaybackStream(int channels)
{
    PulseContext::Lock lock(m_pa.mainloop());

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.rate = uint32_t(m_sampleRate);
    ss.channels = uint8_t(channels);
    pa_channel_map map;
    pa_channel_map_init_auto(&map, channels, PA_CHANNEL_MAP_DEFAULT);

    m_playStream = pa_stream_new(m_pa.context(), "VoiceCraft Output", &ss, &map);
    if (!m_playStream)
        return false;
    pa_stream_set_write_callback(m_playStream, &AudioPipeline::writeCbStatic, this);
    pa_stream_set_state_callback(m_playStream, &AudioPipeline::playStateCbStatic, this);
    pa_stream_set_underflow_callback(m_playStream,
        [](pa_stream*, void* userdata) {
            auto* self = static_cast<AudioPipeline*>(userdata);
            self->m_diag.xruns.fetch_add(1, std::memory_order_relaxed);
        }, this);

    pa_buffer_attr attr;
    attr.maxlength = uint32_t(m_sampleRate * sizeof(float) * channels * 0.2);
    attr.tlength   = uint32_t(m_sampleRate * sizeof(float) * channels / 50); // 20 ms
    attr.prebuf    = uint32_t(attr.tlength);
    attr.minreq    = uint32_t(m_sampleRate * sizeof(float) * channels / 100); // 10 ms
    attr.fragsize  = uint32_t(-1);

    const int ret = pa_stream_connect_playback(m_playStream, VirtualMicrophone::SINK_NAME,
        &attr, static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY),
        nullptr, nullptr);
    if (ret < 0) {
        pa_stream_unref(m_playStream);
        m_playStream = nullptr;
        return false;
    }
    return true;
}

void AudioPipeline::destroyPlaybackStream()
{
    if (!m_playStream) return;
    PulseContext::Lock lock(m_pa.mainloop());
    pa_stream_set_write_callback(m_playStream, nullptr, nullptr);
    pa_stream_set_state_callback(m_playStream, nullptr, nullptr);
    pa_stream_disconnect(m_playStream);
    pa_stream_unref(m_playStream);
    m_playStream = nullptr;
}

void AudioPipeline::writeCbStatic(pa_stream* s, std::size_t nbytes, void* userdata)
{
    static_cast<AudioPipeline*>(userdata)->writeCb(s, nbytes);
}

// PulseAudio thread: pull as much processed audio as the server wants.
// Never blocks; the scratch buffer is preallocated (no heap in callbacks);
// silence pads a starving buffer (chosen over xrun noise).
void AudioPipeline::writeCb(pa_stream* s, std::size_t nbytes)
{
    const std::size_t frames = nbytes / sizeof(float);
    if (m_playScratch.size() < frames)
        m_playScratch.resize(frames);   // only grows, bounded by tlength
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t got = m_outputRing.read(m_playScratch.data() + done,
                                                  frames - done);
        if (got == 0) {
            // keep the timeline running with silence
            std::memset(m_playScratch.data() + done, 0,
                        (frames - done) * sizeof(float));
            break;
        }
        done += got;
    }
    if (pa_stream_write(s, m_playScratch.data(), frames * sizeof(float),
                        nullptr, 0, PA_SEEK_RELATIVE) < 0)
        handlePlayFailure();
}

void AudioPipeline::playStateCbStatic(pa_stream* s, void* userdata)
{
    auto* self = static_cast<AudioPipeline*>(userdata);
    if (pa_stream_get_state(s) == PA_STREAM_FAILED)
        self->handlePlayFailure();
}

void AudioPipeline::handlePlayFailure()
{
    if (!m_running)
        return;
    m_lastError = QStringLiteral("Virtual microphone stream failed. "
                                 "Use 'Repair Audio Connection'.");
    VCERR(m_lastError);
    emit error(m_lastError);
}

// ---------------------------------------------------------------------------
// DSP thread
// ---------------------------------------------------------------------------
void AudioPipeline::setThreadRealtime(int priority)
{
    sched_param sp{};
    sp.sched_priority = priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0)
        VCINFO(QStringLiteral("DSP thread running with SCHED_FIFO priority %1").arg(priority));
    else
        VCWARN(QStringLiteral("SCHED_FIFO not granted (rtprio limits); "
                             "running with normal scheduling"));
}

void AudioPipeline::dspThreadFunc(int blockSize)
{
#ifdef __SSE2__
    // denormal protection: FTZ/DAZ on for the whole DSP thread
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _mm_setcsr(_mm_getcsr() | 0x40);
#endif
    setThreadRealtime(40);

    std::vector<float> block(std::size_t(blockSize) * m_channels);
    const double blockSeconds = double(blockSize) / m_sampleRate;

    // CPU accounting
    QElapsedTimer cycleTimer;
    cycleTimer.start();
    double emaLoad = 0.0;

    bool failureAnnounced = false;
    while (!m_stopRequest) {
        // capture device failure => ask the UI thread for a repair
        if (m_source.hasFailed() && !failureAnnounced) {
            failureAnnounced = true;
            emit sourceLost();   // queued to the UI thread
        }
        // wait until a full block is available in the capture ring
        if (m_source.buffer().readSpace() < std::size_t(blockSize)) {
            std::unique_lock<std::mutex> lk(m_wakeMutex);
            m_wake.wait_for(lk, std::chrono::milliseconds(4));
            continue;
        }

        cycleTimer.restart();
        if (m_source.buffer().read(block.data(), std::size_t(blockSize))
            != std::size_t(blockSize))
            continue;

        m_engine.processInterleaved(block.data(), blockSize, m_diag);

        // recording tap (post-DSP, exactly what OBS will hear)
        if (m_tap)
            m_tap->pushAudio(block.data(), blockSize);

        // push to the playback ring; on overflow drop (counted) to stay bound
        if (m_outputRing.write(block.data(), std::size_t(blockSize)) == 0)
            m_diag.xruns.fetch_add(1, std::memory_order_relaxed);

        // normalized load: 1.0 == one full core busy for this block
        const double secs = double(cycleTimer.nsecsElapsed()) / 1e9;
        const double load = secs / blockSeconds;
        emaLoad = 0.9 * emaLoad + 0.1 * load;
        m_diag.cpuLoad.store(emaLoad);
    }
    m_diag.cpuLoad.store(0.0);
}

// ---------------------------------------------------------------------------
// Repair
// ---------------------------------------------------------------------------
QString AudioPipeline::repair()
{
    QStringList actions;

    if (!m_pa.isConnected()) {
        if (!ensureConnected()) {
            return QStringLiteral("Cannot reach PulseAudio: %1").arg(m_pa.lastError());
        }
        actions << QStringLiteral("reconnected to PulseAudio");
    }

    // 1) module audit: duplicates removed, missing rebuilt
    const auto audit = m_vmic.auditAndClean(true);
    if (!audit.clean || !m_vmic.exists()) {
        if (m_running)
            destroyPlaybackStream();
        m_vmic.destroy();
        if (m_vmic.create(m_channels, m_sampleRate))
            actions << QStringLiteral("virtual microphone rebuilt");
        else
            return QStringLiteral("Could not rebuild the virtual microphone: %1")
                       .arg(m_pa.lastError());
    }

    // 2) capture stream health: rebind if device moved / vanished
    if (m_source.hasFailed() || (m_running && !m_source.isActive())) {
        if (verifyAndRebind(true))
            actions << QStringLiteral("capture stream rebound");
        else
            return QStringLiteral("capture device could not be reopened");
    }

    // 3) playback stream health
    if (m_running && !m_playStream) {
        if (createPlaybackStream(m_channels))
            actions << QStringLiteral("playback stream rebuilt");
    }

    // 4) DSP thread dead? restart the whole engine once
    if (m_running && !m_dspThread.joinable()) {
        AppSettings s;   // engine-relevant fields only (block size, rate...)
        s.sampleRate = m_sampleRate; s.blockSize = m_blockSize; s.lowCpuMode = m_lowCpu;
        stop();
        if (start(s))
            actions << QStringLiteral("DSP thread restarted");
    }

    if (actions.isEmpty())
        return QStringLiteral("audio connection verified, no repair needed");
    return actions.join(QStringLiteral("; "));
}

bool AudioPipeline::verifyAndRebind(bool autoSelect)
{
    Q_UNUSED(autoSelect);
    // Preference order: the source the user picked (if it came back),
    // otherwise the server default source. We only *read* the default,
    // we never write it.
    const QVector<PaSourceInfo> sources = m_pa.listSources();
    QString pick;
    for (const auto& src : sources)
        if (src.name == m_chosenSource) { pick = m_chosenSource; break; }
    if (pick.isEmpty()) {
        for (const auto& src : sources) {
            if (src.name.endsWith(QStringLiteral(".monitor")))
                continue;
            pick = src.name;
            break;
        }
    }
    if (pick.isEmpty())
        return false;
    m_source.stop();
    return m_source.start(pick, m_sampleRate, m_channels);
}

} // namespace vc
