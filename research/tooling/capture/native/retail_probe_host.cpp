#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>
#include <new>

#include "bootstrap_contract.h"
#include "module_observer.h"

namespace {

using elysium::capture::BootstrapHandshake;
using elysium::capture::BootstrapMagic;
using elysium::capture::BootstrapState;
using elysium::capture::BootstrapVersion;
using elysium::capture::ModuleObserver;
using elysium::capture::ProcessedModuleEvent;

HANDLE BootstrapMapping = nullptr;
BootstrapHandshake* Handshake = nullptr;
ModuleObserver* Observer = nullptr;

void CopyMessage(BootstrapHandshake* handshake, const wchar_t* message) {
    wcsncpy_s(
        handshake->Message,
        _countof(handshake->Message),
        message,
        _TRUNCATE);
}

void SetError(
    BootstrapHandshake* handshake,
    HANDLE signal,
    DWORD errorCode,
    const wchar_t* message) {
    handshake->ErrorCode = errorCode;
    CopyMessage(handshake, message);
    InterlockedExchange(
        &handshake->State,
        static_cast<LONG>(BootstrapState::Error));
    SetEvent(signal);
}

void ActivateProfiledProbes(
    const ProcessedModuleEvent&,
    void*) noexcept {
    // CAP2.2 supplies the exact-binary profile registry. CAP2.1 owns this
    // worker-thread activation seam and invokes it only after SHA-256 succeeds.
}

DWORD WINAPI BootstrapWorker(void*) {
    const DWORD processId = GetCurrentProcessId();
    const std::wstring mappingName =
        elysium::capture::BootstrapMappingName(processId);
    const std::wstring signalName =
        elysium::capture::BootstrapSignalName(processId);
    HANDLE signal = OpenEventW(EVENT_MODIFY_STATE, FALSE, signalName.c_str());
    if (signal == nullptr) {
        return 1;
    }

    BootstrapMapping = OpenFileMappingW(
        FILE_MAP_READ | FILE_MAP_WRITE,
        FALSE,
        mappingName.c_str());
    if (BootstrapMapping == nullptr) {
        SetEvent(signal);
        CloseHandle(signal);
        return 2;
    }
    Handshake = static_cast<BootstrapHandshake*>(
        MapViewOfFile(
            BootstrapMapping,
            FILE_MAP_READ | FILE_MAP_WRITE,
            0,
            0,
            sizeof(BootstrapHandshake)));
    if (Handshake == nullptr) {
        SetEvent(signal);
        CloseHandle(signal);
        return 3;
    }
    if (Handshake->Magic != BootstrapMagic ||
        Handshake->Version != BootstrapVersion ||
        Handshake->Bytes != sizeof(BootstrapHandshake) ||
        Handshake->ProcessId != processId) {
        SetError(
            Handshake,
            signal,
            ERROR_REVISION_MISMATCH,
            L"bootstrap handshake contract mismatch");
        CloseHandle(signal);
        return 4;
    }

    Handshake->ProbeThreadId = GetCurrentThreadId();
    InterlockedExchange(
        &Handshake->State,
        static_cast<LONG>(BootstrapState::HostStarting));
    InterlockedExchange(&Handshake->TransportArmed, 1);

    Observer = new (std::nothrow) ModuleObserver();
    if (Observer == nullptr) {
        SetError(
            Handshake,
            signal,
            ERROR_OUTOFMEMORY,
            L"cannot allocate module observer");
        CloseHandle(signal);
        return 5;
    }
    DWORD observerError = ERROR_SUCCESS;
    if (!Observer->Start(
            &Handshake->ModuleObserver,
            &Handshake->ModuleNotificationCount,
            ActivateProfiledProbes,
            nullptr,
            &observerError)) {
        SetError(
            Handshake,
            signal,
            observerError,
            L"module observer startup failed");
        delete Observer;
        Observer = nullptr;
        CloseHandle(signal);
        return 6;
    }
    if (!Observer->WaitUntilIdle(25000)) {
        SetError(
            Handshake,
            signal,
            ERROR_TIMEOUT,
            L"bootstrap module processing timed out");
        Observer->Stop();
        delete Observer;
        Observer = nullptr;
        CloseHandle(signal);
        return 7;
    }

    InterlockedExchange(&Handshake->ModuleObserverArmed, 1);
    CopyMessage(Handshake, L"probe host ready");
    InterlockedExchange(
        &Handshake->State,
        static_cast<LONG>(BootstrapState::Ready));
    SetEvent(signal);
    CloseHandle(signal);
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(instance);
    HANDLE worker = CreateThread(
        nullptr,
        0,
        BootstrapWorker,
        nullptr,
        0,
        nullptr);
    if (worker == nullptr) {
        return FALSE;
    }
    CloseHandle(worker);
    return TRUE;
}
