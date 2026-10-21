#include "SettingsTab.h"
#include "MainWindow.h"
#include "../audio/AudioPipeline.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QVBoxLayout>

namespace vc {

SettingsTab::SettingsTab(MainWindow* win, QWidget* parent)
    : QWidget(parent), m_win(win)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);

    // ---- audio engine --------------------------------------------------
    auto* audioBox = new QGroupBox(QStringLiteral("Audio Engine"));
    auto* ag = new QGridLayout(audioBox);
    ag->addWidget(new QLabel(QStringLiteral("Input device")), 0, 0);
    m_device = new QComboBox;
    m_device->setMinimumWidth(340);
    ag->addWidget(m_device, 0, 1);

    ag->addWidget(new QLabel(QStringLiteral("Sample rate")), 1, 0);
    m_rate = new QComboBox;
    m_rate->addItem(QStringLiteral("48 000 Hz (recommended — full DSP chain)"), 48000);
    m_rate->addItem(QStringLiteral("44 100 Hz (RNNoise disabled at this rate)"), 44100);
    ag->addWidget(m_rate, 1, 1);

    ag->addWidget(new QLabel(QStringLiteral("Block size")), 2, 0);
    m_block = new QComboBox;
    m_block->addItem(QStringLiteral("480 frames (10 ms — recommended)"), 480);
    m_block->addItem(QStringLiteral("256 frames (5.3 ms — lower latency, more CPU)"), 256);
    m_block->addItem(QStringLiteral("960 frames (20 ms — lowest CPU)"), 960);
    ag->addWidget(m_block, 2, 1);

    m_lowCpu = new QCheckBox(QStringLiteral(
        "Low-power CPU mode (J1800-class: single-channel RNNoise, lighter limiter)"));
    ag->addWidget(m_lowCpu, 3, 0, 1, 2);
    m_rtThread = new QCheckBox(QStringLiteral(
        "Try to elevate the DSP thread to SCHED_FIFO real-time priority"));
    ag->addWidget(m_rtThread, 4, 0, 1, 2);

    auto* safety = new QLabel(QStringLiteral(
        "Safety: VoiceCraft never changes your system default microphone or "
        "speakers. It only creates its own devices and routes its own audio. "
        "If the microphone disappears, the engine rebinds automatically; use "
        "'Repair Audio Connection' on the Studio tab for manual recovery."));
    safety->setObjectName(QStringLiteral("muted"));
    safety->setWordWrap(true);
    ag->addWidget(safety, 5, 0, 1, 2);
    root->addWidget(audioBox);

    // ---- recording ---------------------------------------------------------
    auto* recBox = new QGroupBox(QStringLiteral("Recording"));
    auto* rg = new QGridLayout(recBox);
    rg->addWidget(new QLabel(QStringLiteral("Format")), 0, 0);
    m_recFormat = new QComboBox;
    m_recFormat->addItem(QStringLiteral("FLAC — lossless, compressed (recommended)"), QStringLiteral("FLAC"));
    m_recFormat->addItem(QStringLiteral("WAV — lossless, 24-bit"), QStringLiteral("WAV"));
    rg->addWidget(m_recFormat, 0, 1);
    m_recMono = new QCheckBox(QStringLiteral("Export mono (voice work)"));
    rg->addWidget(m_recMono, 1, 0, 1, 2);
    rg->addWidget(new QLabel(QStringLiteral("Folder")), 2, 0);
    m_recDir = new QLabel;
    m_recDir->setObjectName(QStringLiteral("mono"));
    rg->addWidget(m_recDir, 2, 1);
    auto* pickDir = new QPushButton(QStringLiteral("Choose folder…"));
    rg->addWidget(pickDir, 2, 2);
    connect(pickDir, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Recordings folder"), m_recDir->text());
        if (!dir.isEmpty()) {
            m_recDir->setText(dir);
            applySettings();
        }
    });
    root->addWidget(recBox);

    // ---- diagnostics / paths -------------------------------------------------
    auto* aboutBox = new QGroupBox(QStringLiteral("Diagnostics & Storage"));
    auto* bg = new QGridLayout(aboutBox);
    m_paths = new QLabel;
    m_paths->setObjectName(QStringLiteral("mono"));
    m_paths->setWordWrap(true);
    m_paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bg->addWidget(m_paths, 0, 0, 1, 2);
    auto* logsBtn = new QPushButton(QStringLiteral("Open log folder"));
    connect(logsBtn, &QPushButton::clicked, this, []() {
        // ~/.local/share/VoiceCraftStudio/logs — opened by the file manager
        QProcess::startDetached(QStringLiteral("xdg-open"),
            { QStringLiteral("%1/.local/share/VoiceCraftStudio/logs")
                  .arg(QString::fromLocal8Bit(qgetenv("HOME"))) });
    });
    bg->addWidget(logsBtn, 1, 0);
    root->addWidget(aboutBox);

    root->addStretch(1);

    connect(m_device, QOverload<int>::of(&QComboBox::activated), this, [this](int) { applySettings(); });
    connect(m_rate, QOverload<int>::of(&QComboBox::activated), this, [this](int) { applySettings(); });
    connect(m_block, QOverload<int>::of(&QComboBox::activated), this, [this](int) { applySettings(); });
    connect(m_lowCpu, &QCheckBox::toggled, this, [this](bool) { applySettings(); });
    connect(m_rtThread, &QCheckBox::toggled, this, [this](bool) { applySettings(); });
    connect(m_recFormat, QOverload<int>::of(&QComboBox::activated), this, [this](int) { applySettings(); });
    connect(m_recMono, &QCheckBox::toggled, this, [this](bool) { applySettings(); });

    // load current values
    const auto s = m_win->settings().get();
    m_rate->setCurrentIndex(m_rate->findData(s.sampleRate) < 0 ? 0 : m_rate->findData(s.sampleRate));
    m_block->setCurrentIndex(m_block->findData(s.blockSize) < 0 ? 0 : m_block->findData(s.blockSize));
    m_lowCpu->setChecked(s.lowCpuMode);
    m_rtThread->setChecked(s.rtAudioThread);
    const int fmtIdx = m_recFormat->findData(s.recordingFormat);
    m_recFormat->setCurrentIndex(fmtIdx < 0 ? 0 : fmtIdx);
    m_recMono->setChecked(s.recordingMono);
    m_recDir->setText(s.recordingDir);
    m_paths->setText(QStringLiteral(
        "settings:  %1\nlogs:      ~/.local/share/VoiceCraftStudio/logs\n"
        "presets:   %2").arg(m_win->settings().settingsFilePath(),
                            m_win->presets().presetsDir()));
    refreshDeviceList();
}

