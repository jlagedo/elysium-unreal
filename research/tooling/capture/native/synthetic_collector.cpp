#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>

namespace {

struct Options {
    const wchar_t* StopEvent = nullptr;
    DWORD TargetPid = 0;
    DWORD ExitAfterMs = 0;
    DWORD ExitCode = 0;
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
        } else {
            return false;
        }
    }
    return options->StopEvent != nullptr && options->TargetPid != 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options{};
    if (!ParseOptions(argc, argv, &options)) {
        std::fwprintf(stderr, L"invalid synthetic collector arguments\n");
        return 2;
    }
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, options.StopEvent);
    HANDLE target = OpenProcess(SYNCHRONIZE, FALSE, options.TargetPid);
    if (stop == nullptr || target == nullptr) {
        if (stop != nullptr) {
            CloseHandle(stop);
        }
        if (target != nullptr) {
            CloseHandle(target);
        }
        return 3;
    }

    std::wprintf(
        L"synthetic-collector-v1 event=ready target_pid=%lu\n",
        options.TargetPid);
    std::fflush(stdout);
    HANDLE waits[2]{stop, target};
    const DWORD timeout =
        options.ExitAfterMs == 0 ? INFINITE : options.ExitAfterMs;
    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, timeout);
    CloseHandle(target);
    CloseHandle(stop);
    if (wait != WAIT_OBJECT_0 &&
        wait != WAIT_OBJECT_0 + 1 &&
        wait != WAIT_TIMEOUT) {
        return 4;
    }
    std::wprintf(
        L"synthetic-collector-v1 event=exit code=%lu\n",
        options.ExitCode);
    return static_cast<int>(options.ExitCode);
}
