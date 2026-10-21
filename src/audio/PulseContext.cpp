#include "PulseContext.h"
#include "../core/Log.h"
#include <QDeadlineTimer>
#include <QThread>

namespace vc {

PulseContext::PulseContext(QObject* parent)
    : QObject(parent)
{
}

PulseContext::~PulseContext()
{
    if (m_ctx) {
        {
            Lock lock(m_ml);
            pa_context_disconnect(m_ctx);
            pa_context_unref(m_ctx);
            m_ctx = nullptr;
        }
    }
    if (m_ml) {
        pa_threaded_mainloop_stop(m_ml);
        pa_threaded_mainloop_free(m_ml);
        m_ml = nullptr;
    }
}

bool PulseContext::connectToServer()
{
    if (m_ml)
        return isConnected();

    m_ml = pa_threaded_mainloop_new();
    if (!m_ml) {
        m_lastError = QStringLiteral("cannot create PulseAudio mainloop");
        return false;
    }
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(m_ml);
    m_ctx = pa_context_new(api, QStringLiteral("VoiceCraft Studio").toUtf8().constData());
    if (!m_ctx) {
        m_lastError = QStringLiteral("cannot create PulseAudio context");
        return false;
    }
    pa_context_set_state_callback(m_ctx, &PulseContext::contextStateCb, this);
    pa_context_set_subscribe_callback(m_ctx, &PulseContext::subscribeCb, this);

    if (pa_threaded_mainloop_start(m_ml) < 0) {
        m_lastError = QStringLiteral("cannot start PulseAudio mainloop thread");
        return false;
    }

    {
        Lock lock(m_ml);
        // Never spawn a private daemon: we integrate with the user's session.
        const pa_context_flags_t flags = static_cast<pa_context_flags_t>(
            PA_CONTEXT_NOFAIL | PA_CONTEXT_NOAUTOSPAWN);
        if (pa_context_connect(m_ctx, nullptr, flags, nullptr) < 0) {
            m_lastError = QString::fromUtf8(pa_strerror(pa_context_errno(m_ctx)));
            emit failed(m_lastError);
            return false;
        }
        // Wait up to 5 s for READY / FAILED
        const QDeadlineTimer deadline(5000);
        while (m_state != PA_CONTEXT_READY && m_state != PA_CONTEXT_FAILED
               && !deadline.hasExpired()) {
            pa_threaded_mainloop_wait(m_ml);
        }
    }

    if (m_state != PA_CONTEXT_READY) {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("PulseAudio connection failed or timed out "
                                         "(is pulseaudio running?)");
        emit failed(m_lastError);
        return false;
    }

    {
        Lock lock(m_ml);
        pa_operation* o = pa_context_subscribe(m_ctx,
            static_cast<pa_subscription_mask_t>(
                PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SINK |
                PA_SUBSCRIPTION_MASK_MODULE),
            &PulseContext::opDoneCb, this);
        if (o) pa_operation_unref(o);
    }

    emit connected();
    VCINFO(QStringLiteral("PulseAudio connected"));
    return true;
}

void PulseContext::contextStateCb(pa_context* c, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    self->m_state = pa_context_get_state(c);
    switch (self->m_state) {
    case PA_CONTEXT_READY:
        if (pa_context_errno(c) == 0)
            self->m_lastError.clear();
        break;
    case PA_CONTEXT_FAILED: {
        const int err = pa_context_errno(c);
        self->m_lastError = QString::fromUtf8(pa_strerror(err));
        if (err == -PA_ERR_CONNECTIONREFUSED)
            self->m_lastError = QStringLiteral(
                "Cannot reach PulseAudio (connection refused)");
        emit self->failed(self->m_lastError);
        break;
    }
    case PA_CONTEXT_TERMINATED:
        self->m_lastError = QStringLiteral("PulseAudio connection terminated");
        break;
    default:
        break;
    }
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::subscribeCb(pa_context*, pa_subscription_event_type_t t,
                               uint32_t idx, void* userdata)
{
    Q_UNUSED(idx);
    const int facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SOURCE ||
        facility == PA_SUBSCRIPTION_EVENT_SINK ||
        facility == PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT ||
        facility == PA_SUBSCRIPTION_EVENT_MODULE) {
        auto* self = static_cast<PulseContext*>(userdata);
        emit self->deviceListChanged();
    }
}

void PulseContext::sourceListCb(pa_context*, const pa_source_info* i, int eol, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    if (!eol && i) {
        PaSourceInfo info;
        info.name = QString::fromUtf8(i->name);
        info.description = QString::fromUtf8(i->description);
        info.channels = i->sample_spec.channels;
        info.sampleRate = i->sample_spec.rate;
        // a monitor source is one attached to a sink (i->monitor_of_sink);
        // module-virtual-source devices are NOT monitors even though they
        // are non-hardware — OBS sees them as ordinary microphones
        info.monitor = (i->monitor_of_sink != PA_INVALID_INDEX);
        self->m_sourceResults.push_back(info);
    }
    self->m_opDone = true;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::sinkListCb(pa_context*, const pa_sink_info* i, int eol, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    if (!eol && i) {
        PaSinkInfo info;
        info.name = QString::fromUtf8(i->name);
        info.description = QString::fromUtf8(i->description);
        info.channels = i->sample_spec.channels;
        self->m_sinkResults.push_back(info);
    }
    self->m_opDone = true;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::moduleListCb(pa_context*, const pa_module_info* i, int eol, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    if (!eol && i) {
        self->m_moduleResults.push_back({i->index, {
            QString::fromUtf8(i->name),
            QString::fromUtf8(i->argument ? i->argument : "") }});
    }
    self->m_opDone = true;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::loadCb(pa_context*, uint32_t idx, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    self->m_loadedIndex = idx;
    self->m_loadCompleted = true;
    self->m_opDone = true;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::unloadCb(pa_context*, int success, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    self->m_opSuccess = success != 0;
    self->m_opDone = true;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

void PulseContext::opDoneCb(pa_context*, int success, void* userdata)
{
    auto* self = static_cast<PulseContext*>(userdata);
    self->m_opDone = true;
    self->m_opSuccess = success != 0;
    pa_threaded_mainloop_signal(self->m_ml, 0);
}

bool PulseContext::waitForOp(pa_operation* op, int timeoutMs)
{
    if (!op) {
        m_lastError = QStringLiteral("PulseAudio operation could not be created");
        return false;
    }
    const QDeadlineTimer deadline(timeoutMs);
    while (!m_opDone && !deadline.hasExpired() && m_state == PA_CONTEXT_READY)
        pa_threaded_mainloop_wait(m_ml);
    pa_operation_unref(op);
    return m_opDone;
}

QVector<PaSourceInfo> PulseContext::listSources()
{
    m_sourceResults.clear();
    if (!isConnected())
        return m_sourceResults;
    Lock lock(m_ml);
    m_opDone = false;
    pa_operation* o = pa_context_get_source_info_list(m_ctx, &sourceListCb, this);
    waitForOp(o, 5000);
    return m_sourceResults;
}

QVector<PaSinkInfo> PulseContext::listSinks()
{
    m_sinkResults.clear();
    if (!isConnected())
        return m_sinkResults;
    Lock lock(m_ml);
    m_opDone = false;
    pa_operation* o = pa_context_get_sink_info_list(m_ctx, &sinkListCb, this);
    waitForOp(o, 5000);
    return m_sinkResults;
}

PulseContext::ModuleList PulseContext::listModules()
{
    m_moduleResults.clear();
    if (!isConnected())
        return m_moduleResults;
    Lock lock(m_ml);
    m_opDone = false;
    pa_operation* o = pa_context_get_module_info_list(m_ctx, &moduleListCb, this);
    waitForOp(o, 5000);
    return m_moduleResults;
}

bool PulseContext::loadModule(const QString& name, const QString& args,
                              uint32_t* indexOut, int timeoutMs)
{
    if (!isConnected()) {
        m_lastError = QStringLiteral("not connected to PulseAudio");
        return false;
    }
    Lock lock(m_ml);
    m_opDone = false;
    m_loadCompleted = false;
    m_loadedIndex = PA_INVALID_INDEX;
    pa_operation* o = pa_context_load_module(m_ctx, name.toUtf8().constData(),
                                             args.toUtf8().constData(), &loadCb, this);
    waitForOp(o, timeoutMs);
    if (!m_loadCompleted) {
        m_lastError = QStringLiteral("module %1 failed to load (timeout)").arg(name);
        return false;
    }
    if (m_loadedIndex == PA_INVALID_INDEX) {
        m_lastError = QStringLiteral("module %1 refused: %2")
            .arg(name, QString::fromUtf8(pa_strerror(pa_context_errno(m_ctx))));
        return false;
    }
    if (indexOut)
        *indexOut = m_loadedIndex;
    return true;
}

bool PulseContext::unloadModule(uint32_t index, int timeoutMs)
{
    if (!isConnected() || index == PA_INVALID_INDEX)
        return false;
    Lock lock(m_ml);
    m_opDone = false;
    m_opSuccess = false;
    pa_operation* o = pa_context_unload_module(m_ctx, index, &unloadCb, this);
    waitForOp(o, timeoutMs);
    if (!m_opSuccess) {
        m_lastError = QStringLiteral("failed to unload module %1: %2")
            .arg(index).arg(QString::fromUtf8(pa_strerror(pa_context_errno(m_ctx))));
    }
    return m_opSuccess;
}

} // namespace vc
