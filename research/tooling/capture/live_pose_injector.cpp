#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <cstdio>
#include <cwchar>

namespace {

DWORD FindProcess(const wchar_t* executable) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (!_wcsicmp(entry.szExeFile, executable) &&
                entry.th32ProcessID > found) {
                found = entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool AlreadyLoaded(DWORD pid, const wchar_t* moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (!_wcsicmp(entry.szModule, moduleName)) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) {
        std::fwprintf(
            stderr, L"usage: live_pose_injector.exe <hook.dll> [pid]\n");
        return 2;
    }

    wchar_t fullPath[32768]{};
    if (!GetFullPathNameW(argv[1], ARRAYSIZE(fullPath), fullPath, nullptr)) {
        std::fwprintf(stderr, L"cannot resolve hook DLL path\n");
        return 3;
    }
    const wchar_t* moduleName = std::wcsrchr(fullPath, L'\\');
    moduleName = moduleName ? moduleName + 1 : fullPath;
    const DWORD pid =
        argc == 3 ? static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 10))
                  : FindProcess(L"vampire.exe");
    if (!pid) {
        std::fwprintf(stderr, L"vampire.exe is not running\n");
        return 4;
    }
    if (AlreadyLoaded(pid, moduleName)) {
        std::fwprintf(stderr, L"%ls is already loaded in PID %lu\n", moduleName, pid);
        return 5;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!process) {
        std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
        return 6;
    }

    const SIZE_T pathBytes =
        (std::wcslen(fullPath) + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath ||
        !WriteProcessMemory(
            process, remotePath, fullPath, pathBytes, nullptr)) {
        std::fwprintf(stderr, L"cannot stage DLL path: %lu\n", GetLastError());
        if (remotePath) {
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        }
        CloseHandle(process);
        return 7;
    }

    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (!thread) {
        std::fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(process);
        return 8;
    }

    const DWORD wait = WaitForSingleObject(thread, 15000);
    DWORD remoteModule = 0;
    GetExitCodeThread(thread, &remoteModule);
    CloseHandle(thread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    CloseHandle(process);
    if (wait != WAIT_OBJECT_0 || !remoteModule) {
        std::fwprintf(stderr, L"LoadLibraryW failed in PID %lu\n", pid);
        return 9;
    }

    std::wprintf(
        L"Injected %ls into PID %lu at 0x%08lx\n",
        fullPath, pid, remoteModule);
    return 0;
}
