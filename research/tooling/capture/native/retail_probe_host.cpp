#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

#include "bootstrap_contract.h"

namespace {

using elysium::capture::BootstrapHandshake;
using elysium::capture::BootstrapMagic;
using elysium::capture::BootstrapState;
using elysium::capture::BootstrapVersion;

using DllNotificationCallback = void(CALLBACK*)(
    ULONG reason,
    const void* data,
    void* context);
using RegisterDllNotificationFn = LONG(NTAPI*)(
    ULONG flags,
    DllNotificationCallback callback,
    void* context,
    void** cookie);

HANDLE BootstrapMapping = nullptr;
BootstrapHandshake* Handshake = nullptr;
void* NotificationCookie = nullptr;

template <typename Function>
Function Export(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

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

void CALLBACK ObserveModuleLoad(
    ULONG,
    const void*,
    void* context) {
    auto* handshake = static_cast<BootstrapHandshake*>(context);
    InterlockedIncrement(&handshake->ModuleNotificationCount);
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

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const RegisterDllNotificationFn registerNotification =
        Export<RegisterDllNotificationFn>(
            ntdll,
            "LdrRegisterDllNotification");
    if (registerNotification == nullptr) {
        SetError(
            Handshake,
            signal,
            ERROR_PROC_NOT_FOUND,
            L"LdrRegisterDllNotification is unavailable");
        CloseHandle(signal);
        return 5;
    }
    const LONG status = registerNotification(
        0,
        ObserveModuleLoad,
        Handshake,
        &NotificationCookie);
    if (status < 0) {
        SetError(
            Handshake,
            signal,
            static_cast<DWORD>(status),
            L"module notification registration failed");
        CloseHandle(signal);
        return 6;
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
