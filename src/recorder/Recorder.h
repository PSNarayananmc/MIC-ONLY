#pragma once
#include <QObject>
#include <QString>
#include <sndfile.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace vc {

// Streaming recorder: pulls audio from a fixed-slot queue into a WAV or
// FLAC file on a worker thread. Formats: WAV (24-bit PCM) and FLAC (level 5).
// MP3 is intentionally NOT included (licence situation, see docs/LICENSES.md);
// FLAC is lossless, compressed and patent-free.
//
// Realtime safety: pushAudio() only does fixed-size memcpy into
// preallocated slots — no allocation, no locks, no syscalls.
class Recorder : public QObject {
    Q_OBJECT
public:
    enum class Format { Wav, Flac };

    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    bool start(const QString& filePath, int sampleRate, int channels,
               Format fmt, QString* err = nullptr);
    void stop();                       // finishes the file cleanly

    // realtime path: called from the DSP thread
    void pushAudio(const float* interleaved, int frames);

    bool isRecording() const { return m_recording; }
    QString filePath() const { return m_path; }
    double recordedSeconds() const
    { return double(m_framesWritten.load()) / double(m_sampleRate < 1 ? 48000 : m_sampleRate); }

signals:
    void started(QString path);
    void stopped(QString path, double seconds);

private:
    void writerLoop();
    bool openSndFile(const QString& path, QString* err);

    static constexpr int SLOTS = 256;
    struct Slot {
        std::vector<float> data;
        std::atomic<int> size;
        Slot() : size(0) {}
        Slot(std::vector<float>&& d, int s) : data(std::move(d)) { size.store(s); }
        Slot(Slot&& o) noexcept : data(std::move(o.data)) { size.store(o.size.load()); }
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
    };
    std::vector<Slot> m_slots;
    std::atomic<int> m_slotSizeFrames{0};
    std::atomic<std::size_t> m_widx{0}, m_ridx{0};
    Format m_fmt = Format::Wav;

    SF_INFO m_sfInfo{};
    SNDFILE* m_file = nullptr;
    QString m_path;
    std::atomic<bool> m_recording{false};
    std::thread m_writer;
    int m_sampleRate = 48000, m_channels = 1, m_blockFrames = 480;
    std::atomic<qint64> m_framesWritten{0};
    std::vector<float> m_pushAccum;     // realtime-side frame accumulator
    int m_pushFill = 0;
};

} // namespace vc
