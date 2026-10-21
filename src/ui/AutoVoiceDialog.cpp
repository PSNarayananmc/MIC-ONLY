#include "AutoVoiceDialog.h"
#include "MainWindow.h"
#include "../audio/AudioSource.h"
#include "../audio/AudioPipeline.h"
#include "../core/Diagnostics.h"
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>

namespace vc {

AutoVoiceDialog::AutoVoiceDialog(MainWindow* win, QWidget* parent)
    : QDialog(parent)
    , m_win(win)
    , m_samples(48000 * 12, 0.0f)     // up to 12 s @ 48 kHz, preallocated
{
    setWindowTitle(QStringLiteral("AUTO STUDIO VOICE"));
    setMinimumSize(560, 380);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(14);

    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    root->addWidget(m_text, 1);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 600);
    m_progress->setValue(0);
    m_progress->hide();
    root->addWidget(m_progress);

    auto* row = new QHBoxLayout;
    m_actionBtn = new QPushButton(QStringLiteral("Record sample"));
    m_applyBtn = new QPushButton(QStringLiteral("Apply recommended settings"));
    m_applyBtn->setEnabled(false);
    auto* cancel = new QPushButton(QStringLiteral("Close"));
    row->addWidget(m_actionBtn);
    row->addWidget(m_applyBtn);
    row->addWidget(cancel);
    row->addStretch(1);
    root->addLayout(row);

    connect(m_actionBtn, &QPushButton::clicked, this, &AutoVoiceDialog::beginSample);
    connect(m_applyBtn, &QPushButton::clicked, this, &AutoVoiceDialog::applyRecommendation);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 frames = m_rawCount.load();
        m_progress->setValue(int(std::min<qint64>(600, frames / 48)));  // 6 s = 600
        if (m_elapsed.elapsed() >= 6000)
            finishSample();
    });

    setStage(Stage::Intro);
}

AutoVoiceDialog::~AutoVoiceDialog() = default;

void AutoVoiceDialog::setStage(Stage s)
{
    m_stage = s;
    switch (s) {
    case Stage::Intro:
        m_text->setText(QStringLiteral(
            "<b>AUTO STUDIO VOICE</b><br><br>"
            "I will record <b>6 seconds</b> of your microphone (raw signal) "
            "and analyse it on this computer:<br>"
            "• noise floor and speech level<br>"
            "• low-frequency rumble<br>"
            "• sibilance (S-sounds)<br>"
            "• clipping risk and loudness (LUFS)<br><br>"
            "Everything stays <b>offline</b> — the audio is processed in RAM, "
            "never uploaded, never saved to disk.<br><br>"
            "Please speak naturally, as if recording your show. "
            "Start the sample, then talk for the full 6 seconds."));
        m_progress->hide();
        m_actionBtn->setText(QStringLiteral("Record sample"));
        m_actionBtn->setEnabled(true);
        m_applyBtn->setEnabled(false);
        break;
    case Stage::Recording:
        m_text->setText(QStringLiteral(
            "Recording… please talk now (reading a paragraph aloud works best)."));
        m_progress->show();
        m_progress->setValue(0);
        m_actionBtn->setEnabled(false);
        break;
    case Stage::Done:
        m_progress->hide();
        m_actionBtn->setText(QStringLiteral("Record again"));
        m_actionBtn->setEnabled(true);
        break;
    }
}

void AutoVoiceDialog::beginSample()
{
    // capture the RAW microphone: separate short-lived capture stream
    m_rawCount.store(0);
    auto* src = new AudioSource(m_win->engine().pulse());   // freed in finishSample
    const auto s = m_win->settings().get();
    src->setRawCaptureSink(m_samples.data(), qint64(m_samples.size()), &m_rawCount);
    if (!src->startRaw(s.sourceName, 48000, 1)) {
        m_text->setText(QStringLiteral(
            "Could not open the microphone for the sample (%1). "
            "Check that the engine is connected to PulseAudio.")
            .arg(m_win->engine().pulse().lastError()));
        delete src;
        return;
    }
    m_sampleSource = src;
    m_elapsed.restart();
    m_timer->start();
    setStage(Stage::Recording);
}

void AutoVoiceDialog::finishSample()
{
    m_timer->stop();
    if (m_sampleSource) {
        m_sampleSource->stop();
        delete m_sampleSource;
        m_sampleSource = nullptr;
    }
    const qint64 frames = m_rawCount.load();
    if (frames < 48000 * 3) {
        m_text->setText(QStringLiteral(
            "The sample was too short (%1 s). Please try again and talk "
            "for the full 6 seconds.").arg(double(frames) / 48000.0, 0, 'f', 1));
        setStage(Stage::Intro);
        return;
    }

    VoiceAnalyzer analyzer;
    m_result = analyzer.analyze(m_samples.data(), frames, 48000, 1);

    QString html = QStringLiteral("<b>Analysis results</b><br><br><code>");
    html += QStringLiteral("noise floor:      %1 dBFS<br>").arg(m_result.noiseFloorDb, 0, 'f', 1);
    html += QStringLiteral("speech level:     %1 dBFS<br>").arg(m_result.speechLoudness, 0, 'f', 1);
    html += QStringLiteral("integrated:       %1 LUFS<br>").arg(m_result.integratedLufs, 0, 'f', 1);
    html += QStringLiteral("low rumble:       %1 %<br>").arg(m_result.lowRumbleRatio * 100.0f, 0, 'f', 1);
    html += QStringLiteral("sibilance:        %1 %<br>").arg(m_result.sibilanceRatio * 100.0f, 0, 'f', 1);
    html += QStringLiteral("clipped samples:  %1<br>").arg(m_result.clippedSamples);
    html += QStringLiteral("</code><br>");
    for (const QString& n : m_result.notes)
        html += QStringLiteral("• %1<br>").arg(n.toHtmlEscaped());
    m_text->setText(html);
    m_applyBtn->setEnabled(m_result.valid);
    setStage(Stage::Done);
}

void AutoVoiceDialog::applyRecommendation()
{
    if (!m_result.valid)
        return;
    emit recommendationReady(m_result.recommended);
    accept();
}

} // namespace vc
