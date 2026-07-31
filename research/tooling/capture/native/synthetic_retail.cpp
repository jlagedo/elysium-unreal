#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <vector>

namespace {

static_assert(sizeof(void*) == 4, "the synthetic retail process must be 32-bit");

using ModuleNameFn = const wchar_t*(__stdcall*)();
using HookTargetFn = int(__stdcall*)(int);

struct Options {
    DWORD InitialDelayMs = 0;
    DWORD ModuleDelayMs = 25;
    DWORD LifetimeMs = 100;
    DWORD ExitCode = 0;
    const wchar_t* CommandToken = nullptr;
    const wchar_t* ExpectedWorkingDirectory = nullptr;
    const wchar_t* ExpectedModDirectory = nullptr;
    std::array<const wchar_t*, 8> ExpectedEnvironment{};
    std::size_t ExpectedEnvironmentCount = 0;
    const wchar_t* ModDirectory = nullptr;
};

struct ModuleSpec {
    const wchar_t* Filename;
    const wchar_t* Identity;
    int Offset;
};

constexpr std::array<ModuleSpec, 3> Modules{{
    {L"client.dll", L"client.dll", 1000},
    {L"engine.dll", L"engine.dll", 2000},
    {L"StudioRender.dll", L"StudioRender.dll", 3000},
}};

unsigned long Sequence = 0;

long long QpcNow() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

void Emit(const wchar_t* eventName) {
    std::wprintf(
        L"synthetic-retail-v1 sequence=%lu qpc=%lld event=%ls\n",
        ++Sequence,
        QpcNow(),
        eventName);
    std::fflush(stdout);
}

void EmitModule(
    const wchar_t* eventName,
    const wchar_t* module,
    int value) {
    std::wprintf(
        L"synthetic-retail-v1 sequence=%lu qpc=%lld event=%ls "
        L"module=%ls value=%d\n",
        ++Sequence,
        QpcNow(),
        eventName,
        module,
        value);
    std::fflush(stdout);
}

void EmitValue(
    const wchar_t* eventName,
    const wchar_t* value) {
    std::wprintf(
        L"synthetic-retail-v1 sequence=%lu qpc=%lld event=%ls "
        L"value=\"%ls\"\n",
        ++Sequence,
        QpcNow(),
        eventName,
        value);
    std::fflush(stdout);
}

bool ParseDelay(
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

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            std::fwprintf(stderr, L"missing value for %ls\n", argv[index]);
            return false;
        }
        DWORD* destination = nullptr;
        if (std::wcscmp(argv[index], L"--initial-delay-ms") == 0) {
            destination = &options->InitialDelayMs;
        } else if (std::wcscmp(argv[index], L"--module-delay-ms") == 0) {
            destination = &options->ModuleDelayMs;
        } else if (std::wcscmp(argv[index], L"--lifetime-ms") == 0) {
            destination = &options->LifetimeMs;
        } else if (std::wcscmp(argv[index], L"--exit-code") == 0) {
            destination = &options->ExitCode;
        } else if (std::wcscmp(argv[index], L"--command-token") == 0) {
            options->CommandToken = argv[index + 1];
            continue;
        } else if (
            std::wcscmp(argv[index], L"--expect-working-directory") == 0) {
            options->ExpectedWorkingDirectory = argv[index + 1];
            continue;
        } else if (
            std::wcscmp(argv[index], L"--expect-mod-directory") == 0) {
            options->ExpectedModDirectory = argv[index + 1];
            continue;
        } else if (std::wcscmp(argv[index], L"--expect-environment") == 0) {
            if (options->ExpectedEnvironmentCount >=
                options->ExpectedEnvironment.size()) {
                std::fwprintf(stderr, L"too many environment expectations\n");
                return false;
            }
            options->ExpectedEnvironment[
                options->ExpectedEnvironmentCount++] = argv[index + 1];
            continue;
        } else if (std::wcscmp(argv[index], L"-game") == 0) {
            options->ModDirectory = argv[index + 1];
            continue;
        } else {
            std::fwprintf(stderr, L"unknown option: %ls\n", argv[index]);
            return false;
        }
        if (!ParseDelay(argv[index + 1], argv[index], destination)) {
            return false;
        }
        if (destination == &options->ExitCode && options->ExitCode > 255) {
            std::fwprintf(stderr, L"--exit-code must not exceed 255\n");
            return false;
        }
    }
    return true;
}

bool VerifyEnvironment(const wchar_t* expectation) {
    const wchar_t* separator = std::wcschr(expectation, L'=');
    if (separator == nullptr || separator == expectation) {
        std::fwprintf(
            stderr,
            L"invalid environment expectation: %ls\n",
            expectation);
        return false;
    }
    const std::wstring name(expectation, separator);
    const wchar_t* expected = separator + 1;
    const DWORD required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0) {
        std::fwprintf(
            stderr,
            L"missing inherited environment: %ls\n",
            name.c_str());
        return false;
    }
    std::vector<wchar_t> value(required);
    if (GetEnvironmentVariableW(
            name.c_str(),
            value.data(),
            required) >= required ||
        std::wcscmp(value.data(), expected) != 0) {
        std::fwprintf(
            stderr,
            L"environment mismatch for %ls\n",
            name.c_str());
        return false;
    }
    return true;
}

bool CanonicalPath(const wchar_t* input, wchar_t* output, DWORD capacity) {
    const DWORD length = GetFullPathNameW(input, capacity, output, nullptr);
    return length != 0 && length < capacity;
}

