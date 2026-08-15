#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <string>

#include "bootstrap_contract.h"
#include "synthetic_capture_contract.h"

namespace {

struct Options {
    const wchar_t* StopEvent = nullptr;
    const wchar_t* ReadyEvent = nullptr;
    DWORD TargetPid = 0;
    DWORD ExitAfterMs = 0;
    DWORD ExitCode = 0;
    DWORD RequestTargetStopMs = 0;
    const wchar_t* TracePath = nullptr;
    bool RequestTargetStop = false;
};

bool ParseUnsigned(
    const wchar_t* text,
    const wchar_t* option,
    DWORD maximum,
    DWORD* destination) {
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != L'\0' ||
        value > maximum ||
        value > std::numeric_limits<DWORD>::max()) {
        std::fwprintf(stderr, L"invalid %ls value: %ls\n", option, text);
        return false;
    }
    *destination = static_cast<DWORD>(value);
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        if (std::wcscmp(argv[index], L"--stop-event") == 0) {
            options->StopEvent = argv[index + 1];
        } else if (std::wcscmp(argv[index], L"--ready-event") == 0) {
            options->ReadyEvent = argv[index + 1];
        } else if (std::wcscmp(argv[index], L"--target-pid") == 0) {
            if (!ParseUnsigned(
                    argv[index + 1],
                    argv[index],
                    std::numeric_limits<DWORD>::max(),
                    &options->TargetPid)) {
                return false;
            }
        } else if (std::wcscmp(argv[index], L"--exit-after-ms") == 0) {
            if (!ParseUnsigned(
                    argv[index + 1],
                    argv[index],
                    600000,
                    &options->ExitAfterMs)) {
                return false;
            }
        } else if (std::wcscmp(argv[index], L"--exit-code") == 0) {
            if (!ParseUnsigned(
                    argv[index + 1],
                    argv[index],
                    255,
                    &options->ExitCode)) {
                return false;
            }
        } else if (
            std::wcscmp(
                argv[index],
                L"--request-target-stop-ms") == 0) {
            if (!ParseUnsigned(
                    argv[index + 1],
                    argv[index],
                    600000,
                    &options->RequestTargetStopMs)) {
                return false;
            }
            options->RequestTargetStop = true;
        } else if (std::wcscmp(argv[index], L"--trace") == 0) {
            options->TracePath = argv[index + 1];
        } else {
            return false;
        }
    }
    return options->StopEvent != nullptr &&
        options->ReadyEvent != nullptr &&
        options->TargetPid != 0 &&
        (!options->RequestTargetStop || options->TracePath != nullptr);
}

bool WaitForNamedEvent(
    const std::wstring& name,
    HANDLE target,
    HANDLE* event) {
    for (DWORD elapsed = 0; elapsed < 5000; ++elapsed) {
        *event = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name.c_str());
        if (*event != nullptr) {
            return true;
        }
        if (WaitForSingleObject(target, 0) == WAIT_OBJECT_0) {
            return false;
        }
        Sleep(1);
    }
    return false;
}

