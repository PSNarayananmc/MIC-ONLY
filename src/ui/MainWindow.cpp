#include "MainWindow.h"
#include "Theme.h"
#include "StudioTab.h"
#include "RecordingTab.h"
#include "MixerTab.h"
#include "PresetsTab.h"
#include "SettingsTab.h"
#include "AutoVoiceDialog.h"
#include "../core/Log.h"
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace vc {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("VoiceCraft Studio"));
    resize(1180, 760);
    setMinimumSize(980, 640);

    m_presets.loadAll();
    const auto s = m_settings.get();
    if (const Preset* p = m_presets.find(s.lastPreset))
        m_params = p->params;
    else
        m_params = m_presets.builtin(QStringLiteral("Podcast Studio")).params;

    buildUi();
    wireConnections();

    m_pollTimer.setInterval(33);   // ~30 fps meters
    m_pollTimer.start();

    if (s.startWithEngine)
        QTimer::singleShot(400, this, &MainWindow::startEngine);
}

MainWindow::~MainWindow()
{
    setRecordingTapEnabled(false);
    m_pipeline.stop();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabWidget(central);
    m_studioTab = new StudioTab(this);
    m_recordingTab = new RecordingTab(this);
    m_mixerTab = new MixerTab(this);
    m_presetsTab = new PresetsTab(this);
    m_settingsTab = new SettingsTab(this);

    m_tabs->addTab(m_studioTab, QStringLiteral("Studio"));
    m_tabs->addTab(m_mixerTab, QStringLiteral("Mixer"));
    m_tabs->addTab(m_presetsTab, QStringLiteral("Presets"));
    m_tabs->addTab(m_recordingTab, QStringLiteral("Recording"));
    m_tabs->addTab(m_settingsTab, QStringLiteral("Settings"));
    root->addWidget(m_tabs, 1);
    setCentralWidget(central);

    m_status = new QLabel(QStringLiteral("Ready"));
    statusBar()->addWidget(m_status, 1);
    m_statusRight = new QLabel;
    statusBar()->addPermanentWidget(m_statusRight);

    m_mixerTab->setParams(m_params);
    refreshDevices();
}

void MainWindow::wireConnections()
{
    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::poll);
    connect(&m_pipeline, &AudioPipeline::started,
            this, [this]() {
        m_studioTab->updateStatus();
        pushStatus(QStringLiteral("Engine running — 'VoiceCraft Studio "
            "Microphone' is available to OBS."), false);
    });
    connect(&m_pipeline, &AudioPipeline::stopped,
            this, [this]() { m_studioTab->updateStatus(); });
    connect(&m_pipeline, &AudioPipeline::error, this, &MainWindow::onPipelineError);
    connect(&m_pipeline, &AudioPipeline::devicesChanged, this,
            &MainWindow::refreshDevices, Qt::QueuedConnection);
    connect(&m_pipeline, &AudioPipeline::sourceLost, this, &MainWindow::onSourceLost);

    connect(m_studioTab, &StudioTab::startRequested,
            this, &MainWindow::startEngine);
    connect(m_studioTab, &StudioTab::stopRequested,
            this, &MainWindow::stopEngine);

    connect(m_mixerTab, &MixerTab::paramsEdited, this, &MainWindow::publishParams);
}

void MainWindow::poll()
{
    m_studioTab->updateMeters();
    m_studioTab->updateLoudness();
    m_mixerTab->updateMeters();
    m_recordingTab->tickRecordingTime();
    const auto& d = m_pipeline.diagnostics();
    m_statusRight->setText(m_pipeline.isRunning()
        ? QStringLiteral("48 kHz · %1 ms · xrun %2")
              .arg(d.latencyMs.load(), 0, 'f', 0).arg(d.xruns.load())
        : QString());
}

void MainWindow::publishParams(const DspParams& p)
{
    m_params = p;
    m_pipeline.publishParams(p);
}

void MainWindow::applyPresetByName(const QString& name)
{
    const Preset* p = m_presets.find(name);
    if (!p)
        return;
    m_params = p->params;
    m_pipeline.publishParams(m_params);
    m_mixerTab->setParams(m_params);

    auto s = m_settings.get();
    if (s.rememberLastPreset) {
        s.lastPreset = name;
        m_settings.update(s);
    }
    pushStatus(QStringLiteral("Preset applied: %1").arg(name));
}

void MainWindow::pushStatus(const QString& text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError
        ? QStringLiteral("color: %1;").arg(Theme::RED)
        : QString());
    VCINFO(text);
}

void MainWindow::startEngine()
{
    if (!m_pipeline.start(m_settings.get())) {
        if (m_pipeline.lastError().isEmpty())
            return;
        QMessageBox::warning(this, QStringLiteral("Engine"),
                             QStringLiteral("Could not start:\n\n%1")
                                 .arg(m_pipeline.lastError()));
        m_studioTab->updateStatus();
        return;
    }
    m_pipeline.publishParams(m_params);
    m_studioTab->updateStatus();
}

void MainWindow::stopEngine()
{
    m_pipeline.stop();
    m_studioTab->updateStatus();
}

void MainWindow::repairAudio()
{
    const QString report = m_pipeline.repair();
    pushStatus(QStringLiteral("Repair: %1").arg(report),
               report.contains(QStringLiteral("Could not"), Qt::CaseInsensitive));
    m_studioTab->updateStatus();
    refreshDevices();
}

void MainWindow::onPipelineError(const QString& msg)
{
    pushStatus(msg, true);
    // automatic recovery attempt: mic disappeared or stream failed
    QTimer::singleShot(1200, this, &MainWindow::repairAudio);
}

void MainWindow::onSourceLost()
{
    pushStatus(QStringLiteral("Input device lost — attempting automatic "
                              "recovery…"), true);
    QTimer::singleShot(1500, this, &MainWindow::repairAudio);
}

void MainWindow::refreshDevices()
{
    m_studioTab->refreshDeviceList(m_pipeline.inputSources());
    m_settingsTab->refreshDeviceList();
    if (m_pipeline.isRunning() && !m_pipeline.virtualMic().exists())
        onSourceLost();
}

void MainWindow::setRecordingTapEnabled(bool on)
{
    m_pipeline.setRecordingTapEnabled(on ? &m_recorder : nullptr);
}

void MainWindow::loadDocumentFromFile(const QString& path)
{
    QString err;
    if (m_doc.load(path, &err)) {
        m_recordingTab->refreshWaveform();
        pushStatus(QStringLiteral("Loaded %1").arg(path));
    } else {
        pushStatus(QStringLiteral("Load failed: %1").arg(err), true);
    }
}

void MainWindow::openAutoVoice()
{
    if (!m_autoDialog) {
        m_autoDialog = std::make_unique<AutoVoiceDialog>(this, this);
        connect(m_autoDialog.get(), &AutoVoiceDialog::recommendationReady,
                this, [this](const DspParams& p) {
            m_params = p;
            m_pipeline.publishParams(p);
            m_mixerTab->setParams(p);
            pushStatus(QStringLiteral("AUTO STUDIO VOICE settings applied "
                                      "(conservative starting point)."));
        });
    }
    m_autoDialog->exec();
}

} // namespace vc