void SettingsTab::refreshDeviceList()
{
    const QString wanted = m_win->settings().get().sourceName;
    m_device->clear();
    m_device->addItem(QStringLiteral("System default input"), QString());
    int pick = wanted.isEmpty() ? 0 : -1;
    const auto sources = m_win->engine().inputSources();
    for (const auto& src : sources) {
        if (src.name == QLatin1String(VirtualMicrophone::SOURCE_NAME))
            continue;
        m_device->addItem(src.description + (src.monitor ? QStringLiteral("  [monitor]")
                                                         : QString()),
                          src.name);
        if (src.name == wanted)
            pick = m_device->count() - 1;
    }
    if (pick >= 0)
        m_device->setCurrentIndex(pick);
}

void SettingsTab::applySettings()
{
    auto s = m_win->settings().get();
    s.sourceName = m_device->currentData().toString();
    s.sampleRate = m_rate->currentData().toInt();
    s.blockSize = m_block->currentData().toInt();
    s.lowCpuMode = m_lowCpu->isChecked();
    s.rtAudioThread = m_rtThread->isChecked();
    s.recordingFormat = m_recFormat->currentData().toString();
    s.recordingMono = m_recMono->isChecked();
    s.recordingDir = m_recDir->text();
    m_win->settings().update(s);

    // rate/block/lowCpu changes require an engine restart to take effect
    m_win->pushStatus(QStringLiteral("Settings saved. Restart the engine "
        "for audio-engine changes to take effect."));
}

} // namespace vc
