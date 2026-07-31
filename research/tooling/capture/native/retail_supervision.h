#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "probe_validation.h"

namespace elysium::capture {

struct SupervisionRequest {
    HANDLE Process;
    HANDLE PrimaryThread;
    DWORD ProcessId;
    std::wstring WorkingDirectory;
    std::wstring Collector;
    std::vector<std::wstring> CollectorArguments;
    std::wstring FinalizationPath;
    DWORD TimeoutMs;
    std::vector<DWORD> NormalExitCodes;
    std::string StartupProfile;
    std::string StartupConfig;
    const volatile LONG* ModuleNotificationCount;
    const volatile LONG* BinaryProfileCount;
    const volatile LONG* BinaryProfileMatchCount;
    const volatile LONG* BinaryProfileMissCount;
    const volatile LONG* BinaryProfileUnloadCount;
    const volatile LONG* ActiveBinaryProfileCount;
    const volatile LONG* BinaryProfileObservedMask;
    const volatile LONG* BinaryProfileMatchedMask;
    const volatile LONG* BinaryProfileLastMismatchIndex;
    const volatile LONG* BinaryProfileLastMismatchFlags;
    const volatile LONG* ProbeValidationPassCount;
    const volatile LONG* ProbeHookInstallCount;
    const volatile LONG* ProbeDiagnosticWriteCount;
    const ProbeValidationDiagnostic* ProbeDiagnostics;
    std::size_t ProbeDiagnosticCapacity;
};

int RunSupervision(const SupervisionRequest& request);

}  // namespace elysium::capture
