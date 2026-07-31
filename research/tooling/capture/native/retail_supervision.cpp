#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "retail_supervision.h"

namespace elysium::capture {
namespace {

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

struct Finalization {
    const char* State = "partial";
    const char* Reason = "supervision-error";
    DWORD ProcessId = 0;
    DWORD CollectorId = 0;
    DWORD ProcessExitCode = STILL_ACTIVE;
    DWORD CollectorExitCode = STILL_ACTIVE;
    std::string StartupProfile;
    std::string StartupConfig;
    LONG ModuleNotifications = 0;
    LONG BinaryProfileRegistryVersion = 0;
    LONG BinaryProfileCount = 0;
    LONG BinaryProfileMatches = 0;
    LONG BinaryProfileMisses = 0;
    LONG BinaryProfileUnloads = 0;
    LONG ActiveBinaryProfiles = 0;
    LONG BinaryProfileObservedMask = 0;
    LONG BinaryProfileMatchedMask = 0;
    LONG BinaryProfileLastMismatchIndex = 0;
    LONG BinaryProfileLastMismatchFlags = 0;
    LONG ProbeValidationPasses = 0;
    LONG ProbeHookInstalls = 0;
    LONG ProbeDiagnosticWrites = 0;
    std::array<ProbeActivationDiagnosticRecord, 16>
        ProbeDiagnostics{};
    std::size_t ProbeDiagnosticCount = 0;
    bool ProcessResumed = false;
    bool CollectorStarted = false;
};

HANDLE ConsoleShutdownEvent = nullptr;
volatile LONG ConsoleControl = 0;

BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
    if (controlType != CTRL_C_EVENT &&
        controlType != CTRL_BREAK_EVENT &&
        controlType != CTRL_CLOSE_EVENT) {
        return FALSE;
    }
    InterlockedCompareExchange(
        &ConsoleControl,
        static_cast<LONG>(controlType),
        0);
    if (ConsoleShutdownEvent != nullptr) {
        SetEvent(ConsoleShutdownEvent);
    }
    return TRUE;
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

std::wstring BuildCollectorCommand(
    const SupervisionRequest& request,
    const std::wstring& stopEventName) {
    std::vector<std::wstring> arguments{
        request.Collector,
        L"--stop-event",
        stopEventName,
        L"--target-pid",
        std::to_wstring(request.ProcessId),
    };
    arguments.insert(
        arguments.end(),
        request.CollectorArguments.begin(),
        request.CollectorArguments.end());
    std::wstring command;
    for (const std::wstring& argument : arguments) {
        if (!command.empty()) {
            command.push_back(L' ');
        }
        command.append(QuoteArgument(argument));
    }
    return command;
}

bool TerminateAndWait(HANDLE process, UINT exitCode) {
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
        return true;
    }
    if (!TerminateProcess(process, exitCode)) {
        return false;
    }
    return WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
}

DWORD ExitCode(HANDLE process) {
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process, &exitCode);
    return exitCode;
}

bool WriteAll(HANDLE file, const char* data, DWORD bytes) {
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) &&
        written == bytes;
}

const char* ProbeDiagnosticReasonName(std::uint32_t reason) {
    switch (reason) {
        case 1:
            return "unknown-hash";
        case 2:
            return "missing-module";
        case 3:
            return "unexpected-prologue";
        case 4:
            return "invalid-vtable-slot";
        case 5:
            return "unsupported-schema";
        default:
            return "unknown-reason";
    }
}

