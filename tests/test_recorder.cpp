// ---------------------------------------------------------------------------
// Recorder + AudioDocument tests — recording writes REAL files with REAL
// samples (WAV 24-bit, FLAC), and the editor's undo/redo stack behaves.
// ---------------------------------------------------------------------------
#include "../src/recorder/Recorder.h"
#include "../src/recorder/AudioDocument.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <vector>

using namespace vc;

static int g_fail = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            qCritical("FAIL: %s (line %d)", msg, __LINE__);                  \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

static std::vector<float> testSine(int frames, int ch, double freq)
{
    std::vector<float> v(std::size_t(frames) * ch);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
            v[std::size_t(i) * ch + c] =
                float(0.5 * std::sin(2.0 * M_PI * freq * i / 48000.0));
    return v;
}

static void runRecorder(Recorder::Format fmt, const char* ext)
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir valid");

    Recorder rec;
    const QString path = dir.filePath(
        QStringLiteral("rec_test.%1").arg(ext));
    QString err;
    CHECK(rec.start(path, 48000, 2, fmt, &err),
          qPrintable(QStringLiteral("%1 recorder starts: %2").arg(ext, err)));

    // push 2 s of audio in 10 ms blocks (realtime-size chunks)
    const auto data = testSine(96000, 2, 220.0);
    for (int off = 0; off + 480 <= 96000; off += 480)
        rec.pushAudio(data.data() + std::size_t(off) * 2, 480);

    rec.stop();
    CHECK(rec.recordedSeconds() > 1.9, qPrintable(
        QStringLiteral("%1: recorded duration tracked").arg(ext)));   // after stop: writer drained
    CHECK(QFile::exists(path), qPrintable(
        QStringLiteral("%1 file written").arg(ext)));
    CHECK(QFile(path).size() > 20000, qPrintable(
        QStringLiteral("%1 file has real payload (%2 bytes)")
            .arg(ext).arg(QFile(path).size())));

    // read it back through AudioDocument and verify the content
    AudioDocument doc;
    CHECK(doc.load(path, &err),
          qPrintable(QStringLiteral("%1 loads back: %2").arg(ext, err)));
    CHECK(doc.sampleRate() == 48000, qPrintable(
        QStringLiteral("%1 sample rate preserved").arg(ext)));
    CHECK(qAbs(doc.frames() - 96000) < 4800, qPrintable(
        QStringLiteral("%1 frame count matches (±100 ms): %2")
            .arg(ext).arg(doc.frames())));

    // sample-level sanity: RMS in a sane range, no DC explosion
    double sum = 0;
    const auto& d = doc.data();
    for (std::size_t i = 0; i < d.size(); ++i) sum += double(d[i]) * d[i];
    const double rms = std::sqrt(sum / std::max<std::size_t>(1, d.size()));
    qInfo("  %s: %lld frames, RMS %.3f", ext, doc.frames(), rms);
    CHECK(rms > 0.2 && rms < 0.6, qPrintable(
        QStringLiteral("%1: sine survived the round trip").arg(ext)));
}

static void testRecorderWav()  { runRecorder(Recorder::Format::Wav,  "wav");  }
static void testRecorderFlac() { runRecorder(Recorder::Format::Flac, "flac"); }

// FLAC must actually be smaller than raw WAV (compression check)
static void testFlacCompresses()
{
    QTemporaryDir dir;
    Recorder rec;
    QString err;
    const auto data = testSine(96000, 1, 440.0);
    const QString wpath = dir.filePath("a.wav");
    const QString fpath = dir.filePath("a.flac");
    CHECK(rec.start(wpath, 48000, 1, Recorder::Format::Wav, &err), "wav start");
    for (int off = 0; off + 480 <= 96000; off += 480)
        rec.pushAudio(data.data() + off, 480);
    rec.stop();
    CHECK(rec.start(fpath, 48000, 1, Recorder::Format::Flac, &err),
          "flac start");
    for (int off = 0; off + 480 <= 96000; off += 480)
        rec.pushAudio(data.data() + off, 480);
    rec.stop();
    const qint64 w = QFile(wpath).size(), f = QFile(fpath).size();
    qInfo("  FLAC %lld vs WAV %lld bytes", f, w);
    CHECK(f < w, "FLAC is smaller than WAV for tonal content");
}

// realtime contract: pushAudio must never block for long
static void testPushLatency()
{
    QTemporaryDir dir;
    Recorder rec;
    QString err;
    CHECK(rec.start(dir.filePath("lat.flac"), 48000, 2,
                    Recorder::Format::Flac, &err), "start");
    const auto data = testSine(4800, 2, 1000.0);
    qint64 worstNs = 0;
    for (int off = 0; off + 480 <= 4800; off += 480) {
        QElapsedTimer t;
        t.start();
        rec.pushAudio(data.data() + std::size_t(off) * 2, 480);
        worstNs = std::max(worstNs, t.nsecsElapsed());
    }
    rec.stop();
    qInfo("  pushAudio worst-case: %lld us", worstNs / 1000);
    CHECK(worstNs / 1000 < 1000, "pushAudio stays under 1 ms (RT contract)");
}

// ---------------------------------------------------------------------------
// AudioDocument editing semantics
// ---------------------------------------------------------------------------
static void testEditing()
{
    AudioDocument doc;
    doc.setFormat(48000, 1);
    doc.data() = testSine(48000, 1, 220.0);   // 1 s

    // NOTE: trim/deleteRegion/insertSilence/split push their own undo
    // snapshots internally — do NOT push again here.

    // trim to first half
    doc.trim(0, 24000);
    CHECK(doc.frames() == 24000, "trim keeps [start,end)");

    // delete a middle region
    doc.deleteRegion(1000, 5000);
    CHECK(doc.frames() == 24000 - 4000, "deleteRegion shrinks");

    // insert silence
    doc.insertSilence(0, 8000);
    CHECK(doc.frames() == 20000 + 8000, "insertSilence grows");

    // split
    auto tail = doc.split(14000);
    CHECK(doc.frames() == 14000, "split: head keeps [0,at)");
    CHECK(tail && tail->frames() == 14000, "split: tail holds the rest");

    // undo all the way back
    CHECK(doc.canUndo(), "undo available");
    doc.undo();
    CHECK(doc.frames() == 28000, "undo restores pre-split");
    doc.undo();
    CHECK(doc.frames() == 20000, "undo restores pre-insert");
    doc.undo();
    CHECK(doc.frames() == 24000, "undo restores pre-delete");
    doc.undo();
    CHECK(doc.frames() == 48000, "undo restores pre-trim");
    CHECK(!doc.canUndo(), "undo stack exhausted");
    CHECK(doc.canRedo(), "redo available after undo");
}

static void testLoadNonexistent()
{
    AudioDocument doc;
    QString err;
    CHECK(!doc.load("/nonexistent/file.wav", &err), "missing file rejected");
    CHECK(!err.isEmpty(), "error message provided");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInfo("== Recorder / AudioDocument test suite ==");
    testRecorderWav();
    testRecorderFlac();
    testFlacCompresses();
    testPushLatency();
    testEditing();
    testLoadNonexistent();

    if (g_fail == 0) {
        qInfo("RECORDER TESTS: ALL PASSED");
        return 0;
    }
    qCritical("RECORDER TESTS: %d FAILURES", g_fail);
    return 1;
}
