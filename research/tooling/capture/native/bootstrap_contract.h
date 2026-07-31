#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace elysium::capture {

inline constexpr std::uint32_t BootstrapMagic = 0x31484245U;  // EBH1
inline constexpr std::uint32_t BootstrapVersion = 1;

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
    DWORD ErrorCode;
    wchar_t Message[64];
};
#pragma pack(pop)

static_assert(sizeof(BootstrapHandshake) == 168);

inline std::wstring BootstrapMappingName(DWORD processId) {
    return L"Local\\ElysiumRetailBootstrap.v1." +
        std::to_wstring(processId);
}

inline std::wstring BootstrapSignalName(DWORD processId) {
    return L"Local\\ElysiumRetailBootstrap.v1." +
        std::to_wstring(processId) + L".signal";
}

}  // namespace elysium::capture
