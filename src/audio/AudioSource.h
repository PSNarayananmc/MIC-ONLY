#pragma once
#include "PulseContext.h"
#include "RingBuffer.h"
#include <QString>
#include <atomic>

namespace vc {

// Capture stream from a real microphone (or any PulseAudio source).
// The read callback runs on the PulseAudio thread: it only copies samples
// into the lock-free ring and updates metering atomics — no locks, no
// allocations, no I/O.
class AudioSource {
public:
    explicit AudioSource(PulseContext& pa) : m_pa(pa) {}
    ~AudioSource() { stop(); }

    // Start capturing from `sourceName` (empty = server default source).
    bool start(const QString& sourceName, int sampleRate, int channels)
    {
        stop();
        m_channels = channels;
        m_sampleRate = sampleRate;

        PulseContext::Lock lock(m_pa.mainloop());

        pa_sample_spec ss;
        ss.format = PA_SAMPLE_FLOAT32LE;
        ss.rate = uint32_t(sampleRate);
        ss.channels = uint8_t(channels);

        pa_channel_map map;
        pa_channel_map_init_auto(&map, channels, PA_CHANNEL_MAP_DEFAULT);
        m_stream = pa_stream_new(m_pa.context(), "VoiceCraft Capture", &ss, &map);
        if (!m_stream)
            return false;

        pa_stream_set_read_callback(m_stream, &AudioSource::readCb, this);
        pa_stream_set_state_callback(m_stream, &AudioSource::stateCb, this);
        pa_stream_set_moved_callback(m_stream, &AudioSource::movedCb, this);

        // 10 ms fragments; let the server adjust for HW constraints
        pa_buffer_attr attr;
        attr.maxlength = uint32_t(-1);
        attr.fragsize  = uint32_t(sampleRate * sizeof(float) * channels / 100); // 10 ms
        attr.prebuf = uint32_t(-1); attr.tlength = uint32_t(-1); attr.minreq = uint32_t(-1);

        const QByteArray dev = sourceName.isEmpty()
            ? QByteArray() : sourceName.toUtf8();
        const int ret = pa_stream_connect_record(m_stream, dev.constData(), &attr,
            static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY));
        if (ret < 0)
            return false;
        m_active = true;
        return true;
    }

    void stop()
    {
        if (!m_stream) return;
        PulseContext::Lock lock(m_pa.mainloop());
        pa_stream_set_read_callback(m_stream, nullptr, nullptr);
        pa_stream_set_state_callback(m_stream, nullptr, nullptr);
        pa_stream_set_moved_callback(m_stream, nullptr, nullptr);
        pa_stream_disconnect(m_stream);
        pa_stream_unref(m_stream);
        m_stream = nullptr;
        m_active = false;
        m_failed = false;
    }

    bool isActive() const { return m_active; }
    bool hasFailed() const { return m_failed; }
    QString currentSource() const { return m_currentSource; }

    RingBuffer& buffer() { return m_ring; }
    void setRingCapacity(std::size_t frames) { m_ring.reset(frames * 4); }

    // latency in ms (server-reported), control thread
    float latencyMs()
    {
        if (!m_stream) return 0;
        PulseContext::Lock lock(m_pa.mainloop());
        pa_usec_t usec = 0;
        int negative = 0;
        if (pa_stream_get_latency(m_stream, &usec, &negative) < 0)
            return 0;
        return negative ? 0.0f : float(usec) / 1000.0f;
    }

    // realtime-side metering
    void setLevelSink(std::atomic<float>* inPeak, std::atomic<float>* inRms,
                      std::atomic<std::uint64_t>* xruns)
    {
        m_inPeak = inPeak; m_inRms = inRms; m_xruns = xruns;
    }

    std::atomic<bool>& signalDetected() { return m_signalDetected; }

    // Raw-sample sink for the AUTO STUDIO VOICE analyzer: the read callback
    // copies up to capacityFrames mono frames into a caller-provided,
    // preallocated buffer (no allocation happens in the callback).
    void setRawCaptureSink(float* monoBuf, qint64 capacityFrames,
                           std::atomic<qint64>* counter)
    {
        m_rawBuf = monoBuf;
        m_rawCapacity = capacityFrames;
        m_rawCount = counter;
        m_rawCh = 1;   // downmix in callback
    }

