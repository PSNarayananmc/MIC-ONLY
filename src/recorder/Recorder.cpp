#include "Recorder.h"
#include "../core/Log.h"
#include <QDir>
#include <QFileInfo>
#include <cstring>
#include <chrono>

namespace vc {

Recorder::Recorder(QObject* parent) : QObject(parent) {}

Recorder::~Recorder() { stop(); }

bool Recorder::openSndFile(const QString& path, QString* err)
{
    m_sfInfo = {};
    m_sfInfo.samplerate = m_sampleRate;
    m_sfInfo.channels = m_channels;
    m_sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    if (m_fmt == Format::Flac)
        m_sfInfo.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;

    m_file = sf_open(path.toUtf8().constData(), SFM_WRITE, &m_sfInfo);
    if (!m_file) {
        if (err) *err = QString::fromUtf8(sf_strerror(nullptr));
        return false;
    }
    // FLAC metadata: library version comment
    const char* comment = "Recorded by VoiceCraft Studio";
    sf_set_string(m_file, SF_STR_COMMENT, comment);
    return true;
}

bool Recorder::start(const QString& filePath, int sampleRate, int channels,
                     Format fmt, QString* err)
{
    stop();
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_fmt = fmt;
    m_path = filePath;
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    if (!openSndFile(filePath, err))
        return false;

    // 10 ms blocks preallocated: realtime side never allocates
    m_blockFrames = sampleRate / 100;
    m_slotSizeFrames.store(m_blockFrames);
    m_slots.clear();
    m_slots.reserve(SLOTS);
    for (int i = 0; i < SLOTS; ++i)
        m_slots.push_back(
            Slot{std::vector<float>(std::size_t(m_blockFrames) * m_channels, 0.0f),
                 0});
    m_pushAccum.assign(std::size_t(m_blockFrames) * m_channels, 0.0f);
    m_pushFill = 0;
    m_framesWritten.store(0);

    m_recording.store(true);
    m_writer = std::thread(&Recorder::writerLoop, this);
    VCINFO(QStringLiteral("Recording started: %1").arg(filePath));
    emit started(filePath);
    return true;
}

void Recorder::pushAudio(const float* interleaved, int frames)
{
    if (!m_recording)
        return;
    const int ch = m_channels;
    int consumed = 0;
    while (consumed < frames) {
        const int want = m_blockFrames - m_pushFill;
        const int take = std::min(want, frames - consumed);
        std::memcpy(&m_pushAccum[std::size_t(m_pushFill) * ch],
                    interleaved + std::size_t(consumed) * ch,
                    std::size_t(take) * ch * sizeof(float));
        m_pushFill += take;
        consumed += take;

        if (m_pushFill == m_blockFrames) {
            const std::size_t w = m_widx.load(std::memory_order_relaxed);
            const std::size_t r = m_ridx.load(std::memory_order_acquire);
            if (w - r >= SLOTS) {
                // queue full: drop the block (never block the DSP thread)
                m_pushFill = 0;
                return;
            }
            auto& slot = m_slots[w % SLOTS];
            std::memcpy(slot.data.data(), m_pushAccum.data(),
                        std::size_t(m_blockFrames) * ch * sizeof(float));
            slot.size.store(m_blockFrames, std::memory_order_release);
            m_widx.store(w + 1, std::memory_order_release);
            m_pushFill = 0;
        }
    }
}

void Recorder::writerLoop()
{
    while (true) {
        const std::size_t r = m_ridx.load(std::memory_order_relaxed);
        const std::size_t w = m_widx.load(std::memory_order_acquire);
        if (r < w) {
            auto& slot = m_slots[r % SLOTS];
            const int frames = slot.size.load(std::memory_order_acquire);
            if (frames > 0) {
                sf_writef_float(m_file, slot.data.data(), frames);
                m_framesWritten.fetch_add(frames);
                slot.size.store(0, std::memory_order_release);
            }
            m_ridx.store(r + 1, std::memory_order_release);
            continue;
        }
        if (!m_recording.load())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

void Recorder::stop()
{
    if (!m_recording.load() && !m_writer.joinable())
        return;
    m_recording.store(false);
    if (m_writer.joinable())
        m_writer.join();
    if (m_file) {
        sf_close(m_file);
        m_file = nullptr;
        const double secs = recordedSeconds();
        VCINFO(QStringLiteral("Recording finished: %1 (%2 s)").arg(m_path).arg(secs));
        emit stopped(m_path, secs);
    }
}

} // namespace vc
