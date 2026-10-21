#include "StudioTab.h"
#include "MainWindow.h"
#include "Theme.h"
#include "LevelMeter.h"
#include "WaveformView.h"
#include "AutoVoiceDialog.h"
#include "../audio/AudioPipeline.h"
#include "../core/Diagnostics.h"
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

namespace vc {

StudioTab::StudioTab(MainWindow* win, QWidget* parent)
    : QWidget(parent), m_win(win)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    // ---- top row: device + transport --------------------------------
    auto* transportBox = new QGroupBox(QStringLiteral("Microphone & Transport"));
    auto* grid = new QGridLayout(transportBox);
    grid->setHorizontalSpacing(16);

    grid->addWidget(new QLabel(QStringLiteral("Input device")), 0, 0);
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setToolTip(QStringLiteral(
        "Real microphone captured by VoiceCraft Studio."));
    grid->addWidget(m_deviceCombo, 0, 1, 1, 3);

    m_startBtn = new QPushButton(QStringLiteral("START"));
    m_startBtn->setObjectName(QStringLiteral("bigStart"));
    m_startBtn->setToolTip(QStringLiteral(
        "Starts the engine and creates 'VoiceCraft Studio Microphone' "
        "for OBS."));
    grid->addWidget(m_startBtn, 1, 0, 2, 2);

    auto* midCol = new QVBoxLayout;
    m_stateLabel = new QLabel(QStringLiteral("Engine stopped"));
    m_stateLabel->setObjectName(QStringLiteral("h2"));
    m_obsLabel = new QLabel(QStringLiteral("OBS device: not created"));
    m_obsLabel->setObjectName(QStringLiteral("muted"));
    m_latencyLabel = new QLabel(QStringLiteral("Latency: —"));
    m_latencyLabel->setObjectName(QStringLiteral("mono"));
    m_xrunLabel = new QLabel(QStringLiteral("Buffer problems: 0"));
    m_xrunLabel->setObjectName(QStringLiteral("mono"));
    m_cpuLabel = new QLabel(QStringLiteral("DSP load: —"));
    m_cpuLabel->setObjectName(QStringLiteral("mono"));
    midCol->addWidget(m_stateLabel);
    midCol->addWidget(m_obsLabel);
    midCol->addWidget(m_latencyLabel);
    midCol->addWidget(m_xrunLabel);
    midCol->addWidget(m_cpuLabel);
    grid->addLayout(midCol, 1, 2, 2, 1);

    auto* rightCol = new QVBoxLayout;
    m_autoVoiceBtn = new QPushButton(QStringLiteral("AUTO STUDIO VOICE…"));
    m_autoVoiceBtn->setToolTip(QStringLiteral(
        "Records a short spoken sample (with your consent) and derives "
        "conservative starting settings. Fully offline."));
    m_repairBtn = new QPushButton(QStringLiteral("Repair Audio Connection"));
    m_repairBtn->setToolTip(QStringLiteral(
        "Audits and rebuilds the virtual microphone and streams without "
        "restarting the app or your computer."));
    rightCol->addWidget(m_autoVoiceBtn);
    rightCol->addWidget(m_repairBtn);
    rightCol->addStretch(1);
    grid->addLayout(rightCol, 1, 3, 2, 1);
    root->addWidget(transportBox);

    // ---- middle: scope + meters --------------------------------------
    auto* scopeBox = new QGroupBox(QStringLiteral("Live Signal"));
    auto* scopeLay = new QHBoxLayout(scopeBox);
    m_inMeter = new LevelMeter(LevelMeter::Vertical);
    m_inMeter->setToolTip(QStringLiteral("Input level (raw microphone)"));
    m_outMeter = new LevelMeter(LevelMeter::Vertical);
    m_outMeter->setToolTip(QStringLiteral("Output level (processed, as sent to OBS)"));
    m_scope = new WaveformView;
    m_scope->setMinimumHeight(190);
    scopeLay->addWidget(m_inMeter);
    scopeLay->addWidget(m_scope, 1);
    scopeLay->addWidget(m_outMeter);
    root->addWidget(scopeBox, 1);

    // ---- bottom: loudness + rms --------------------------------------
    auto* loudBox = new QGroupBox(QStringLiteral("Loudness & Levels (measured live)"));
    auto* loudGrid = new QGridLayout(loudBox);
    auto mkMono = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setObjectName(QStringLiteral("mono"));
        return l;
    };
    m_lufsM = mkMono("Momentary: — LUFS");
    m_lufsS = mkMono("Short-term: — LUFS");
    m_lufsI = mkMono("Integrated: — LUFS");
    m_truePeak = mkMono("True peak: — dBTP");
    m_rmsLabel = mkMono("RMS in/out: — / —");
    loudGrid->addWidget(m_lufsM, 0, 0);
    loudGrid->addWidget(m_lufsS, 0, 1);
    loudGrid->addWidget(m_lufsI, 0, 2);
    loudGrid->addWidget(m_truePeak, 1, 0);
    loudGrid->addWidget(m_rmsLabel, 1, 1, 1, 2);
    auto* note = new QLabel(QStringLiteral(
        "Measured with ITU-R BS.1770-4. Use these numbers with your "
        "platform's loudness target — no single LUFS value fits every case."));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    loudGrid->addWidget(note, 2, 0, 1, 3);
    root->addWidget(loudBox);

    // ---- wiring -------------------------------------------------------
    connect(m_startBtn, &QPushButton::clicked, this, [this]() {
        if (m_win->engine().isRunning())
            emit stopRequested();
        else
            emit startRequested();
    });
    connect(m_repairBtn, &QPushButton::clicked, this, &StudioTab::updateStatus);
    connect(m_repairBtn, &QPushButton::clicked, m_win, &MainWindow::repairAudio);
    connect(m_autoVoiceBtn, &QPushButton::clicked, m_win, &MainWindow::openAutoVoice);
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int idx) {
                const QString name = m_deviceCombo->itemData(idx).toString();
                onDeviceChosen(name);
            });
    updateStatus();
}

