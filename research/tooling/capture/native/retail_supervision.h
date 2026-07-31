#pragma once

#include <windows.h>

#include <string>
#include <vector>

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
    const volatile LONG* ModuleNotificationCount;
};

int RunSupervision(const SupervisionRequest& request);

}  // namespace elysium::capture
