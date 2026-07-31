#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "module_observer.h"
#include "probe_validation.h"

namespace elysium::capture {

inline constexpr std::uint32_t BootstrapMagic = 0x31484245U;  // EBH1
inline constexpr std::uint32_t BootstrapVersion = 5;
inline constexpr std::uint32_t ProbeDiagnosticCapacity = 16;

enum class BootstrapState : LONG {
    Created = 0,
    HostStarting = 1,
    Ready = 2,
    Error = 3,
};

#pragma pack(push, 4)
struct BootstrapHandshake {
    std::uint32_t Magic;
    std::uint32_t Version;
    std::uint32_t Bytes;
    volatile LONG State;
    DWORD ProcessId;
    DWORD ProbeThreadId;
    volatile LONG ModuleObserverArmed;
    volatile LONG TransportArmed;
    volatile LONG ModuleNotificationCount;
    ModuleObserverCounters ModuleObserver;
    volatile LONG BinaryProfileRegistryLoaded;
    volatile LONG BinaryProfileCount;
    volatile LONG BinaryProfileMatchCount;
    volatile LONG BinaryProfileMissCount;
    volatile LONG BinaryProfileUnloadCount;
    volatile LONG ActiveBinaryProfileCount;
    volatile LONG BinaryProfileObservedMask;
    volatile LONG BinaryProfileMatchedMask;
    volatile LONG BinaryProfileLastMismatchIndex;
    volatile LONG BinaryProfileLastMismatchFlags;
    volatile LONG ProbeValidationPassCount;
    volatile LONG ProbeHookInstallCount;
    volatile LONG ProbeDiagnosticWriteCount;
    ProbeValidationDiagnostic ProbeDiagnostics[ProbeDiagnosticCapacity];
    DWORD ErrorCode;
    wchar_t Message[64];
};
#pragma pack(pop)

static_assert(sizeof(BootstrapHandshake) == 708);

inline std::wstring BootstrapMappingName(DWORD processId) {
    return L"Local\\ElysiumRetailBootstrap.v5." +
        std::to_wstring(processId);
}

inline std::wstring BootstrapSignalName(DWORD processId) {
    return L"Local\\ElysiumRetailBootstrap.v5." +
        std::to_wstring(processId) + L".signal";
}

}  // namespace elysium::capture