void StudioTab::onDeviceChosen(const QString& name)
{
    auto s = m_win->settings().get();
    s.sourceName = name;
    m_win->settings().update(s);
    if (m_win->engine().isRunning()) {
        // restart engine on the new device (clean stop removes modules)
        m_win->stopEngine();
        m_win->startEngine();
    }
}

void StudioTab::refreshDeviceList(const QVector<PaSourceInfo>& sources)
{
    const QString prev = m_deviceCombo->currentData().toString();
    m_deviceCombo->clear();
    int pick = -1;
    const QString wanted = m_win->settings().get().sourceName;
    int i = 0;
    for (const auto& s : sources) {
        if (s.name == QLatin1String(VirtualMicrophone::SOURCE_NAME))
            continue;    // never offer our own device as input
        QString label = s.description;
        if (s.monitor)
            label += QStringLiteral("   [monitor]");
        m_deviceCombo->addItem(label, s.name);
        if (s.name == wanted) pick = i;
        ++i;
    }
    if (pick < 0 && m_deviceCombo->count() > 0) {
        pick = 0;
        const QString name = m_deviceCombo->itemData(0).toString();
        auto st = m_win->settings().get();
        st.sourceName = name;
        m_win->settings().update(st);
    }
    if (pick >= 0)
        m_deviceCombo->setCurrentIndex(pick);
    Q_UNUSED(prev);
}

void StudioTab::updateStatus()
{
    const bool running = m_win->engine().isRunning();
    m_startBtn->setText(running ? QStringLiteral("STOP") : QStringLiteral("START"));
    m_startBtn->setObjectName(running ? QStringLiteral("bigStop")
                                      : QStringLiteral("bigStart"));
    // ensure the QSS reapplies to the renamed object
    m_startBtn->style()->unpolish(m_startBtn);
    m_startBtn->style()->polish(m_startBtn);

    if (running) {
        m_stateLabel->setText(QStringLiteral("Engine running"));
        m_stateLabel->setStyleSheet(
            QStringLiteral("color: %1; font-weight: 700;").arg(Theme::ACCENT_2));
        m_obsLabel->setText(QStringLiteral(
            "OBS: select input 'VoiceCraft Studio Microphone'"));
        m_obsLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::ACCENT));
    } else {
        m_stateLabel->setText(QStringLiteral("Engine stopped"));
        m_stateLabel->setStyleSheet(QStringLiteral("font-weight: 700;"));
        m_obsLabel->setText(QStringLiteral("OBS device: not created"));
        m_obsLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::MUTED));
    }
}

void StudioTab::updateMeters()
{
    Diagnostics& d = m_win->engine().diagnostics();
    m_inMeter->setValue(d.inputPeak.load(), d.inputRms.load());
    m_outMeter->setValue(d.outputPeak.load(), d.outputRms.load());

    // feed the scope with the *input* level shape; for a proper waveform we
    // sample the pipeline's tap ring via the recorder path — here we push
    // the recent peak so the scope breathes with the signal
    static float last = 0.0f;
    const float v = d.inputPeak.load();
    const float arr[1] = { last = 0.7f * last + 0.3f * v };
    m_scope->pushBlock(arr, 1, 1);

    m_latencyLabel->setText(QStringLiteral("Latency: ~%1 ms")
        .arg(d.latencyMs.load(), 0, 'f', 1));
    m_xrunLabel->setText(QStringLiteral("Buffer problems: %1")
        .arg(d.xruns.load()));
    const double cpu = d.cpuLoad.load();
    m_cpuLabel->setText(QStringLiteral("DSP load: %1% of one core")
        .arg(cpu * 100.0, 0, 'f', 1));

    m_rmsLabel->setText(QStringLiteral("RMS in/out: %1 / %2 dBFS")
        .arg(20.0 * std::log10(std::max(double(d.inputRms.load()), 1e-6)), 0, 'f', 1)
        .arg(20.0 * std::log10(std::max(double(d.outputRms.load()), 1e-6)), 0, 'f', 1));
}

void StudioTab::updateLoudness()
{
    Diagnostics& d = m_win->engine().diagnostics();
    m_lufsM->setText(QStringLiteral("Momentary: %1 LUFS")
        .arg(d.momentaryLufs.load(), 0, 'f', 1));
    m_lufsS->setText(QStringLiteral("Short-term: %1 LUFS")
        .arg(d.shortTermLufs.load(), 0, 'f', 1));
    m_lufsI->setText(QStringLiteral("Integrated: %1 LUFS")
        .arg(d.integratedLufs.load(), 0, 'f', 1));
    m_truePeak->setText(QStringLiteral("True peak: %1 dBTP")
        .arg(d.truePeakDb.load(), 0, 'f', 1));
}

} // namespace vc
