#include "AudioDocument.h"
#include <sndfile.h>
#include <algorithm>
#include <cstring>

namespace vc {

bool AudioDocument::load(const QString& path, QString* err)
{
    SF_INFO info{};
    SNDFILE* f = sf_open(path.toUtf8().constData(), SFM_READ, &info);
    if (!f) {
        if (err) *err = QString::fromUtf8(sf_strerror(nullptr));
        return false;
    }
    if (info.channels < 1 || info.channels > 2 || info.frames < 1) {
        sf_close(f);
        if (err) *err = QStringLiteral("unsupported layout (%1 ch)").arg(info.channels);
        return false;
    }
    m_data.resize(std::size_t(info.frames) * info.channels);
    const sf_count_t read = sf_readf_float(f, m_data.data(), info.frames);
    sf_close(f);
    m_sampleRate = info.samplerate;
    m_channels = info.channels;
    m_data.resize(std::size_t(read) * info.channels);
    m_sourcePath = path;
    m_snapshots.clear();
    m_undoIndex = -1;
    return true;
}

bool AudioDocument::save(const QString& path, Format fmt, bool mono, QString* err)
{
    SF_INFO info{};
    info.samplerate = m_sampleRate;
    info.channels = mono ? 1 : m_channels;
    info.format = (fmt == Format::Flac)
        ? (SF_FORMAT_FLAC | SF_FORMAT_PCM_16)
        : (SF_FORMAT_WAV | SF_FORMAT_PCM_24);

    std::vector<float> out;
    const float* src = m_data.data();
    if (mono && m_channels == 2) {
        out.resize(m_data.size() / 2);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = 0.5f * (m_data[2 * i] + m_data[2 * i + 1]);
        src = out.data();
    } else if (mono && m_channels == 1) {
        info.channels = 1;
    }
    // enforce mono frames count
    const sf_count_t framesToWrite = (mono && m_channels == 2)
        ? sf_count_t(m_data.size() / 2) : sf_count_t(frames());

    SNDFILE* f = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
    if (!f) {
        if (err) *err = QString::fromUtf8(sf_strerror(nullptr));
        return false;
    }
    sf_set_string(f, SF_STR_COMMENT, "Edited in VoiceCraft Studio");
    const sf_count_t written = sf_writef_float(f, src, framesToWrite);
    sf_close(f);
    if (written != framesToWrite) {
        if (err) *err = QStringLiteral("short write (%1/%2)").arg(written).arg(framesToWrite);
        return false;
    }
    return true;
}

void AudioDocument::pushUndoSnapshot()
{
    // drop redo tail
    while (m_snapshots.size() > m_undoIndex + 1)
        m_snapshots.removeLast();
    Snapshot s;
    s.data = m_data;
    s.sampleRate = m_sampleRate;
    s.channels = m_channels;
    m_snapshots.append(std::move(s));
    if (m_snapshots.size() > MAX_SNAPSHOTS)
        m_snapshots.removeFirst();
    m_undoIndex = m_snapshots.size() - 1;
}

bool AudioDocument::undo()
{
    if (!canUndo())
        return false;
    // restore the snapshot the pending mutation pushed (the state right
    // before the most recent edit), THEN step the cursor back
    const Snapshot& s = m_snapshots[m_undoIndex];
    m_data = s.data;
    m_sampleRate = s.sampleRate;
    m_channels = s.channels;
    --m_undoIndex;
    return true;
}

bool AudioDocument::redo()
{
    if (!canRedo())
        return false;
    ++m_undoIndex;   // move to the next state, then restore it
    const Snapshot& s = m_snapshots[m_undoIndex];
    m_data = s.data;
    m_sampleRate = s.sampleRate;
    m_channels = s.channels;
    return true;
}

void AudioDocument::trim(qint64 startFrame, qint64 endFrame)
{
    startFrame = std::max<qint64>(0, startFrame);
    endFrame = std::min(endFrame, frames());
    if (endFrame <= startFrame)
        return;
    pushUndoSnapshot();
    const auto b = m_data.begin() + startFrame * m_channels;
    const auto e = m_data.begin() + endFrame * m_channels;
    std::vector<float> trimmed(b, e);
    m_data = std::move(trimmed);
}

void AudioDocument::deleteRegion(qint64 startFrame, qint64 endFrame)
{
    startFrame = std::max<qint64>(0, startFrame);
    endFrame = std::min(endFrame, frames());
    if (endFrame <= startFrame)
        return;
    pushUndoSnapshot();
    m_data.erase(m_data.begin() + startFrame * m_channels,
                 m_data.begin() + endFrame * m_channels);
}

void AudioDocument::insertSilence(qint64 atFrame, qint64 frames)
{
    atFrame = std::max<qint64>(0, std::min(atFrame, this->frames()));
    if (frames <= 0)
        return;
    pushUndoSnapshot();
    m_data.insert(m_data.begin() + atFrame * m_channels,
                  std::size_t(frames) * m_channels, 0.0f);
}

std::unique_ptr<AudioDocument> AudioDocument::split(qint64 atFrame)
{
    atFrame = std::max<qint64>(0, std::min(atFrame, frames()));
    if (atFrame <= 0)
        return nullptr;
    auto right = std::make_unique<AudioDocument>();
    right->setFormat(m_sampleRate, m_channels);
    const std::size_t splitIdx = std::size_t(atFrame) * m_channels;
    right->m_data.assign(m_data.begin() + splitIdx, m_data.end());
    right->m_sourcePath = m_sourcePath + QStringLiteral(" [part 2]");
    pushUndoSnapshot();
    m_data.resize(splitIdx);
    m_sourcePath += QStringLiteral(" [part 1]");
    return right;
}

void AudioDocument::minMax(qint64 startFrame, qint64 endFrame,
                           float& lo, float& hi) const
{
    lo = 0.0f; hi = 0.0f;
    startFrame = std::max<qint64>(0, startFrame);
    endFrame = std::min(endFrame, frames());
    for (qint64 f = startFrame; f < endFrame; ++f) {
        for (int c = 0; c < m_channels; ++c) {
            const float v = m_data[std::size_t(f) * m_channels + c];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
}

} // namespace vc