bool WriteFinalization(
    const std::wstring& path,
    const Finalization& result) {
    char document[8192]{};
    int length = _snprintf_s(
        document,
        sizeof(document),
        _TRUNCATE,
        "contract=elysium.retail-capture-finalization\n"
        "version=1\n"
        "state=%s\n"
        "reason=%s\n"
        "partial=%d\n"
        "process_id=%lu\n"
        "startup_profile=%s\n"
        "startup_config=%s\n"
        "collector_id=%lu\n"
        "process_resumed=%d\n"
        "collector_started=%d\n"
        "process_exit_code=%lu\n"
        "collector_exit_code=%lu\n"
        "module_notifications=%ld\n"
        "binary_profile_registry_version=%ld\n"
        "binary_profile_count=%ld\n"
        "binary_profile_matches=%ld\n"
        "binary_profile_misses=%ld\n"
        "binary_profile_unloads=%ld\n"
        "active_binary_profiles=%ld\n"
        "binary_profile_observed_mask=0x%08lx\n"
        "binary_profile_matched_mask=0x%08lx\n"
        "binary_profile_last_mismatch_index=%ld\n"
        "binary_profile_last_mismatch_flags=0x%08lx\n"
        "probe_validation_passes=%ld\n"
        "probe_hook_installs=%ld\n"
        "probe_diagnostic_writes=%ld\n"
        "probe_diagnostic_records=%zu\n",
        result.State,
        result.Reason,
        result.State[0] == 'c' ? 0 : 1,
        result.ProcessId,
        result.StartupProfile.c_str(),
        result.StartupConfig.c_str(),
        result.CollectorId,
        result.ProcessResumed ? 1 : 0,
        result.CollectorStarted ? 1 : 0,
        result.ProcessExitCode,
        result.CollectorExitCode,
        result.ModuleNotifications,
        result.BinaryProfileRegistryVersion,
        result.BinaryProfileCount,
        result.BinaryProfileMatches,
        result.BinaryProfileMisses,
        result.BinaryProfileUnloads,
        result.ActiveBinaryProfiles,
        result.BinaryProfileObservedMask,
        result.BinaryProfileMatchedMask,
        result.BinaryProfileLastMismatchIndex,
        result.BinaryProfileLastMismatchFlags,
        result.ProbeValidationPasses,
        result.ProbeHookInstalls,
        result.ProbeDiagnosticWrites,
        result.ProbeDiagnosticCount);
    if (length < 0) {
        return false;
    }
    for (std::size_t index = 0;
         index < result.ProbeDiagnosticCount;
         ++index) {
        const ProbeActivationDiagnosticRecord& record =
            result.ProbeDiagnostics[index];
        const int appended = _snprintf_s(
            document + length,
            sizeof(document) - static_cast<std::size_t>(length),
            _TRUNCATE,
            "probe_diagnostic_%zu="
            "record_id:%lu,schema:%lu,reason:%s,"
            "profile:%lu,target:%lu,image_base:0x%08lx,"
            "target_rva:0x%08lx,expected:0x%08lx,"
            "observed:0x%08lx\n",
            index,
            static_cast<unsigned long>(record.recordId),
            static_cast<unsigned long>(record.schemaVersion),
            ProbeDiagnosticReasonName(record.reason),
            static_cast<unsigned long>(record.profileIndex),
            static_cast<unsigned long>(record.targetIndex),
            static_cast<unsigned long>(record.imageBase),
            static_cast<unsigned long>(record.targetRva),
            static_cast<unsigned long>(record.expected),
            static_cast<unsigned long>(record.observed));
        if (appended < 0) {
            return false;
        }
        length += appended;
    }

    const std::wstring temporary = path + L".tmp";
    UniqueHandle file(CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE ||
        !WriteAll(file.Get(), document, static_cast<DWORD>(length)) ||
        !FlushFileBuffers(file.Get())) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    file = UniqueHandle();
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool ConfigureKillJob(HANDLE job, HANDLE process) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(
               job,
               JobObjectExtendedLimitInformation,
               &limits,
               sizeof(limits)) &&
        AssignProcessToJobObject(job, process);
}

}  // namespace

