#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "binary_profile_contract.h"

namespace elysium::capture {

enum class ModuleEventKind : std::uint32_t {
    Bootstrap = 1,
    Loaded = 2,
    Unloaded = 3,
};

struct ModuleObserverCounters {
    volatile LONG BootstrapEventCount;
    volatile LONG LoadEventCount;
    volatile LONG UnloadEventCount;
    volatile LONG ProcessedEventCount;
    volatile LONG HashSuccessCount;
    volatile LONG HashFailureCount;
    volatile LONG ActivationDispatchCount;
    volatile LONG QueueDropCount;
    volatile LONG PathTruncationCount;
    volatile LONG WorkerThreadId;
};

struct ProcessedModuleEvent {
    ModuleEventKind Kind;
    std::uintptr_t ImageBase;
    std::uint32_t ImageSize;
    std::uint64_t FileSize;
    PeIdentity Pe;
    const wchar_t* ModuleName;
    std::size_t ModuleNameCharacters;
    const wchar_t* Path;
    std::size_t PathCharacters;
    bool PathTruncated;
    bool Duplicate;
    bool HashSucceeded;
    bool IdentitySucceeded;
    std::array<std::uint8_t, 32> Sha256;
};

using ModuleEventProcessor = void (*)(
    const ProcessedModuleEvent& event,
    void* context) noexcept;

class ModuleObserver {
public:
    ModuleObserver() noexcept = default;
    ~ModuleObserver();

    ModuleObserver(const ModuleObserver&) = delete;
    ModuleObserver& operator=(const ModuleObserver&) = delete;

    bool Start(
        ModuleObserverCounters* counters,
        volatile LONG* notificationCount,
        ModuleEventProcessor processor,
        void* processorContext,
        DWORD* errorCode) noexcept;
    bool WaitUntilIdle(DWORD timeoutMilliseconds) const noexcept;
    void RequestStop() noexcept;
    void Stop() noexcept;

private:
    struct Impl;
    Impl* Implementation_ = nullptr;
};

}  // namespace elysium::capture
