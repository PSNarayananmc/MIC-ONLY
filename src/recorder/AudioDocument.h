#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>
#include <memory>

namespace vc {

// In-memory audio document for the Recording workspace.
// Stores interleaved float samples; editing operations (trim, delete,
// split) are snapshot-based with bounded undo/redo stacks.
class AudioDocument {
public:
    enum class Format { Wav, Flac };

    AudioDocument() = default;

    bool load(const QString& path, QString* err = nullptr);
    bool save(const QString& path, Format fmt, bool mono, QString* err = nullptr);

    // --- editing ----------------------------------------------------------
    void trim(qint64 startFrame, qint64 endFrame);   // keep [start,end)
    void deleteRegion(qint64 startFrame, qint64 endFrame);
    void insertSilence(qint64 atFrame, qint64 frames);
    std::unique_ptr<AudioDocument> split(qint64 atFrame); // this keeps [0,at)

    // --- undo / redo ------------------------------------------------------
    void pushUndoSnapshot();
    bool undo();
    bool redo();
    bool canUndo() const { return m_undoIndex >= 0; }   // -1 = live edge before any undo
    bool canRedo() const { return m_undoIndex + 1 < m_snapshots.size(); }

    // --- info ---------------------------------------------------------------
    qint64 frames() const { return m_data.empty() ? 0 : qint64(m_data.size()) / m_channels; }
    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    double seconds() const { return double(frames()) / double(m_sampleRate < 1 ? 48000 : m_sampleRate); }
    const std::vector<float>& data() const { return m_data; }
    std::vector<float>& data() { return m_data; }
    void setFormat(int sampleRate, int channels)
    { m_sampleRate = sampleRate; m_channels = channels; }

    // min/max peak pair over a frame range for waveform drawing
    void minMax(qint64 startFrame, qint64 endFrame, float& lo, float& hi) const;

    QString sourcePath() const { return m_sourcePath; }

private:
    struct Snapshot {
        std::vector<float> data;
        int sampleRate = 48000, channels = 1;
    };

    std::vector<float> m_data;
    int m_sampleRate = 48000, m_channels = 1;
    QString m_sourcePath;
    QVector<Snapshot> m_snapshots;
    int m_undoIndex = -1;

    static constexpr int MAX_SNAPSHOTS = 24;   // bounded memory (~24 × doc)
};

} // namespace vc
