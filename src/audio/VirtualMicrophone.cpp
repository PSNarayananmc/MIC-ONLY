#include "VirtualMicrophone.h"
#include "../core/Log.h"

namespace vc {

bool VirtualMicrophone::create(int channels, int sampleRate)
{
    Q_UNUSED(sampleRate);
    if (!m_pa.isConnected())
        return false;

    // 1) null sink: our processed audio lands here; OBS never hears anything
    //    else. channel_map keeps the device mono/stereo to match our stream
    //    so the server performs no remixing.
    QString chmap = QStringLiteral("front-left,front-right");
    if (channels == 1)
        chmap = QStringLiteral("mono");

    // NOTE: the description contains spaces. pa_modargs consumes the outer
    // double quotes around the WHOLE properties list, and the proplist
    // parser needs the value single-quoted inside — the canonical form is
    //   sink_properties="device.description='VoiceCraft Studio Microphone'"
    // Without this quoting the extra words are stray tokens and the module
    // refuses to load.
    const QString sinkArgs = QStringLiteral(
        "sink_name=%1 channels=%2 channel_map=%3 "
        "sink_properties=\"device.description='%4'\""
        ).arg(SINK_NAME)
         .arg(channels)
         .arg(chmap)
         .arg(QString::fromUtf8(DESCRIPTION));

    uint32_t idx = PA_INVALID_INDEX;
    if (!m_pa.loadModule(QStringLiteral("module-null-sink"), sinkArgs, &idx))
        return false;
    m_sinkModule = idx;

    // 2) virtual source uplinking the monitor of our sink: this is the device
    //    OBS lists as an input microphone.
    const QString srcArgs = QStringLiteral(
        "source_name=%1 master=%2.monitor "
        "source_properties=\"device.description='%3'\"")
        .arg(SOURCE_NAME)
        .arg(SINK_NAME)
        .arg(QString::fromUtf8(DESCRIPTION));

    if (!m_pa.loadModule(QStringLiteral("module-virtual-source"), srcArgs, &idx)) {
        // roll back the sink so we never leave half-built devices behind
        m_pa.unloadModule(m_sinkModule);
        m_sinkModule = PA_INVALID_INDEX;
        return false;
    }
    m_sourceModule = idx;

    VCINFO(QStringLiteral("Virtual microphone created (sink module %1, source module %2)")
           .arg(m_sinkModule).arg(m_sourceModule));
    return true;
}

bool VirtualMicrophone::destroy()
{
    bool ok = true;
    if (m_sinkModule != PA_INVALID_INDEX)
        ok &= m_pa.unloadModule(m_sinkModule);
    if (m_sourceModule != PA_INVALID_INDEX)
        ok &= m_pa.unloadModule(m_sourceModule);
    m_sinkModule = PA_INVALID_INDEX;
    m_sourceModule = PA_INVALID_INDEX;

    // belt & braces: remove anything left over with our signature
    auditAndClean(true);
    return ok;
}

VirtualMicrophone::AuditResult VirtualMicrophone::auditAndClean(bool removeStrays)
{
    AuditResult r;
    const PulseContext::ModuleList modules = m_pa.listModules();
    for (const auto& m : modules) {
        const QString& name = m.second.first;
        const QString& args = m.second.second;
        const bool oursSink = name == QStringLiteral("module-null-sink")
                              && args.contains(QStringLiteral("sink_name=") + SINK_NAME);
        const bool oursSource = name == QStringLiteral("module-virtual-source")
                                && args.contains(QStringLiteral("source_name=") + SOURCE_NAME);
        if (oursSink) {
            ++r.sinkModules;
            if (removeStrays && m.first != m_sinkModule)
                m_pa.unloadModule(m.first);
        }
        if (oursSource) {
            ++r.sourceModules;
            if (removeStrays && m.first != m_sourceModule)
                m_pa.unloadModule(m.first);
        }
    }
    r.clean = (r.sinkModules <= 1 && r.sourceModules <= 1);
    r.report = QStringLiteral("sink modules: %1, source modules: %2")
                   .arg(r.sinkModules).arg(r.sourceModules);
    return r;
}

bool VirtualMicrophone::exists()
{
    if (m_sinkModule == PA_INVALID_INDEX || m_sourceModule == PA_INVALID_INDEX)
        return false;
    // confirm both modules are still loaded in the server
    const PulseContext::ModuleList modules = m_pa.listModules();
    bool sink = false, source = false;
    for (const auto& m : modules) {
        if (m.first == m_sinkModule) sink = true;
        if (m.first == m_sourceModule) source = true;
    }
    return sink && source;
}

bool VirtualMicrophone::sourceVisibleToClients()
{
    const QVector<PaSourceInfo> sources = m_pa.listSources();
    for (const auto& s : sources)
        if (s.name == QLatin1String(SOURCE_NAME))
            return true;
    return false;
}

} // namespace vc
