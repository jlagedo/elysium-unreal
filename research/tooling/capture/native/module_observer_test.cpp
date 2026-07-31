#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <cwchar>

#include "module_observer.h"

namespace {

using elysium::capture::ModuleEventKind;
using elysium::capture::ModuleObserver;
using elysium::capture::ModuleObserverCounters;
using elysium::capture::ProcessedModuleEvent;

struct TestState {
    DWORD WorkerThreadId = 0;
    std::uintptr_t ClientBase = 0;
    std::uint32_t ClientSize = 0;
    LONG ClientLoads = 0;
    LONG ClientUnloads = 0;
    bool ClientHashSucceeded = false;
    bool ClientPathWasTruncated = false;
};

const wchar_t* Filename(const wchar_t* path) noexcept {
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* forwardSlash = std::wcsrchr(path, L'/');
    const wchar_t* separator = slash;
    if (forwardSlash != nullptr &&
        (separator == nullptr || forwardSlash > separator)) {
        separator = forwardSlash;
    }
    return separator == nullptr ? path : separator + 1;
}

void ProcessModule(
    const ProcessedModuleEvent& event,
    void* context) noexcept {
    auto* state = static_cast<TestState*>(context);
    const DWORD callbackThread = GetCurrentThreadId();
    if (state->WorkerThreadId == 0) {
        state->WorkerThreadId = callbackThread;
    } else if (state->WorkerThreadId != callbackThread) {
        state->WorkerThreadId = MAXDWORD;
    }
    if (_wcsicmp(Filename(event.Path), L"client.dll") != 0) {
        return;
    }

    state->ClientPathWasTruncated =
        state->ClientPathWasTruncated || event.PathTruncated;
    if (event.Kind == ModuleEventKind::Loaded) {
        ++state->ClientLoads;
        state->ClientBase = event.ImageBase;
        state->ClientSize = event.ImageSize;
        state->ClientHashSucceeded = event.HashSucceeded;
    } else if (event.Kind == ModuleEventKind::Unloaded) {
        ++state->ClientUnloads;
    }
}

int Fail(const wchar_t* message, int result) {
    std::fwprintf(stderr, L"module observer test failed: %ls\n", message);
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        return Fail(L"expected the synthetic client path", 2);
    }

    ModuleObserverCounters counters{};
    volatile LONG notifications = 0;
    TestState state{};
    ModuleObserver observer;
    DWORD errorCode = ERROR_SUCCESS;
    if (!observer.Start(
            &counters,
            &notifications,
            ProcessModule,
            &state,
            &errorCode)) {
        std::fwprintf(
            stderr,
            L"module observer startup failed: 0x%08lx\n",
            errorCode);
        return 3;
    }
    if (!observer.WaitUntilIdle(10000)) {
        return Fail(L"bootstrap enumeration did not drain", 4);
    }
    if (counters.BootstrapEventCount < 3 ||
        counters.HashSuccessCount < 3 ||
        counters.ActivationDispatchCount < 3) {
        return Fail(L"bootstrap modules were not hashed and dispatched", 5);
    }
    if (state.WorkerThreadId == 0 ||
        state.WorkerThreadId == GetCurrentThreadId() ||
        state.WorkerThreadId !=
            static_cast<DWORD>(counters.WorkerThreadId)) {
        return Fail(L"module processing did not run on the worker", 6);
    }

    HMODULE client = LoadLibraryW(argv[1]);
    if (client == nullptr) {
        std::fwprintf(
            stderr,
            L"cannot load synthetic client: %lu\n",
            GetLastError());
        return 7;
    }
    if (!observer.WaitUntilIdle(10000)) {
        FreeLibrary(client);
        return Fail(L"load event did not drain", 8);
    }
    if (state.ClientLoads != 1 ||
        state.ClientBase != reinterpret_cast<std::uintptr_t>(client) ||
        state.ClientSize == 0 ||
        !state.ClientHashSucceeded ||
        state.ClientPathWasTruncated) {
        FreeLibrary(client);
        return Fail(L"synthetic client load was not captured completely", 9);
    }

    if (!FreeLibrary(client)) {
        return Fail(L"cannot unload synthetic client", 10);
    }
    if (!observer.WaitUntilIdle(10000)) {
        return Fail(L"unload event did not drain", 11);
    }
    if (state.ClientUnloads != 1 ||
        counters.LoadEventCount < 1 ||
        counters.UnloadEventCount < 1 ||
        notifications < 2 ||
        counters.QueueDropCount != 0 ||
        counters.PathTruncationCount != 0 ||
        state.WorkerThreadId !=
            static_cast<DWORD>(counters.WorkerThreadId)) {
        return Fail(L"synthetic client unload or observer health is invalid", 12);
    }

    observer.Stop();
    std::wprintf(
        L"module-observer-test-v1 state=complete bootstrap=%ld "
        L"loads=%ld unloads=%ld processed=%ld hashes=%ld "
        L"activation_dispatches=%ld worker_thread=%lu\n",
        counters.BootstrapEventCount,
        counters.LoadEventCount,
        counters.UnloadEventCount,
        counters.ProcessedEventCount,
        counters.HashSuccessCount,
        counters.ActivationDispatchCount,
        state.WorkerThreadId);
    return 0;
}