bool VerifyStartup(const Options& options) {
    if (options.ExpectedModDirectory != nullptr &&
        (options.ModDirectory == nullptr ||
         std::wcscmp(
             options.ModDirectory,
             options.ExpectedModDirectory) != 0)) {
        std::fwprintf(stderr, L"mod-directory startup mismatch\n");
        return false;
    }
    if (options.ExpectedWorkingDirectory != nullptr) {
        wchar_t current[32768]{};
        wchar_t expected[32768]{};
        if (GetCurrentDirectoryW(
                static_cast<DWORD>(std::size(current)),
                current) == 0 ||
            !CanonicalPath(
                options.ExpectedWorkingDirectory,
                expected,
                static_cast<DWORD>(std::size(expected))) ||
            _wcsicmp(current, expected) != 0) {
            std::fwprintf(stderr, L"working-directory startup mismatch\n");
            return false;
        }
    }
    for (std::size_t index = 0;
         index < options.ExpectedEnvironmentCount;
         ++index) {
        if (!VerifyEnvironment(options.ExpectedEnvironment[index])) {
            return false;
        }
    }
    return true;
}

bool ExecutableDirectory(wchar_t* destination, DWORD capacity) {
    const DWORD length = GetModuleFileNameW(nullptr, destination, capacity);
    if (length == 0 || length >= capacity) {
        return false;
    }
    wchar_t* separator = std::wcsrchr(destination, L'\\');
    if (separator == nullptr) {
        return false;
    }
    separator[1] = L'\0';
    return true;
}

template <typename Function>
Function Export(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

}  // namespace

extern "C" __declspec(dllexport) __declspec(noinline) int __stdcall
ElysiumSyntheticExecutableHookTarget(int value) noexcept {
    volatile int stableValue = value + 4000;
    return stableValue;
}

int wmain(int argc, wchar_t** argv) {
    Options options{};
    if (!ParseOptions(argc, argv, &options)) {
        std::fwprintf(
            stderr,
            L"usage: vampire.exe [--initial-delay-ms N] "
            L"[--module-delay-ms N] [--lifetime-ms N]\n");
        return 2;
    }
    if (!VerifyStartup(options)) {
        return 3;
    }

    wchar_t executableDirectory[32768]{};
    if (!ExecutableDirectory(
            executableDirectory,
            static_cast<DWORD>(std::size(executableDirectory)))) {
        std::fwprintf(stderr, L"cannot resolve executable directory\n");
        return 3;
    }

    Emit(L"process_started");
    if (options.ModDirectory != nullptr) {
        EmitValue(L"mod_directory", options.ModDirectory);
    }
    if (options.CommandToken != nullptr) {
        EmitValue(L"command_token", options.CommandToken);
    }
    const int processTarget = ElysiumSyntheticExecutableHookTarget(17);
    if (processTarget != 4017) {
        std::fwprintf(stderr, L"executable hook target failed\n");
        return 4;
    }
    EmitModule(L"hook_target_ready", L"vampire.exe", processTarget);
    Sleep(options.InitialDelayMs);

    std::array<HMODULE, Modules.size()> loaded{};
    for (std::size_t index = 0; index < Modules.size(); ++index) {
        Sleep(options.ModuleDelayMs);
        wchar_t path[32768]{};
        const int written = _snwprintf_s(
            path,
            std::size(path),
            _TRUNCATE,
            L"%ls%ls",
            executableDirectory,
            Modules[index].Filename);
        if (written < 0) {
            std::fwprintf(stderr, L"module path is too long\n");
            return 5;
        }

        loaded[index] = LoadLibraryW(path);
        if (loaded[index] == nullptr) {
            std::fwprintf(
                stderr,
                L"LoadLibraryW failed for %ls: %lu\n",
                path,
                GetLastError());
            return 6;
        }

        const ModuleNameFn moduleName =
            Export<ModuleNameFn>(loaded[index], "ElysiumSyntheticModuleName");
        const HookTargetFn hookTarget =
            Export<HookTargetFn>(loaded[index], "ElysiumSyntheticHookTarget");
        if (moduleName == nullptr || hookTarget == nullptr ||
            std::wcscmp(moduleName(), Modules[index].Identity) != 0) {
            std::fwprintf(
                stderr,
                L"synthetic exports failed for %ls\n",
                Modules[index].Filename);
            return 7;
        }

        const int input = static_cast<int>(index) + 17;
        const int value = hookTarget(input);
        if (value != Modules[index].Offset + input) {
            std::fwprintf(
                stderr,
                L"hook target returned an invalid value for %ls\n",
                Modules[index].Filename);
            return 8;
        }
        EmitModule(L"module_loaded", moduleName(), value);
    }

    Emit(L"all_modules_ready");
    Sleep(options.LifetimeMs);

    for (std::size_t index = loaded.size(); index > 0; --index) {
        const std::size_t moduleIndex = index - 1;
        if (!FreeLibrary(loaded[moduleIndex])) {
            std::fwprintf(
                stderr,
                L"FreeLibrary failed for %ls: %lu\n",
                Modules[moduleIndex].Filename,
                GetLastError());
            return 9;
        }
        EmitModule(
            L"module_unloaded",
            Modules[moduleIndex].Filename,
            static_cast<int>(moduleIndex));
    }

    std::wprintf(
        L"synthetic-retail-v1 sequence=%lu qpc=%lld "
        L"event=shutdown_complete modules=%zu\n",
        ++Sequence,
        QpcNow(),
        Modules.size());
    std::fflush(stdout);
    return static_cast<int>(options.ExitCode);
}
