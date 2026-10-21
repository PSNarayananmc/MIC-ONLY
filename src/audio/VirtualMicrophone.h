#pragma once
#include "PulseContext.h"
#include <QString>
#include <atomic>
#include <vector>

namespace vc {

// Creates and owns the PulseAudio plumbing that gives OBS a real selectable
// microphone device called "VoiceCraft Studio Microphone":
//
//   our processed playback stream --> module-null-sink "VoiceCraft_Output"
//                                 --> .monitor source
//                                 --> module-virtual-source
//                                     "VoiceCraft_Studio_Microphone" --> OBS
//
// Modules are loaded with the native libpulse API (no pactl, no shell),
// which makes the operation injection-safe and desktop-agnostic.
class VirtualMicrophone : public QObject {
    Q_OBJECT
public:
    static inline const char* SINK_NAME    = "VoiceCraft_Output";
    static inline const char* SOURCE_NAME  = "VoiceCraft_Studio_Microphone";
    static inline const char* DESCRIPTION  = "VoiceCraft Studio Microphone";

    explicit VirtualMicrophone(PulseContext& pa, QObject* parent = nullptr)
        : QObject(parent), m_pa(pa) {}
    ~VirtualMicrophone() override { destroy(); }

    // Create sink + virtual source. Returns false with a reason on failure.
    bool create(int channels, int sampleRate);

    // Remove our modules; verifies nothing with our signature remains.
    bool destroy();

    // Audit module list: unload strays / duplicates with our signature and
    // report whether the expected pair exists. Used by "Repair Audio
    // Connection" and by start/stop cycles.
    struct AuditResult {
        int sinkModules = 0;
        int sourceModules = 0;
        bool clean = false;
        QString report;
    };
    AuditResult auditAndClean(bool removeStrays);

    bool exists();                       // both modules present?
    bool sourceVisibleToClients();       // source listed by the server?

    uint32_t sinkModuleIndex() const { return m_sinkModule; }
    uint32_t sourceModuleIndex() const { return m_sourceModule; }

private:
    PulseContext& m_pa;
    uint32_t m_sinkModule = PA_INVALID_INDEX;
    uint32_t m_sourceModule = PA_INVALID_INDEX;
};

} // namespace vc
