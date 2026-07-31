#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "bootstrap_contract.h"
#include "retail_supervision.h"

namespace {

static_assert(sizeof(void*) == 4, "the retail launcher must be 32-bit");

using elysium::capture::BootstrapHandshake;
using elysium::capture::BootstrapMagic;
using elysium::capture::BootstrapState;
using elysium::capture::BootstrapVersion;

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : Handle_(handle) {}
    ~UniqueHandle() {
        if (Handle_ != nullptr && Handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(Handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : Handle_(std::exchange(other.Handle_, nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (Handle_ != nullptr && Handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(Handle_);
            }
            Handle_ = std::exchange(other.Handle_, nullptr);
        }
        return *this;
    }

    HANDLE Get() const noexcept {
        return Handle_;
    }

private:
    HANDLE Handle_ = nullptr;
};

enum class StartupProfile {
    Direct,
    UnofficialPatch,
};

struct Options {
    std::wstring Executable;
    std::wstring WorkingDirectory;
    std::wstring Distribution;
    std::wstring ProbeHost;
    std::wstring Collector;
    std::wstring FinalizationPath;
    StartupProfile Profile = StartupProfile::Direct;
    std::vector<std::wstring> EnvironmentOverrides;
    std::vector<std::wstring> CollectorArguments;
    std::vector<std::wstring> TargetArguments;
    DWORD VerifySuspendedMs = 25;
    DWORD TimeoutMs = 0;
    bool ResumeSynthetic = false;
    bool InjectAndTerminate = false;
    bool Supervise = false;
    bool TerminateAfterVerification = false;
};

bool ParseUnsigned(
    const wchar_t* text,
    const wchar_t* option,
    DWORD* destination) {
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != L'\0' ||
        value > 600000UL ||
        value > std::numeric_limits<DWORD>::max()) {
        std::fwprintf(stderr, L"invalid %ls value: %ls\n", option, text);
        return false;
    }
    *destination = static_cast<DWORD>(value);
    return true;
}

bool TakeValue(
    int argc,
    wchar_t** argv,
    int* index,
    const wchar_t** value) {
    if (*index + 1 >= argc) {
        std::fwprintf(stderr, L"missing value for %ls\n", argv[*index]);
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    int index = 1;
    for (; index < argc; ++index) {
        const wchar_t* option = argv[index];
        if (std::wcscmp(option, L"--") == 0) {
            ++index;
            break;
        }
        if (std::wcscmp(option, L"--resume-synthetic") == 0) {
            options->ResumeSynthetic = true;
            continue;
        }
        if (std::wcscmp(option, L"--terminate-after-verification") == 0) {
            options->TerminateAfterVerification = true;
            continue;
        }
        if (std::wcscmp(option, L"--inject-and-terminate") == 0) {
            options->InjectAndTerminate = true;
            continue;
        }
        if (std::wcscmp(option, L"--supervise") == 0) {
            options->Supervise = true;
            continue;
        }

        const wchar_t* value = nullptr;
        if (!TakeValue(argc, argv, &index, &value)) {
            return false;
        }
        if (std::wcscmp(option, L"--executable") == 0) {
            options->Executable = value;
        } else if (std::wcscmp(option, L"--probe-host") == 0) {
            options->ProbeHost = value;
        } else if (std::wcscmp(option, L"--collector") == 0) {
            options->Collector = value;
        } else if (std::wcscmp(option, L"--collector-argument") == 0) {
            options->CollectorArguments.emplace_back(value);
        } else if (std::wcscmp(option, L"--finalization") == 0) {
            options->FinalizationPath = value;
        } else if (std::wcscmp(option, L"--working-directory") == 0) {
            options->WorkingDirectory = value;
        } else if (std::wcscmp(option, L"--distribution") == 0) {
            options->Distribution = value;
        } else if (std::wcscmp(option, L"--startup-profile") == 0) {
            if (std::wcscmp(value, L"direct") == 0) {
                options->Profile = StartupProfile::Direct;
            } else if (std::wcscmp(value, L"unofficial-patch") == 0) {
                options->Profile = StartupProfile::UnofficialPatch;
            } else {
                std::fwprintf(
                    stderr,
                    L"unknown startup profile: %ls\n",
                    value);
                return false;
            }
        } else if (std::wcscmp(option, L"--environment") == 0) {
            const wchar_t* separator = std::wcschr(value, L'=');
            if (separator == nullptr || separator == value) {
                std::fwprintf(
                    stderr,
                    L"--environment must be NAME=VALUE: %ls\n",
                    value);
                return false;
            }
            options->EnvironmentOverrides.emplace_back(value);
        } else if (std::wcscmp(option, L"--verify-suspended-ms") == 0) {
            if (!ParseUnsigned(value, option, &options->VerifySuspendedMs)) {
                return false;
            }
        } else if (std::wcscmp(option, L"--timeout-ms") == 0) {
            if (!ParseUnsigned(value, option, &options->TimeoutMs)) {
                return false;
            }
        } else {
            std::fwprintf(stderr, L"unknown launcher option: %ls\n", option);
            return false;
        }
    }
    for (; index < argc; ++index) {
        options->TargetArguments.emplace_back(argv[index]);
    }

    if (options->Executable.empty() ||
        options->WorkingDirectory.empty() ||
        options->Distribution.empty()) {
        std::fwprintf(
            stderr,
            L"--executable, --working-directory, and --distribution "
            L"are required\n");
        return false;
    }
    const int actions =
        (options->ResumeSynthetic ? 1 : 0) +
        (options->InjectAndTerminate ? 1 : 0) +
        (options->Supervise ? 1 : 0) +
        (options->TerminateAfterVerification ? 1 : 0);
    if (actions != 1) {
        std::fwprintf(
            stderr,
            L"select exactly one launch completion action\n");
        return false;
    }
    if ((options->ResumeSynthetic || options->InjectAndTerminate ||
         options->Supervise) &&
        options->ProbeHost.empty()) {
        std::fwprintf(
            stderr,
            L"--probe-host is required for bootstrap injection\n");
        return false;
    }
    if (options->Supervise && options->FinalizationPath.empty()) {
        std::fwprintf(
            stderr,
            L"--finalization is required with --supervise\n");
        return false;
    }
    return true;
}

bool FullPath(
    const std::wstring& input,
    std::wstring* output) {
    const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return false;
    }
    std::vector<wchar_t> buffer(required);
    const DWORD written = GetFullPathNameW(
        input.c_str(),
        required,
        buffer.data(),
        nullptr);
    if (written == 0 || written >= required) {
        return false;
    }
    *output = buffer.data();
    return true;
}

std::wstring QuoteArgument(const std::wstring& argument) {
    if (!argument.empty() &&
        argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted(1, L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildCommandLine(
    const std::wstring& executable,
    const Options& options) {
    std::vector<std::wstring> arguments;
    arguments.push_back(executable);
    if (options.Profile == StartupProfile::UnofficialPatch) {
        arguments.emplace_back(L"-game");
        arguments.emplace_back(L"Unofficial_Patch");
    }
    arguments.insert(
        arguments.end(),
        options.TargetArguments.begin(),
        options.TargetArguments.end());

    std::wstring commandLine;
    for (const std::wstring& argument : arguments) {
        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine.append(QuoteArgument(argument));
    }
    return commandLine;
}

std::size_t EnvironmentNameLength(const std::wstring& entry) {
    const std::size_t start = !entry.empty() && entry.front() == L'=' ? 1 : 0;
    const std::size_t separator = entry.find(L'=', start);
    return separator == std::wstring::npos ? entry.size() : separator;
}

bool SameEnvironmentName(
    const std::wstring& left,
    const std::wstring& right) {
    const int leftLength = static_cast<int>(EnvironmentNameLength(left));
    const int rightLength = static_cast<int>(EnvironmentNameLength(right));
    if (leftLength != rightLength) {
        return false;
    }
    return CompareStringOrdinal(
               left.c_str(),
               leftLength,
               right.c_str(),
               rightLength,
               TRUE) == CSTR_EQUAL;
}

bool EnvironmentLess(
    const std::wstring& left,
    const std::wstring& right) {
    const int comparison = CompareStringOrdinal(
        left.c_str(),
        -1,
        right.c_str(),
        -1,
        TRUE);
    return comparison == CSTR_LESS_THAN;
}

bool BuildEnvironment(
    const std::vector<std::wstring>& overrides,
    std::vector<wchar_t>* block) {
    if (overrides.empty()) {
        return true;
    }

    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) {
        return false;
    }
    std::vector<std::wstring> entries;
    for (const wchar_t* cursor = inherited; *cursor != L'\0';) {
        entries.emplace_back(cursor);
        cursor += std::wcslen(cursor) + 1;
    }
    FreeEnvironmentStringsW(inherited);

    for (const std::wstring& overrideValue : overrides) {
        const auto existing = std::find_if(
            entries.begin(),
            entries.end(),
            [&overrideValue](const std::wstring& entry) {
                return SameEnvironmentName(entry, overrideValue);
            });
        if (existing == entries.end()) {
            entries.push_back(overrideValue);
        } else {
            *existing = overrideValue;
        }
    }
    std::sort(entries.begin(), entries.end(), EnvironmentLess);

    std::size_t characters = 1;
    for (const std::wstring& entry : entries) {
        characters += entry.size() + 1;
    }
    block->reserve(characters);
    for (const std::wstring& entry : entries) {
        block->insert(block->end(), entry.begin(), entry.end());
        block->push_back(L'\0');
    }
    block->push_back(L'\0');
    return true;
}

class BootstrapSession {
public:
    ~BootstrapSession() {
        if (Handshake_ != nullptr) {
            UnmapViewOfFile(Handshake_);
        }
    }

    BootstrapSession(const BootstrapSession&) = delete;
    BootstrapSession& operator=(const BootstrapSession&) = delete;
    BootstrapSession() = default;

    bool Create(DWORD processId) {
        const std::wstring mappingName =
            elysium::capture::BootstrapMappingName(processId);
        const std::wstring signalName =
            elysium::capture::BootstrapSignalName(processId);
        Mapping_ = UniqueHandle(CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(BootstrapHandshake),
            mappingName.c_str()));
        if (Mapping_.Get() == nullptr ||
            GetLastError() == ERROR_ALREADY_EXISTS) {
            return false;
        }
        Handshake_ = static_cast<BootstrapHandshake*>(
            MapViewOfFile(
                Mapping_.Get(),
                FILE_MAP_READ | FILE_MAP_WRITE,
                0,
                0,
                sizeof(BootstrapHandshake)));
        if (Handshake_ == nullptr) {
            return false;
        }
        Signal_ = UniqueHandle(CreateEventW(
            nullptr,
            FALSE,
            FALSE,
            signalName.c_str()));
        if (Signal_.Get() == nullptr ||
            GetLastError() == ERROR_ALREADY_EXISTS) {
            return false;
        }

        ZeroMemory(Handshake_, sizeof(*Handshake_));
        Handshake_->Magic = BootstrapMagic;
        Handshake_->Version = BootstrapVersion;
        Handshake_->Bytes = sizeof(BootstrapHandshake);
        Handshake_->ProcessId = processId;
        InterlockedExchange(
            &Handshake_->State,
            static_cast<LONG>(BootstrapState::Created));
        return true;
    }

    bool WaitReady(DWORD timeoutMs) const {
        if (WaitForSingleObject(Signal_.Get(), timeoutMs) != WAIT_OBJECT_0) {
            std::fwprintf(stderr, L"probe-host handshake timed out\n");
            return false;
        }
        MemoryBarrier();
        const auto state =
            static_cast<BootstrapState>(Handshake_->State);
        if (state == BootstrapState::Error) {
            std::fwprintf(
                stderr,
                L"probe-host error 0x%08lx: %ls\n",
                Handshake_->ErrorCode,
                Handshake_->Message);
            return false;
        }
        if (state != BootstrapState::Ready ||
            Handshake_->Magic != BootstrapMagic ||
            Handshake_->Version != BootstrapVersion ||
            Handshake_->Bytes != sizeof(BootstrapHandshake) ||
            Handshake_->ProbeThreadId == 0 ||
            Handshake_->ModuleObserverArmed != 1 ||
            Handshake_->TransportArmed != 1) {
            std::fwprintf(stderr, L"probe-host ready contract is invalid\n");
            return false;
        }
        return true;
    }

    const BootstrapHandshake& Handshake() const {
        return *Handshake_;
    }

private:
    UniqueHandle Mapping_;
    UniqueHandle Signal_;
    BootstrapHandshake* Handshake_ = nullptr;
};

template <typename Function>
Function Export(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

bool InjectLibrary(
    HANDLE process,
    const std::wstring& library,
    BootstrapSession* bootstrap) {
    const SIZE_T pathBytes =
        (library.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process,
        nullptr,
        pathBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (remotePath == nullptr) {
        std::fwprintf(stderr, L"cannot allocate remote DLL path\n");
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(
            process,
            remotePath,
            library.c_str(),
            pathBytes,
            &written) ||
        written != pathBytes) {
        std::fwprintf(stderr, L"cannot write remote DLL path\n");
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    const auto loadLibrary = Export<LPTHREAD_START_ROUTINE>(
        GetModuleHandleW(L"kernel32.dll"),
        "LoadLibraryW");
    if (loadLibrary == nullptr) {
        std::fwprintf(stderr, L"cannot resolve LoadLibraryW\n");
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }
    UniqueHandle loader(CreateRemoteThread(
        process,
        nullptr,
        0,
        loadLibrary,
        remotePath,
        0,
        nullptr));
    if (loader.Get() == nullptr) {
        std::fwprintf(
            stderr,
            L"CreateRemoteThread failed: %lu\n",
            GetLastError());
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }
    if (WaitForSingleObject(loader.Get(), 15000) != WAIT_OBJECT_0) {
        std::fwprintf(stderr, L"remote LoadLibraryW timed out\n");
        return false;
    }

    DWORD remoteModule = 0;
    const bool loaded =
        GetExitCodeThread(loader.Get(), &remoteModule) &&
        remoteModule != 0;
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if (!loaded) {
        std::fwprintf(stderr, L"remote LoadLibraryW failed\n");
        return false;
    }
    return bootstrap->WaitReady(15000);
}

bool IsSyntheticTarget(const std::wstring& executable) {
    HMODULE module = LoadLibraryExW(
        executable.c_str(),
        nullptr,
        DONT_RESOLVE_DLL_REFERENCES);
    if (module == nullptr) {
        return false;
    }
    const bool synthetic =
        GetProcAddress(
            module,
            "ElysiumSyntheticExecutableHookTarget") != nullptr;
    FreeLibrary(module);
    return synthetic;
}

bool TerminateAndWait(HANDLE process, UINT exitCode) {
    if (!TerminateProcess(process, exitCode)) {
        return false;
    }
    return WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
}

const wchar_t* ProfileName(StartupProfile profile) {
    return profile == StartupProfile::UnofficialPatch
        ? L"unofficial-patch"
        : L"direct";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options{};
    if (!ParseOptions(argc, argv, &options)) {
        std::fwprintf(
            stderr,
            L"usage: retail_launcher.exe --executable PATH "
            L"--working-directory PATH --distribution NAME "
            L"[--startup-profile direct|unofficial-patch] "
            L"[--environment NAME=VALUE] [--verify-suspended-ms N] "
            L"[--probe-host PATH] [--collector PATH] "
            L"[--collector-argument VALUE] [--timeout-ms N] "
            L"[--finalization PATH] "
            L"(--resume-synthetic|--inject-and-terminate|"
            L"--supervise|--terminate-after-verification) "
            L"[-- target arguments]\n");
        return 2;
    }

    std::wstring executable;
    std::wstring workingDirectory;
    std::wstring probeHost;
    std::wstring collector;
    std::wstring finalizationPath;
    if (!FullPath(options.Executable, &executable) ||
        !FullPath(options.WorkingDirectory, &workingDirectory) ||
        (!options.ProbeHost.empty() &&
         !FullPath(options.ProbeHost, &probeHost)) ||
        (!options.Collector.empty() &&
         !FullPath(options.Collector, &collector)) ||
        (!options.FinalizationPath.empty() &&
         !FullPath(options.FinalizationPath, &finalizationPath))) {
        std::fwprintf(stderr, L"cannot resolve launch paths\n");
        return 3;
    }
    const DWORD executableAttributes = GetFileAttributesW(executable.c_str());
    const DWORD directoryAttributes =
        GetFileAttributesW(workingDirectory.c_str());
    if (executableAttributes == INVALID_FILE_ATTRIBUTES ||
        (executableAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        directoryAttributes == INVALID_FILE_ATTRIBUTES ||
        (directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        std::fwprintf(stderr, L"invalid executable or working directory\n");
        return 4;
    }
    if (!probeHost.empty()) {
        const DWORD probeAttributes =
            GetFileAttributesW(probeHost.c_str());
        if (probeAttributes == INVALID_FILE_ATTRIBUTES ||
            (probeAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            std::fwprintf(stderr, L"invalid probe-host DLL\n");
            return 4;
        }
    }
    if (!collector.empty()) {
        const DWORD collectorAttributes =
            GetFileAttributesW(collector.c_str());
        if (collectorAttributes == INVALID_FILE_ATTRIBUTES ||
            (collectorAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            std::fwprintf(stderr, L"invalid collector executable\n");
            return 4;
        }
    }
    if (options.ResumeSynthetic && !IsSyntheticTarget(executable)) {
        std::fwprintf(
            stderr,
            L"--resume-synthetic requires the synthetic test executable\n");
        return 5;
    }

    const std::wstring commandLine = BuildCommandLine(executable, options);
    std::vector<wchar_t> mutableCommandLine(
        commandLine.begin(),
        commandLine.end());
    mutableCommandLine.push_back(L'\0');
    std::vector<wchar_t> environment;
    if (!BuildEnvironment(options.EnvironmentOverrides, &environment)) {
        std::fwprintf(stderr, L"cannot construct inherited environment\n");
        return 6;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION created{};
    const DWORD creationFlags =
        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
        CREATE_NEW_PROCESS_GROUP;
    const BOOL launched = CreateProcessW(
        executable.c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        environment.empty() ? nullptr : environment.data(),
        workingDirectory.c_str(),
        &startup,
        &created);
    if (!launched) {
        std::fwprintf(
            stderr,
            L"CreateProcessW failed: %lu\n",
            GetLastError());
        return 7;
    }

    UniqueHandle process(created.hProcess);
    UniqueHandle thread(created.hThread);
    bool childActive = true;
    auto failLaunch = [&](const wchar_t* message, int code) {
        std::fwprintf(stderr, L"%ls\n", message);
        if (childActive && !TerminateAndWait(process.Get(), 0xE1U)) {
            std::fwprintf(
                stderr,
                L"failed to terminate suspended child: %lu\n",
                GetLastError());
        }
        return code;
    };

    if (WaitForSingleObject(process.Get(), options.VerifySuspendedMs) !=
        WAIT_TIMEOUT) {
        return failLaunch(L"child exited before suspended verification", 8);
    }
    const DWORD previousSuspendCount = SuspendThread(thread.Get());
    if (previousSuspendCount == static_cast<DWORD>(-1)) {
        return failLaunch(L"SuspendThread verification failed", 9);
    }
    const DWORD restoredSuspendCount = ResumeThread(thread.Get());
    if (previousSuspendCount != 1 || restoredSuspendCount != 2) {
        return failLaunch(L"primary thread was not created suspended", 10);
    }

    std::wprintf(
        L"retail-launch-v1 event=process_suspended pid=%lu "
        L"distribution=%ls startup_profile=%ls "
        L"working_directory=\"%ls\" command_line=\"%ls\" "
        L"environment_overrides=%zu\n",
        created.dwProcessId,
        options.Distribution.c_str(),
        ProfileName(options.Profile),
        workingDirectory.c_str(),
        commandLine.c_str(),
        options.EnvironmentOverrides.size());
    std::fflush(stdout);

    if (options.TerminateAfterVerification) {
        if (!TerminateAndWait(process.Get(), 0)) {
            return failLaunch(L"cannot terminate verified child", 11);
        }
        childActive = false;
        std::wprintf(
            L"retail-launch-v1 event=verification_complete pid=%lu\n",
            created.dwProcessId);
        return 0;
    }

    BootstrapSession bootstrap;
    if (!bootstrap.Create(created.dwProcessId)) {
        return failLaunch(L"cannot create bootstrap handshake", 12);
    }
    if (!InjectLibrary(process.Get(), probeHost, &bootstrap)) {
        return failLaunch(L"probe-host bootstrap injection failed", 13);
    }
    const BootstrapHandshake& ready = bootstrap.Handshake();
    std::wprintf(
        L"retail-launch-v1 event=bootstrap_ready pid=%lu "
        L"version=%lu observer_armed=%ld transport_armed=%ld "
        L"probe_thread=%lu\n",
        created.dwProcessId,
        static_cast<unsigned long>(ready.Version),
        ready.ModuleObserverArmed,
        ready.TransportArmed,
        ready.ProbeThreadId);
    std::fflush(stdout);

    if (options.InjectAndTerminate) {
        if (!TerminateAndWait(process.Get(), 0)) {
            return failLaunch(L"cannot terminate bootstrapped child", 14);
        }
        childActive = false;
        std::wprintf(
            L"retail-launch-v1 event=bootstrap_verified pid=%lu\n",
            created.dwProcessId);
        return 0;
    }

    if (options.Supervise) {
        const elysium::capture::SupervisionRequest request{
            process.Get(),
            thread.Get(),
            created.dwProcessId,
            workingDirectory,
            collector,
            options.CollectorArguments,
            finalizationPath,
            options.TimeoutMs,
            &ready.ModuleNotificationCount,
        };
        const int supervisionResult =
            elysium::capture::RunSupervision(request);
        childActive = false;
        return supervisionResult;
    }

    if (ResumeThread(thread.Get()) != 1) {
        return failLaunch(L"cannot resume synthetic primary thread", 15);
    }
    const DWORD wait = WaitForSingleObject(process.Get(), 15000);
    if (wait != WAIT_OBJECT_0) {
        return failLaunch(L"synthetic child did not exit within 15 seconds", 16);
    }
    childActive = false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.Get(), &exitCode)) {
        std::fwprintf(stderr, L"cannot read synthetic exit code\n");
        return 17;
    }
    MemoryBarrier();
    const LONG moduleNotifications =
        bootstrap.Handshake().ModuleNotificationCount;
    if (moduleNotifications < 3) {
        std::fwprintf(
            stderr,
            L"probe host observed only %ld module notifications\n",
            moduleNotifications);
        return 18;
    }
    std::wprintf(
        L"retail-launch-v1 event=launch_complete pid=%lu exit_code=%lu "
        L"module_notifications=%ld\n",
        created.dwProcessId,
        exitCode,
        moduleNotifications);
    return exitCode == 0 ? 0 : 19;
}