bool WriteTrace(
    const Options& options,
    LONG moduleNotifications,
    bool targetExited) {
    char document[512]{};
    const int length = _snprintf_s(
        document,
        sizeof(document),
        _TRUNCATE,
        "contract=%s\n"
        "version=%lu\n"
        "state=complete\n"
        "target_pid=%lu\n"
        "module_notifications=%ld\n"
        "capture_started=1\n"
        "stop_requested=1\n"
        "target_exited=%d\n",
        elysium::capture::SyntheticTraceContract,
        elysium::capture::SyntheticTraceVersion,
        options.TargetPid,
        moduleNotifications,
        targetExited ? 1 : 0);
    if (length < 0) {
        return false;
    }

    const std::wstring tracePath(options.TracePath);
    const std::wstring temporary = tracePath + L".tmp";
    HANDLE file = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool complete =
        WriteFile(
            file,
            document,
            static_cast<DWORD>(length),
            &written,
            nullptr) &&
        written == static_cast<DWORD>(length) &&
        FlushFileBuffers(file);
    CloseHandle(file);
    if (!complete ||
        !MoveFileExW(
            temporary.c_str(),
            tracePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options{};
    if (!ParseOptions(argc, argv, &options)) {
        std::fwprintf(stderr, L"invalid synthetic collector arguments\n");
        return 2;
    }
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, options.StopEvent);
    HANDLE ready = OpenEventW(
        EVENT_MODIFY_STATE,
        FALSE,
        options.ReadyEvent);
    HANDLE target = OpenProcess(SYNCHRONIZE, FALSE, options.TargetPid);
    if (stop == nullptr || ready == nullptr || target == nullptr) {
        if (stop != nullptr) {
            CloseHandle(stop);
        }
        if (target != nullptr) {
            CloseHandle(target);
        }
        if (ready != nullptr) {
            CloseHandle(ready);
        }
        return 3;
    }
    if (!SetEvent(ready)) {
        CloseHandle(ready);
        CloseHandle(target);
        CloseHandle(stop);
        return 4;
    }
    CloseHandle(ready);

    HANDLE mapping = nullptr;
    const elysium::capture::BootstrapHandshake* handshake = nullptr;
    HANDLE captureReady = nullptr;
    HANDLE captureStop = nullptr;
    if (options.RequestTargetStop) {
        mapping = OpenFileMappingW(
            FILE_MAP_READ,
            FALSE,
            elysium::capture::BootstrapMappingName(
                options.TargetPid).c_str());
        if (mapping != nullptr) {
            handshake = static_cast<
                const elysium::capture::BootstrapHandshake*>(
                    MapViewOfFile(
                        mapping,
                        FILE_MAP_READ,
                        0,
                        0,
                        sizeof(elysium::capture::BootstrapHandshake)));
        }
        if (handshake == nullptr ||
            !WaitForNamedEvent(
                elysium::capture::SyntheticCaptureReadyName(
                    options.TargetPid),
                target,
                &captureReady) ||
            !WaitForNamedEvent(
                elysium::capture::SyntheticCaptureStopName(
                    options.TargetPid),
                target,
                &captureStop)) {
            if (handshake != nullptr) {
                UnmapViewOfFile(handshake);
            }
            if (mapping != nullptr) {
                CloseHandle(mapping);
            }
            if (captureReady != nullptr) {
                CloseHandle(captureReady);
            }
            if (captureStop != nullptr) {
                CloseHandle(captureStop);
            }
            CloseHandle(target);
            CloseHandle(stop);
            return 5;
        }
    }

    std::wprintf(
        L"synthetic-collector-v1 event=ready target_pid=%lu\n",
        options.TargetPid);
    std::fflush(stdout);
    LONG capturedNotifications = 0;
    if (options.RequestTargetStop) {
        HANDLE captureWaits[2]{captureReady, target};
        if (WaitForMultipleObjects(
                2,
                captureWaits,
                FALSE,
                5000) != WAIT_OBJECT_0) {
            UnmapViewOfFile(handshake);
            CloseHandle(mapping);
            CloseHandle(captureReady);
            CloseHandle(captureStop);
            CloseHandle(target);
            CloseHandle(stop);
            return 6;
        }
        MemoryBarrier();
        capturedNotifications = handshake->ModuleNotificationCount;
        if (handshake->Magic != elysium::capture::BootstrapMagic ||
            handshake->Version != elysium::capture::BootstrapVersion ||
            capturedNotifications < 3) {
            UnmapViewOfFile(handshake);
            CloseHandle(mapping);
            CloseHandle(captureReady);
            CloseHandle(captureStop);
            CloseHandle(target);
            CloseHandle(stop);
            return 7;
        }
        std::wprintf(
            L"synthetic-collector-v1 event=capture_started "
            L"module_notifications=%ld\n",
            capturedNotifications);
        std::fflush(stdout);
        Sleep(options.RequestTargetStopMs);
        std::wprintf(
            L"synthetic-collector-v1 event=capture_stop_requested\n");
        std::fflush(stdout);
        if (!SetEvent(captureStop)) {
            UnmapViewOfFile(handshake);
            CloseHandle(mapping);
            CloseHandle(captureReady);
            CloseHandle(captureStop);
            CloseHandle(target);
            CloseHandle(stop);
            return 8;
        }
    }

    HANDLE waits[2]{stop, target};
    const DWORD timeout =
        options.ExitAfterMs == 0 ? INFINITE : options.ExitAfterMs;
    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, timeout);
    if (wait != WAIT_OBJECT_0 &&
        wait != WAIT_OBJECT_0 + 1 &&
        wait != WAIT_TIMEOUT) {
        CloseHandle(target);
        CloseHandle(stop);
        return 4;
    }
    bool targetExited =
        WaitForSingleObject(target, 2000) == WAIT_OBJECT_0;
    if (options.RequestTargetStop) {
        MemoryBarrier();
        const LONG finalNotifications =
            handshake->ModuleNotificationCount;
        if (!targetExited ||
            !WriteTrace(options, finalNotifications, targetExited)) {
            UnmapViewOfFile(handshake);
            CloseHandle(mapping);
            CloseHandle(captureReady);
            CloseHandle(captureStop);
            CloseHandle(target);
            CloseHandle(stop);
            return 9;
        }
        std::wprintf(
            L"synthetic-collector-v1 event=trace_finalized "
            L"module_notifications=%ld\n",
            finalNotifications);
        UnmapViewOfFile(handshake);
        CloseHandle(mapping);
        CloseHandle(captureReady);
        CloseHandle(captureStop);
    }
    CloseHandle(target);
    CloseHandle(stop);
    std::wprintf(
        L"synthetic-collector-v1 event=exit code=%lu\n",
        options.ExitCode);
    return static_cast<int>(options.ExitCode);
}