int RunSupervision(const SupervisionRequest& request) {
    Finalization result{};
    result.ProcessId = request.ProcessId;
    result.StartupProfile = request.StartupProfile;
    result.StartupConfig = request.StartupConfig;

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (job.Get() == nullptr ||
        !ConfigureKillJob(job.Get(), request.Process)) {
        result.Reason = "job-setup-error";
        TerminateAndWait(request.Process, 0xE2U);
        WriteFinalization(request.FinalizationPath, result);
        return 20;
    }

    const std::wstring stopEventName =
        L"Local\\ElysiumRetailCollectorStop.v1." +
        std::to_wstring(request.ProcessId);
    UniqueHandle collectorStop(CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        stopEventName.c_str()));
    if (collectorStop.Get() == nullptr) {
        result.Reason = "collector-stop-event-error";
        TerminateAndWait(request.Process, 0xE3U);
        WriteFinalization(request.FinalizationPath, result);
        return 21;
    }

    UniqueHandle collector;
    UniqueHandle collectorThread;
    if (!request.Collector.empty()) {
        const std::wstring collectorCommand =
            BuildCollectorCommand(request, stopEventName);
        std::vector<wchar_t> mutableCommand(
            collectorCommand.begin(),
            collectorCommand.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION created{};
        if (!CreateProcessW(
                request.Collector.c_str(),
                mutableCommand.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                    CREATE_NEW_PROCESS_GROUP,
                nullptr,
                request.WorkingDirectory.c_str(),
                &startup,
                &created)) {
            result.Reason = "collector-launch-error";
            TerminateAndWait(request.Process, 0xE4U);
            WriteFinalization(request.FinalizationPath, result);
            return 22;
        }
        collector = UniqueHandle(created.hProcess);
        collectorThread = UniqueHandle(created.hThread);
        result.CollectorId = created.dwProcessId;
        result.CollectorStarted = true;
        if (!AssignProcessToJobObject(job.Get(), collector.Get()) ||
            ResumeThread(collectorThread.Get()) != 1) {
            result.Reason = "collector-arm-error";
            TerminateAndWait(collector.Get(), 0xE5U);
            TerminateAndWait(request.Process, 0xE5U);
            WriteFinalization(request.FinalizationPath, result);
            return 23;
        }
    }

    UniqueHandle consoleEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (consoleEvent.Get() == nullptr) {
        result.Reason = "console-event-error";
        TerminateAndWait(request.Process, 0xE6U);
        WriteFinalization(request.FinalizationPath, result);
        return 24;
    }
    ConsoleShutdownEvent = consoleEvent.Get();
    InterlockedExchange(&ConsoleControl, 0);
    if (!SetConsoleCtrlHandler(ConsoleControlHandler, TRUE)) {
        ConsoleShutdownEvent = nullptr;
        result.Reason = "console-handler-error";
        TerminateAndWait(request.Process, 0xE7U);
        WriteFinalization(request.FinalizationPath, result);
        return 25;
    }

    if (ResumeThread(request.PrimaryThread) != 1) {
        SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
        ConsoleShutdownEvent = nullptr;
        result.Reason = "process-resume-error";
        TerminateAndWait(request.Process, 0xE8U);
        WriteFinalization(request.FinalizationPath, result);
        return 26;
    }
    result.ProcessResumed = true;
    std::wprintf(
        L"retail-launch-v1 event=supervision_started mode=launched pid=%lu "
        L"collector=%d timeout_ms=%lu\n",
        request.ProcessId,
        result.CollectorStarted ? 1 : 0,
        request.TimeoutMs);
    std::fflush(stdout);

    HANDLE waits[3]{
        request.Process,
        consoleEvent.Get(),
        collector.Get(),
    };
    const DWORD waitCount = collector.Get() == nullptr ? 2 : 3;
    const DWORD timeout =
        request.TimeoutMs == 0 ? INFINITE : request.TimeoutMs;
    const DWORD wait = WaitForMultipleObjects(
        waitCount,
        waits,
        FALSE,
        timeout);

    if (wait == WAIT_OBJECT_0) {
        result.ProcessExitCode = ExitCode(request.Process);
        if (std::find(
                request.NormalExitCodes.begin(),
                request.NormalExitCodes.end(),
                result.ProcessExitCode) !=
            request.NormalExitCodes.end()) {
            result.State = "complete";
            result.Reason = "process-exit";
        } else {
            result.Reason = "process-crash";
        }
    } else if (wait == WAIT_OBJECT_0 + 1) {
        result.Reason = "ctrl-c";
    } else if (waitCount == 3 && wait == WAIT_OBJECT_0 + 2) {
        result.Reason = "collector-exit";
        result.CollectorExitCode = ExitCode(collector.Get());
    } else if (wait == WAIT_TIMEOUT) {
        result.Reason = "timeout";
    } else {
        result.Reason = "wait-error";
    }

    SetEvent(collectorStop.Get());
    if (WaitForSingleObject(request.Process, 0) != WAIT_OBJECT_0) {
        TerminateAndWait(request.Process, 0xE9U);
    }
    result.ProcessExitCode = ExitCode(request.Process);
    if (collector.Get() != nullptr) {
        if (WaitForSingleObject(collector.Get(), 2000) != WAIT_OBJECT_0) {
            TerminateAndWait(collector.Get(), 0xEAU);
        }
        result.CollectorExitCode = ExitCode(collector.Get());
        if (result.State[0] == 'c' && result.CollectorExitCode != 0) {
            result.State = "partial";
            result.Reason = "collector-finalization-error";
        }
    } else {
        result.CollectorExitCode = 0;
    }
    MemoryBarrier();
    if (request.ModuleNotificationCount != nullptr) {
        result.ModuleNotifications = *request.ModuleNotificationCount;
    }
    if (request.BinaryProfileRegistryVersion != nullptr) {
        result.BinaryProfileRegistryVersion =
            *request.BinaryProfileRegistryVersion;
    }
    if (request.BinaryProfileCount != nullptr) {
        result.BinaryProfileCount = *request.BinaryProfileCount;
    }
    if (request.BinaryProfileMatchCount != nullptr) {
        result.BinaryProfileMatches = *request.BinaryProfileMatchCount;
    }
    if (request.BinaryProfileMissCount != nullptr) {
        result.BinaryProfileMisses = *request.BinaryProfileMissCount;
    }
    if (request.BinaryProfileUnloadCount != nullptr) {
        result.BinaryProfileUnloads = *request.BinaryProfileUnloadCount;
    }
    if (request.ActiveBinaryProfileCount != nullptr) {
        result.ActiveBinaryProfiles = *request.ActiveBinaryProfileCount;
    }
    if (request.BinaryProfileObservedMask != nullptr) {
        result.BinaryProfileObservedMask =
            *request.BinaryProfileObservedMask;
    }
    if (request.BinaryProfileMatchedMask != nullptr) {
        result.BinaryProfileMatchedMask =
            *request.BinaryProfileMatchedMask;
    }
    if (request.BinaryProfileLastMismatchIndex != nullptr) {
        result.BinaryProfileLastMismatchIndex =
            *request.BinaryProfileLastMismatchIndex;
    }
    if (request.BinaryProfileLastMismatchFlags != nullptr) {
        result.BinaryProfileLastMismatchFlags =
            *request.BinaryProfileLastMismatchFlags;
    }
    if (request.ProbeValidationPassCount != nullptr) {
        result.ProbeValidationPasses =
            *request.ProbeValidationPassCount;
    }
    if (request.ProbeHookInstallCount != nullptr) {
        result.ProbeHookInstalls =
            *request.ProbeHookInstallCount;
    }
    if (request.ProbeDiagnosticWriteCount != nullptr) {
        result.ProbeDiagnosticWrites =
            *request.ProbeDiagnosticWriteCount;
    }
    if (request.ProbeDiagnostics != nullptr &&
        request.ProbeDiagnosticCapacity != 0 &&
        result.ProbeDiagnosticWrites > 0) {
        const std::size_t capacity = (std::min)(
            request.ProbeDiagnosticCapacity,
            result.ProbeDiagnostics.size());
        const std::size_t retained = (std::min)(
            capacity,
            static_cast<std::size_t>(
                result.ProbeDiagnosticWrites));
        const std::size_t firstSequence =
            static_cast<std::size_t>(
                result.ProbeDiagnosticWrites) - retained;
        for (std::size_t index = 0; index < retained; ++index) {
            const std::size_t source =
                (firstSequence + index) % capacity;
            result.ProbeDiagnostics[index] =
                request.ProbeDiagnostics[source];
        }
        result.ProbeDiagnosticCount = retained;
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    ConsoleShutdownEvent = nullptr;
    const bool finalized =
        WriteFinalization(request.FinalizationPath, result);
    std::wprintf(
        L"retail-launch-v1 event=supervision_finalized mode=launched pid=%lu "
        L"state=%hs reason=%hs partial=%d process_exit=%lu "
        L"collector_exit=%lu module_notifications=%ld report=\"%ls\"\n",
        request.ProcessId,
        result.State,
        result.Reason,
        result.State[0] == 'c' ? 0 : 1,
        result.ProcessExitCode,
        result.CollectorExitCode,
        result.ModuleNotifications,
        request.FinalizationPath.c_str());
    std::fflush(stdout);
    if (!finalized) {
        return 27;
    }
    if (result.State[0] == 'c') {
        return 0;
    }
    if (std::strcmp(result.Reason, "ctrl-c") == 0) {
        return 130;
    }
    return 28;
}

}  // namespace elysium::capture
