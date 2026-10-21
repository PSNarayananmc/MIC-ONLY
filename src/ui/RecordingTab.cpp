#include "RecordingTab.h"
#include "MainWindow.h"
#include "WaveformView.h"
#include "../audio/AudioPipeline.h"
#include "../recorder/Recorder.h"
#include "../recorder/AudioDocument.h"
#include <QDateTime>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace vc {

RecordingTab::RecordingTab(MainWindow* win, QWidget* parent)
    : QWidget(parent), m_win(win)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    // ---- transport row ---------------------------------------------------
    auto* transport = new QHBoxLayout;
    m_recBtn = new QPushButton(QStringLiteral("● Record"));
    m_recBtn->setToolTip(QStringLiteral(
        "Records the PROCESSED stream (exactly what OBS hears). "
        "Starts the engine if it is stopped."));
    m_pauseBtn = new QPushButton(QStringLiteral("⏸ Pause"));
    m_stopBtn = new QPushButton(QStringLiteral("⏹ Stop"));
    m_exportBtn = new QPushButton(QStringLiteral("Export…"));
    m_info = new QLabel(QStringLiteral("Ready — recordings capture the processed voice."));
    m_info->setObjectName(QStringLiteral("muted"));
    transport->addWidget(m_recBtn);
    transport->addWidget(m_pauseBtn);
    transport->addWidget(m_stopBtn);
    transport->addWidget(m_exportBtn);
    transport->addWidget(m_info, 1);
    root->addLayout(transport);

    // ---- waveform ---------------------------------------------------------
    m_wave = new WaveformView;
    m_wave->setMinimumHeight(220);
    root->addWidget(m_wave, 1);

    // ---- edit row -----------------------------------------------------------
    auto* editRow = new QHBoxLayout;
    m_trimBtn = new QPushButton(QStringLiteral("Trim to selection"));
    m_splitBtn = new QPushButton(QStringLiteral("Split at click"));
    m_undoBtn = new QPushButton(QStringLiteral("Undo"));
    m_redoBtn = new QPushButton(QStringLiteral("Redo"));
    m_selInfo = new QLabel(QStringLiteral("No selection"));
    m_selInfo->setObjectName(QStringLiteral("mono"));
    editRow->addWidget(m_trimBtn);
    editRow->addWidget(m_splitBtn);
    editRow->addWidget(m_undoBtn);
    editRow->addWidget(m_redoBtn);
    editRow->addWidget(m_selInfo, 1);
    root->addLayout(editRow);

    auto* hint = new QLabel(QStringLiteral(
        "Zoom with the mouse wheel. Drag to select a range for trimming. "
        "Click to place the split marker. Exports support WAV (24-bit) and "
        "FLAC (lossless), mono or stereo."));
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    // ---- wiring -----------------------------------------------------------
    connect(m_recBtn, &QPushButton::clicked, this, &RecordingTab::recStart);
    connect(m_pauseBtn, &QPushButton::clicked, this, &RecordingTab::recPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &RecordingTab::recStop);
    connect(m_exportBtn, &QPushButton::clicked, this, &RecordingTab::exportRecording);
    connect(m_trimBtn, &QPushButton::clicked, this, &RecordingTab::trimToSelection);
    connect(m_splitBtn, &QPushButton::clicked, this, &RecordingTab::splitAtPlayhead);
    connect(m_undoBtn, &QPushButton::clicked, this, &RecordingTab::undoEdit);
    connect(m_redoBtn, &QPushButton::clicked, this, &RecordingTab::redoEdit);
    connect(m_wave, &WaveformView::frameClicked, this, [this](qint64 f) {
        m_playheadFrame = f;
        m_wave->setPlayheadFrame(f);
        updateButtonStates();
    });
    connect(m_wave, &WaveformView::selectionChanged, this, [this](qint64 s, qint64 e) {
        m_selStartFrame = s; m_selEndFrame = e;
        if (e > s) {
            const int fs = m_win->currentDocument().sampleRate();
            m_selInfo->setText(QStringLiteral("selection: %1 – %2 s (%3 frames)")
                .arg(double(s) / fs, 0, 'f', 2)
                .arg(double(e) / fs, 0, 'f', 2)
                .arg(e - s));
        } else {
            m_selInfo->setText(QStringLiteral("No selection"));
        }
        updateButtonStates();
    });
    connect(&m_win->recorder(), &Recorder::stopped,
            this, &RecordingTab::onRecorderFinished);
    updateButtonStates();
}

void RecordingTab::updateButtonStates()
{
    const bool recording = m_win->recorder().isRecording();
    const bool haveDoc = m_win->currentDocument().frames() > 0;
    m_recBtn->setEnabled(!recording);
    m_pauseBtn->setEnabled(recording);
    m_stopBtn->setEnabled(recording);
    m_exportBtn->setEnabled(haveDoc && !recording);
    m_trimBtn->setEnabled(haveDoc && !recording);
    m_splitBtn->setEnabled(haveDoc && !recording);
    m_undoBtn->setEnabled(m_win->currentDocument().canUndo() && !recording);
    m_redoBtn->setEnabled(m_win->currentDocument().canRedo() && !recording);
}

