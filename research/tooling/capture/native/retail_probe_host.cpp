#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>

#include "binary_profile_registry.h"
#include "bootstrap_contract.h"
#include "module_observer.h"
#include "probe_validation.h"

namespace {

using elysium::capture::BootstrapHandshake;
using elysium::capture::BootstrapMagic;
using elysium::capture::BootstrapState;
using elysium::capture::BootstrapVersion;
using elysium::capture::BinaryProfileRegistry;
using elysium::capture::BinaryProfileMatchDiagnostic;
using elysium::capture::ModuleObserver;
using elysium::capture::ProcessedModuleEvent;
using elysium::capture::ProfileActivationResult;
using elysium::capture::ProfileMismatchHashUnavailable;
using elysium::capture::ProfileMismatchSha256;
using elysium::capture::ProbeActivationGate;
using elysium::capture::ProbeDiagnosticCapacity;
using elysium::capture::ProbeValidationDiagnostic;

HANDLE BootstrapMapping = nullptr;
BootstrapHandshake* Handshake = nullptr;
ModuleObserver* Observer = nullptr;
BinaryProfileRegistry* ProfileRegistry = nullptr;
ProbeActivationGate* ActivationGate = nullptr;

void EmitProbeDiagnostic(
    const ProbeValidationDiagnostic& diagnostic,
    void*) noexcept {
    if (Handshake == nullptr) {
        return;
    }
    const LONG sequence =
        InterlockedIncrement(&Handshake->ProbeDiagnosticWriteCount);
    const std::uint32_t slot =
        static_cast<std::uint32_t>(sequence - 1) %
        ProbeDiagnosticCapacity;
    ProbeActivationDiagnosticRecord record{};
    std::memcpy(record.magic, "DIAG", sizeof(record.magic));
    record.recordBytes = sizeof(record);
    record.recordId =
        kProbeActivationDiagnosticRecordRecordId;
    record.schemaVersion =
        kProbeActivationDiagnosticRecordSchemaVersion;
    record.reason = static_cast<std::uint32_t>(diagnostic.Reason);
    record.profileIndex = diagnostic.ProfileIndex;
    record.targetIndex = diagnostic.TargetIndex;
    record.imageBase =
        static_cast<std::uint32_t>(diagnostic.ImageBase);
    record.targetRva = diagnostic.TargetRva;
    record.expected = diagnostic.Expected;
    record.observed = diagnostic.Observed;
    Handshake->ProbeDiagnostics[slot] = record;
    MemoryBarrier();
}

void ValidateProfileTargets(
    const ProcessedModuleEvent& event,
    const BinaryProfileMatchDiagnostic& match) noexcept {
    if (ProfileRegistry == nullptr ||
        ActivationGate == nullptr ||
        Handshake == nullptr ||
        match.Candidate == nullptr) {
        return;
    }
    elysium::capture::ActiveBinaryProfile active{};
    if (!ProfileRegistry->FindActive(event.ImageBase, &active)) {
        return;
    }
    for (std::uint32_t index = 0;
         index < active.Profile->TargetCount;
         ++index) {
        if (ActivationGate->ValidateTarget(
                active,
                static_cast<std::uint32_t>(match.ProfileIndex),
                active.Profile->Targets[index],
                index)) {
            InterlockedIncrement(
                &Handshake->ProbeValidationPassCount);
        }
    }
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

void ActivateProfiledProbes(
    const ProcessedModuleEvent& event,
    void*) noexcept {
    if (ProfileRegistry == nullptr || Handshake == nullptr) {
        return;
    }
    if (event.Kind == elysium::capture::ModuleEventKind::Unloaded) {
        if (ProfileRegistry->Deactivate(event.ImageBase)) {
            InterlockedIncrement(
                &Handshake->BinaryProfileUnloadCount);
        }
    } else {
        BinaryProfileMatchDiagnostic diagnostic{};
        const ProfileActivationResult result =
            ProfileRegistry->Activate(event, &diagnostic);
        if (diagnostic.Candidate != nullptr &&
            diagnostic.ProfileIndex < 32) {
            const LONG bit =
                static_cast<LONG>(1u << diagnostic.ProfileIndex);
            InterlockedOr(
                &Handshake->BinaryProfileObservedMask,
                bit);
            if (diagnostic.MismatchFlags == 0) {
                InterlockedOr(
                    &Handshake->BinaryProfileMatchedMask,
                    bit);
            } else {
                InterlockedExchange(
                    &Handshake->BinaryProfileLastMismatchIndex,
                    static_cast<LONG>(diagnostic.ProfileIndex));
                InterlockedExchange(
                    &Handshake->BinaryProfileLastMismatchFlags,
                    static_cast<LONG>(diagnostic.MismatchFlags));
                if ((diagnostic.MismatchFlags &
                     (ProfileMismatchSha256 |
                      ProfileMismatchHashUnavailable)) != 0 &&
                    ActivationGate != nullptr) {
                    ActivationGate->RejectUnknownHash(
                        static_cast<std::uint32_t>(
                            diagnostic.ProfileIndex),
                        event.ImageBase,
                        diagnostic.MismatchFlags);
                }
            }
        }
        if (result == ProfileActivationResult::Matched) {
            InterlockedIncrement(
                &Handshake->BinaryProfileMatchCount);
            ValidateProfileTargets(event, diagnostic);
        } else if (result == ProfileActivationResult::Unknown ||
                   result == ProfileActivationResult::Conflict) {
            InterlockedIncrement(
                &Handshake->BinaryProfileMissCount);
        }
    }
    InterlockedExchange(
        &Handshake->ActiveBinaryProfileCount,
        static_cast<LONG>(ProfileRegistry->ActiveCount()));
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

    ProfileRegistry = new (std::nothrow) BinaryProfileRegistry();
    if (ProfileRegistry == nullptr) {
        SetError(
            Handshake,
            signal,
            ERROR_OUTOFMEMORY,
            L"cannot allocate binary profile registry");
        CloseHandle(signal);
        return 5;
    }
    InterlockedExchange(
        &Handshake->BinaryProfileRegistryVersion,
        static_cast<LONG>(
            BinaryProfileRegistry::CompiledRegistryVersion()));
    InterlockedExchange(
        &Handshake->BinaryProfileCount,
        static_cast<LONG>(ProfileRegistry->ProfileCount()));
    InterlockedExchange(
        &Handshake->BinaryProfileRegistryLoaded,
        1);

    ActivationGate = new (std::nothrow) ProbeActivationGate(
        kCaptureRecordSchemas,
        std::size(kCaptureRecordSchemas),
        EmitProbeDiagnostic,
        nullptr);
    if (ActivationGate == nullptr) {
        SetError(
            Handshake,
            signal,
            ERROR_OUTOFMEMORY,
            L"cannot allocate probe activation gate");
        delete ProfileRegistry;
        ProfileRegistry = nullptr;
        CloseHandle(signal);
        return 6;
    }

    Observer = new (std::nothrow) ModuleObserver();
    if (Observer == nullptr) {
        SetError(
            Handshake,
            signal,
            ERROR_OUTOFMEMORY,
            L"cannot allocate module observer");
        delete ProfileRegistry;
        ProfileRegistry = nullptr;
        delete ActivationGate;
        ActivationGate = nullptr;
        CloseHandle(signal);
        return 7;
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
        delete ProfileRegistry;
        ProfileRegistry = nullptr;
        delete ActivationGate;
        ActivationGate = nullptr;
        CloseHandle(signal);
        return 8;
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
        delete ProfileRegistry;
        ProfileRegistry = nullptr;
        delete ActivationGate;
        ActivationGate = nullptr;
        CloseHandle(signal);
        return 9;
    }

    for (std::size_t index = 0;
         index < ProfileRegistry->ProfileCount();
         ++index) {
        const elysium::capture::BinaryProfile* profile =
            ProfileRegistry->ProfileAt(index);
        if (profile == nullptr || profile->TargetCount == 0) {
            continue;
        }
        elysium::capture::ActiveBinaryProfile active{};
        if (!ProfileRegistry->FindActive(*profile, &active)) {
            ActivationGate->RejectMissingModule(
                static_cast<std::uint32_t>(index));
        }
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