    bool startRaw(const QString& sourceName, int sampleRate, int channels)
    { return start(sourceName, sampleRate, channels); }

private:
    static void readCb(pa_stream* s, std::size_t /*nbytes*/, void* userdata)
    {
        auto* self = static_cast<AudioSource*>(userdata);
        while (true) {
            const void* data = nullptr;
            std::size_t size = 0;
            if (pa_stream_peek(s, &data, &size) < 0)
                return;                                   // error: let state cb handle it
            if (!data || size == 0) {                     // nothing more to read
                pa_stream_drop(s);
                return;
            }
            const std::size_t frames = size / sizeof(float); // interleaved floats
            // realtime-safe: plain copy into lock-free ring; drop on overflow
            if (self->m_ring.write(static_cast<const float*>(data), frames) == 0
                && self->m_xruns)
                self->m_xruns->fetch_add(1, std::memory_order_relaxed);

            // input metering (cheap max/RMS over the chunk)
            const float* x = static_cast<const float*>(data);
            float peak = 0.0f;
            double sq = 0.0;
            for (std::size_t i = 0; i < frames; ++i) {
                const float a = std::fabs(x[i]);
                if (a > peak) peak = a;
                sq += x[i] * x[i];
            }
            if (self->m_inPeak) {
                float cur = self->m_inPeak->load(std::memory_order_relaxed);
                while (peak > cur &&
                       !self->m_inPeak->compare_exchange_weak(cur, peak))
                    ;
            }
            if (self->m_inRms)
                self->m_inRms->store(float(std::sqrt(sq / double(frames + 1))),
                                     std::memory_order_relaxed);
            if (peak > 0.01f)
                self->m_signalDetected.store(true, std::memory_order_relaxed);

            // raw analyzer sink: bounded copy, mono downmix, no allocation
            if (self->m_rawBuf && self->m_rawCount) {
                const qint64 have = self->m_rawCount->load(std::memory_order_relaxed);
                const qint64 framesHere = qint64(frames) / qint64(self->m_channels);
                const qint64 room = self->m_rawCapacity - have;
                if (room > 0) {
                    const qint64 take = std::min<qint64>(framesHere, room);
                    const int ch = self->m_channels;
                    for (qint64 f = 0; f < take; ++f) {
                        float v = 0.0f;
                        for (int c = 0; c < ch; ++c)
                            v += x[f * ch + c];
                        self->m_rawBuf[have + f] = v / float(ch);
                    }
                    self->m_rawCount->store(have + take, std::memory_order_release);
                }
            }

            if (pa_stream_drop(s) < 0)
                return;
        }
    }

    static void stateCb(pa_stream* s, void* userdata)
    {
        auto* self = static_cast<AudioSource*>(userdata);
        switch (pa_stream_get_state(s)) {
        case PA_STREAM_FAILED:
            self->m_failed = true;
            break;
        case PA_STREAM_TERMINATED:
            self->m_active = false;
            break;
        case PA_STREAM_READY: {
            const char* dev = pa_stream_get_device_name(s);
            if (dev) self->m_currentSource = QString::fromUtf8(dev);
            break;
        }
        default:
            break;
        }
    }

    static void movedCb(pa_stream* s, void* userdata)
    {
        auto* self = static_cast<AudioSource*>(userdata);
        const char* dev = pa_stream_get_device_name(s);
        if (dev) self->m_currentSource = QString::fromUtf8(dev);
        // device moved (e.g. our source vanished): treat as event for repair
        self->m_moved = true;
    }

    PulseContext& m_pa;
    pa_stream* m_stream = nullptr;
    RingBuffer m_ring;
    QString m_currentSource;
    std::atomic<float>* m_inPeak = nullptr;
    std::atomic<float>* m_inRms = nullptr;
    std::atomic<std::uint64_t>* m_xruns = nullptr;
    std::atomic<bool> m_signalDetected{false};
    int m_channels = 1, m_sampleRate = 48000;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_moved{false};
    float* m_rawBuf = nullptr;
    qint64 m_rawCapacity = 0;
    std::atomic<qint64>* m_rawCount = nullptr;
    int m_rawCh = 1;
};

} // namespace vc