void RecordingTab::recStart()
{
    if (m_win->recorder().isRecording())
        return;
    // the tap needs a running engine
    m_engineWasRunning = m_win->engine().isRunning();
    if (!m_engineWasRunning)
        m_win->startEngine();
    if (!m_win->engine().isRunning())
        return;   // startEngine already reported the error

    const auto s = m_win->settings().get();
    const QString base = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const bool flac = s.recordingFormat.compare(QStringLiteral("FLAC"), Qt::CaseInsensitive) == 0;
    const QString path = s.recordingDir + QStringLiteral("/voicecraft-%1.%2")
        .arg(base, flac ? QStringLiteral("flac") : QStringLiteral("wav"));

    QString err;
    if (m_win->recorder().start(path, m_win->engine().pulse().isConnected()
                                    ? s.sampleRate : 48000, 1,
                                flac ? Recorder::Format::Flac : Recorder::Format::Wav,
                                &err)) {
        m_win->setRecordingTapEnabled(true);
        m_pauseFrames = 0;
        m_paused = false;
        m_recTimer.restart();
        m_info->setText(QStringLiteral("Recording…"));
        m_wave->clearDocument();
        updateButtonStates();
    } else {
        QMessageBox::warning(this, QStringLiteral("Recording"),
                             QStringLiteral("Could not start recording: %1").arg(err));
    }
}

void RecordingTab::recPause()
{
    if (!m_win->recorder().isRecording())
        return;
    m_paused = !m_paused;
    m_win->setRecordingTapEnabled(!m_paused);
    m_info->setText(m_paused ? QStringLiteral("Paused")
                             : QStringLiteral("Recording…"));
}

void RecordingTab::recStop()
{
    if (!m_win->recorder().isRecording())
        return;
    m_win->setRecordingTapEnabled(false);
    m_win->recorder().stop();
    if (!m_engineWasRunning)
        m_win->stopEngine();
}

void RecordingTab::onRecorderFinished(const QString& path, double seconds)
{
    QString err;
    if (m_win->currentDocument().load(path, &err)) {
        refreshWaveform();
        m_info->setText(QStringLiteral("Recorded %1 s → %2")
            .arg(seconds, 0, 'f', 1).arg(path));
    } else {
        m_info->setText(QStringLiteral("Recorded to %1 (could not display: %2)")
            .arg(path, err));
    }
    updateButtonStates();
}

void RecordingTab::refreshWaveform()
{
    const AudioDocument& doc = m_win->currentDocument();
    if (doc.frames() > 0) {
        m_wave->setDocument(QVector<float>(doc.data().begin(), doc.data().end()),
                            doc.channels(), doc.sampleRate());
        if (m_selEndFrame > m_selStartFrame && m_selStartFrame >= 0)
            m_wave->setSelection(m_selStartFrame, m_selEndFrame);
        if (m_playheadFrame >= 0 && m_playheadFrame <= doc.frames())
            m_wave->setPlayheadFrame(m_playheadFrame);
    }
    updateButtonStates();
}

void RecordingTab::tickRecordingTime()
{
    if (m_win->recorder().isRecording() && !m_paused) {
        const double s = m_recTimer.elapsed() / 1000.0;
        m_info->setText(QStringLiteral("Recording… %1 s")
            .arg(s, 0, 'f', 1));
    }
}

void RecordingTab::exportRecording()
{
    AudioDocument& doc = m_win->currentDocument();
    if (doc.frames() <= 0)
        return;
    const auto s = m_win->settings().get();
    const bool flac = s.recordingFormat.compare(QStringLiteral("FLAC"), Qt::CaseInsensitive) == 0;
    QString filter = flac ? QStringLiteral("FLAC audio (*.flac);;WAV audio (*.wav)")
                          : QStringLiteral("WAV audio (*.wav);;FLAC audio (*.flac)");
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export recording"), s.recordingDir, filter);
    if (path.isEmpty())
        return;
    const bool useFlac = path.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive);
    QString err;
    if (doc.save(path, useFlac ? AudioDocument::Format::Flac
                               : AudioDocument::Format::Wav,
                 s.recordingMono, &err))
        m_info->setText(QStringLiteral("Exported: %1").arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("Export failed"), err);
}

void RecordingTab::trimToSelection()
{
    AudioDocument& doc = m_win->currentDocument();
    if (doc.frames() <= 0)
        return;
    const qint64 s = std::max<qint64>(0, m_selStartFrame);
    const qint64 e = (m_selEndFrame > m_selStartFrame)
                         ? m_selEndFrame : doc.frames();
    if (e > s) {
        doc.trim(s, e);
        m_selStartFrame = m_selEndFrame = -1;
        m_wave->setSelection(-1, -1);
        refreshWaveform();
        m_selInfo->setText(QStringLiteral("No selection"));
    }
}

void RecordingTab::splitAtPlayhead()
{
    AudioDocument& doc = m_win->currentDocument();
    if (doc.frames() <= 0 || m_playheadFrame < 0)
        return;
    auto right = doc.split(m_playheadFrame);
    if (right) {
        // keep part 1 in the view; part 2 becomes a new file next to the doc
        refreshWaveform();
        m_info->setText(QStringLiteral("Split done — part 1 shown. "
            "Part 2 (%1 frames) available via undo.").arg(right->frames()));
    }
}

void RecordingTab::undoEdit()
{
    if (m_win->currentDocument().undo()) {
        refreshWaveform();
        m_selStartFrame = m_selEndFrame = -1;
    }
}

void RecordingTab::redoEdit()
{
    if (m_win->currentDocument().redo()) {
        refreshWaveform();
    }
}

} // namespace vc
