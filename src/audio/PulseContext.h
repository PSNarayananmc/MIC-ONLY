#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <pulse/pulseaudio.h>

namespace vc {

struct PaSourceInfo {
    QString name;          // pulse source name (e.g. "alsa_input.usb-...")
    QString description;   // human readable
    int     channels = 1;
    int     sampleRate = 48000;
    bool    monitor = false;  // true if this is a .monitor source
};

struct PaSinkInfo {
    QString name;
    QString description;
    int     channels = 2;
};

// Owns the pa_threaded_mainloop + pa_context. All methods are thread-safe:
// they lock the mainloop around libpulse calls. Callbacks from libpulse run
// on the PulseAudio thread; anything the UI needs is delivered via Qt queued
// signals (safe to emit cross-thread).
class PulseContext : public QObject {
    Q_OBJECT
public:
    explicit PulseContext(QObject* parent = nullptr);
    ~PulseContext() override;

    bool connectToServer();               // local server, never spawns a new one
    bool isConnected() const { return m_state == PA_CONTEXT_READY; }
    QString lastError() const { return m_lastError; }

    // Blocking helpers (with internal timeouts) — control thread only
    QVector<PaSourceInfo> listSources();
    QVector<PaSinkInfo>   listSinks();

    using ModuleList = QVector<QPair<uint32_t, QPair<QString, QString>>>; // idx, (name, args)
    ModuleList listModules();

    bool loadModule(const QString& name, const QString& args, uint32_t* indexOut,
                    int timeoutMs = 5000);
    bool unloadModule(uint32_t index, int timeoutMs = 5000);

    // give access for stream creation (callers must hold mainloop lock themselves)
    pa_threaded_mainloop* mainloop() const { return m_ml; }
    pa_context* context() const { return m_ctx; }

    // RAII lock helper around pa_threaded_mainloop
    struct Lock {
        explicit Lock(pa_threaded_mainloop* ml) : m_ml(ml) {
            if (m_ml) pa_threaded_mainloop_lock(m_ml);
        }
        ~Lock() { if (m_ml) pa_threaded_mainloop_unlock(m_ml); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        pa_threaded_mainloop* m_ml;
    };

signals:
    void connected();
    void failed(QString reason);
    void deviceListChanged();   // sources/sinks appeared or disappeared

private:
    void contextStateChanged(pa_context_state_t state);
    static void contextStateCb(pa_context* c, void* userdata);
    static void subscribeCb(pa_context* c, pa_subscription_event_type_t t,
                            uint32_t idx, void* userdata);
    static void sourceListCb(pa_context*, const pa_source_info* i, int eol, void* userdata);
    static void sinkListCb(pa_context*, const pa_sink_info* i, int eol, void* userdata);
    static void moduleListCb(pa_context*, const pa_module_info* i, int eol, void* userdata);
    static void loadCb(pa_context*, uint32_t idx, void* userdata);
    static void unloadCb(pa_context*, int success, void* userdata);
    static void opDoneCb(pa_context*, int success, void* userdata);

    bool waitForOp(pa_operation* op, int timeoutMs);
    void drainOperations();

    pa_threaded_mainloop* m_ml = nullptr;
    pa_context* m_ctx = nullptr;
    pa_context_state_t m_state = PA_CONTEXT_UNCONNECTED;
    QString m_lastError;

    // result slots for blocking queries (guarded by mainloop lock)
    QVector<PaSourceInfo> m_sourceResults;
    QVector<PaSinkInfo>   m_sinkResults;
    ModuleList            m_moduleResults;
    bool m_opDone = false;
    bool m_opSuccess = false;
    uint32_t m_loadedIndex = PA_INVALID_INDEX;
    bool m_loadCompleted = false;
};

} // namespace vc
