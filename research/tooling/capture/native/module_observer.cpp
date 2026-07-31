#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

#include "module_observer.h"

namespace elysium::capture {
namespace {

constexpr ULONG DllNotificationLoaded = 1;
constexpr ULONG DllNotificationUnloaded = 2;
constexpr std::size_t QueueCapacity = 128;
constexpr std::size_t ModulePathCapacity = 32768;
constexpr std::size_t ModuleNameCapacity = 512;

struct DllLoadedNotificationData {
    ULONG Flags;
    const UNICODE_STRING* FullDllName;
    const UNICODE_STRING* BaseDllName;
    void* DllBase;
    ULONG SizeOfImage;
};

struct DllUnloadedNotificationData {
    ULONG Flags;
    const UNICODE_STRING* FullDllName;
    const UNICODE_STRING* BaseDllName;
    void* DllBase;
    ULONG SizeOfImage;
};

union DllNotificationData {
    DllLoadedNotificationData Loaded;
    DllUnloadedNotificationData Unloaded;
};

using DllNotificationCallback = void(CALLBACK*)(
    ULONG reason,
    const DllNotificationData* data,
    void* context);
using RegisterDllNotificationFn = LONG(NTAPI*)(
    ULONG flags,
    DllNotificationCallback callback,
    void* context,
    void** cookie);
using UnregisterDllNotificationFn = LONG(NTAPI*)(void* cookie);

template <typename Function>
Function Export(HMODULE module, const char* name) noexcept {
    const FARPROC address =
        module == nullptr ? nullptr : GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

void ZeroCounters(ModuleObserverCounters* counters) noexcept {
    InterlockedExchange(&counters->BootstrapEventCount, 0);
    InterlockedExchange(&counters->LoadEventCount, 0);
    InterlockedExchange(&counters->UnloadEventCount, 0);
    InterlockedExchange(&counters->ProcessedEventCount, 0);
    InterlockedExchange(&counters->HashSuccessCount, 0);
    InterlockedExchange(&counters->HashFailureCount, 0);
    InterlockedExchange(&counters->ActivationDispatchCount, 0);
    InterlockedExchange(&counters->QueueDropCount, 0);
    InterlockedExchange(&counters->PathTruncationCount, 0);
    InterlockedExchange(&counters->WorkerThreadId, 0);
}

bool IsLoadEvent(ModuleEventKind kind) noexcept {
    return kind == ModuleEventKind::Bootstrap ||
        kind == ModuleEventKind::Loaded;
}

bool ModuleStillLoaded(std::uintptr_t imageBase) noexcept {
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(imageBase);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address,
            &module)) {
        return false;
    }
    return reinterpret_cast<std::uintptr_t>(module) == imageBase;
}

template <typename Value>
Value ReadLittleEndian(const std::uint8_t* data) noexcept {
    Value value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool ReadExactAt(
    HANDLE file,
    std::uint64_t offset,
    void* destination,
    DWORD bytes) noexcept {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
        return false;
    }
    DWORD read = 0;
    return ReadFile(file, destination, bytes, &read, nullptr) &&
        read == bytes;
}

bool ReadPeIdentity(
    HANDLE file,
    std::uint64_t* fileSize,
    PeIdentity* identity) noexcept {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        return false;
    }
    *fileSize = static_cast<std::uint64_t>(size.QuadPart);

    std::array<std::uint8_t, 64> dos{};
    if (!ReadExactAt(
            file,
            0,
            dos.data(),
            static_cast<DWORD>(dos.size())) ||
        dos[0] != 'M' ||
        dos[1] != 'Z') {
        return false;
    }
    const std::uint32_t peOffset =
        ReadLittleEndian<std::uint32_t>(dos.data() + 0x3c);
    std::array<std::uint8_t, 24> fileHeader{};
    if (!ReadExactAt(
            file,
            peOffset,
            fileHeader.data(),
            static_cast<DWORD>(fileHeader.size())) ||
        std::memcmp(fileHeader.data(), "PE\0\0", 4) != 0) {
        return false;
    }
    const std::uint16_t optionalBytes =
        ReadLittleEndian<std::uint16_t>(fileHeader.data() + 20);
    if (optionalBytes < 68) {
        return false;
    }
    std::array<std::uint8_t, 68> optional{};
    if (!ReadExactAt(
            file,
            static_cast<std::uint64_t>(peOffset) + fileHeader.size(),
            optional.data(),
            static_cast<DWORD>(optional.size()))) {
        return false;
    }

    identity->Machine =
        ReadLittleEndian<std::uint16_t>(fileHeader.data() + 4);
    identity->SectionCount =
        ReadLittleEndian<std::uint16_t>(fileHeader.data() + 6);
    identity->Timestamp =
        ReadLittleEndian<std::uint32_t>(fileHeader.data() + 8);
    identity->Characteristics =
        ReadLittleEndian<std::uint16_t>(fileHeader.data() + 22);
    identity->OptionalMagic =
        ReadLittleEndian<std::uint16_t>(optional.data());
    if (identity->OptionalMagic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return false;
    }
    identity->PreferredImageBase =
        ReadLittleEndian<std::uint32_t>(optional.data() + 28);
    identity->SizeOfImage =
        ReadLittleEndian<std::uint32_t>(optional.data() + 56);
    identity->Checksum =
        ReadLittleEndian<std::uint32_t>(optional.data() + 64);
    return true;
}

HANDLE CreateModuleSnapshot() noexcept {
    constexpr unsigned int MaximumAttempts = 16;
    for (unsigned int attempt = 0; attempt < MaximumAttempts; ++attempt) {
        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE,
            GetCurrentProcessId());
        if (snapshot != INVALID_HANDLE_VALUE) {
            return snapshot;
        }
        if (GetLastError() != ERROR_BAD_LENGTH) {
            return INVALID_HANDLE_VALUE;
        }
        Sleep(1);
    }
    SetLastError(ERROR_BAD_LENGTH);
    return INVALID_HANDLE_VALUE;
}

bool HashFile(
    BCRYPT_ALG_HANDLE algorithm,
    const wchar_t* path,
    std::uint64_t* fileSize,
    PeIdentity* identity,
    bool* identitySucceeded,
    std::array<std::uint8_t, 32>* digest) noexcept {
    HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    *identitySucceeded = ReadPeIdentity(file, fileSize, identity);
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        CloseHandle(file);
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptCreateHash(
        algorithm,
        &hash,
        nullptr,
        0,
        nullptr,
        0,
        0);
    std::array<UCHAR, 64 * 1024> buffer{};
    while (status >= 0) {
        DWORD bytesRead = 0;
        if (!ReadFile(
                file,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                nullptr)) {
            status = static_cast<NTSTATUS>(-1);
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
    }
    if (status >= 0) {
        status = BCryptFinishHash(
            hash,
            digest->data(),
            static_cast<ULONG>(digest->size()),
            0);
    }
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    CloseHandle(file);
    return status >= 0;
}

}  // namespace

struct ModuleObserver::Impl {
#pragma warning(push)
#pragma warning(disable : 4324)
    struct alignas(MEMORY_ALLOCATION_ALIGNMENT) QueueEntry {
        SLIST_ENTRY Link;
        ModuleEventKind Kind;
        std::uintptr_t ImageBase;
        std::uint32_t ImageSize;
        std::uint16_t PathCharacters;
        std::uint16_t ModuleNameCharacters;
        bool PathTruncated;
        bool ModuleNameTruncated;
        wchar_t ModuleName[ModuleNameCapacity];
        wchar_t Path[ModulePathCapacity];
    };
#pragma warning(pop)

    SLIST_HEADER FreeEntries{};
    SLIST_HEADER PendingEntries{};
    std::array<QueueEntry, QueueCapacity> Entries{};
    HANDLE WorkEvent = nullptr;
    HANDLE StopEvent = nullptr;
    HANDLE WorkerThread = nullptr;
    void* NotificationCookie = nullptr;
    UnregisterDllNotificationFn UnregisterNotification = nullptr;
    ModuleObserverCounters* Counters = nullptr;
    volatile LONG* NotificationCount = nullptr;
    ModuleEventProcessor Processor = nullptr;
    void* ProcessorContext = nullptr;
    std::unordered_set<std::uintptr_t> ObservedModules;
    std::unordered_set<std::uintptr_t> ActiveModules;
    volatile LONG OutstandingEvents = 0;
    volatile LONG WorkerBusy = 0;
    volatile LONG StopRequested = 0;

    static void CALLBACK OnDllNotification(
        ULONG reason,
        const DllNotificationData* data,
        void* context) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || data == nullptr ||
            InterlockedCompareExchange(
                &self->StopRequested,
                0,
                0) != 0) {
            return;
        }

        if (reason == DllNotificationLoaded) {
            self->EnqueueNotification(
                ModuleEventKind::Loaded,
                data->Loaded.DllBase,
                data->Loaded.SizeOfImage,
                data->Loaded.FullDllName,
                data->Loaded.BaseDllName);
        } else if (reason == DllNotificationUnloaded) {
            self->EnqueueNotification(
                ModuleEventKind::Unloaded,
                data->Unloaded.DllBase,
                data->Unloaded.SizeOfImage,
                data->Unloaded.FullDllName,
                data->Unloaded.BaseDllName);
        }
    }

    static DWORD WINAPI WorkerMain(void* context) noexcept {
        auto* self = static_cast<Impl*>(context);
        InterlockedExchange(
            &self->Counters->WorkerThreadId,
            static_cast<LONG>(GetCurrentThreadId()));

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        const NTSTATUS algorithmStatus = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0);

        HANDLE waits[2]{self->StopEvent, self->WorkEvent};
        bool stopping = false;
        while (!stopping ||
               InterlockedCompareExchange(
                   &self->OutstandingEvents,
                   0,
                   0) != 0) {
            const DWORD wait = WaitForMultipleObjects(
                2,
                waits,
                FALSE,
                stopping ? 0 : 250);
            if (wait == WAIT_OBJECT_0) {
                stopping = true;
            } else if (wait != WAIT_OBJECT_0 + 1 &&
                       wait != WAIT_TIMEOUT) {
                break;
            }
            InterlockedExchange(&self->WorkerBusy, 1);
            self->Drain(algorithmStatus >= 0 ? algorithm : nullptr);
            if (!stopping) {
                self->Reconcile(
                    algorithmStatus >= 0 ? algorithm : nullptr);
            }
            InterlockedExchange(&self->WorkerBusy, 0);
        }
        InterlockedExchange(&self->WorkerBusy, 1);
        self->Drain(algorithmStatus >= 0 ? algorithm : nullptr);
        InterlockedExchange(&self->WorkerBusy, 0);
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        return 0;
    }

    void EnqueueNotification(
        ModuleEventKind kind,
        const void* imageBase,
        std::uint32_t imageSize,
        const UNICODE_STRING* path,
        const UNICODE_STRING* moduleName) noexcept {
        const wchar_t* characters =
            path == nullptr ? nullptr : path->Buffer;
        const std::size_t characterCount =
            path == nullptr ? 0 : path->Length / sizeof(wchar_t);
        const wchar_t* moduleCharacters =
            moduleName == nullptr ? nullptr : moduleName->Buffer;
        const std::size_t moduleCharacterCount =
            moduleName == nullptr
                ? 0
                : moduleName->Length / sizeof(wchar_t);
        if (!TryEnqueue(
                kind,
                imageBase,
                imageSize,
                characters,
                characterCount,
                moduleCharacters,
                moduleCharacterCount)) {
            InterlockedIncrement(&Counters->QueueDropCount);
        }
    }

    bool TryEnqueue(
        ModuleEventKind kind,
        const void* imageBase,
        std::uint32_t imageSize,
        const wchar_t* path,
        std::size_t pathCharacters,
        const wchar_t* moduleName,
        std::size_t moduleNameCharacters) noexcept {
        PSLIST_ENTRY freeLink =
            InterlockedPopEntrySList(&FreeEntries);
        if (freeLink == nullptr) {
            return false;
        }

        auto* entry = CONTAINING_RECORD(
            freeLink,
            QueueEntry,
            Link);
        entry->Kind = kind;
        entry->ImageBase =
            reinterpret_cast<std::uintptr_t>(imageBase);
        entry->ImageSize = imageSize;
        const std::size_t copiedCharacters = std::min(
            pathCharacters,
            ModulePathCapacity - 1);
        entry->PathCharacters =
            static_cast<std::uint16_t>(copiedCharacters);
        entry->PathTruncated =
            pathCharacters > copiedCharacters;
        if (path != nullptr && copiedCharacters != 0) {
            std::memcpy(
                entry->Path,
                path,
                copiedCharacters * sizeof(wchar_t));
        }
        entry->Path[copiedCharacters] = L'\0';
        const std::size_t copiedModuleNameCharacters = std::min(
            moduleNameCharacters,
            ModuleNameCapacity - 1);
        entry->ModuleNameCharacters =
            static_cast<std::uint16_t>(copiedModuleNameCharacters);
        entry->ModuleNameTruncated =
            moduleNameCharacters > copiedModuleNameCharacters;
        if (moduleName != nullptr &&
            copiedModuleNameCharacters != 0) {
            std::memcpy(
                entry->ModuleName,
                moduleName,
                copiedModuleNameCharacters * sizeof(wchar_t));
        }
        entry->ModuleName[copiedModuleNameCharacters] = L'\0';

        if (entry->PathTruncated || entry->ModuleNameTruncated) {
            InterlockedIncrement(
                &Counters->PathTruncationCount);
        }
        if (kind == ModuleEventKind::Bootstrap) {
            InterlockedIncrement(
                &Counters->BootstrapEventCount);
        } else if (kind == ModuleEventKind::Loaded) {
            InterlockedIncrement(&Counters->LoadEventCount);
            if (NotificationCount != nullptr) {
                InterlockedIncrement(NotificationCount);
            }
        } else {
            InterlockedIncrement(&Counters->UnloadEventCount);
            if (NotificationCount != nullptr) {
                InterlockedIncrement(NotificationCount);
            }
        }
        InterlockedIncrement(&OutstandingEvents);
        InterlockedPushEntrySList(&PendingEntries, &entry->Link);
        SetEvent(WorkEvent);
        return true;
    }

    void Drain(BCRYPT_ALG_HANDLE algorithm) noexcept {
        PSLIST_ENTRY pending =
            InterlockedFlushSList(&PendingEntries);
        PSLIST_ENTRY ordered = nullptr;
        while (pending != nullptr) {
            PSLIST_ENTRY next = pending->Next;
            pending->Next = ordered;
            ordered = pending;
            pending = next;
        }

        while (ordered != nullptr) {
            PSLIST_ENTRY next = ordered->Next;
            auto* entry = CONTAINING_RECORD(
                ordered,
                QueueEntry,
                Link);
            Process(*entry, algorithm);
            InterlockedPushEntrySList(
                &FreeEntries,
                &entry->Link);
            InterlockedDecrement(&OutstandingEvents);
            ordered = next;
        }
    }

    void Reconcile(BCRYPT_ALG_HANDLE algorithm) noexcept {
        HANDLE snapshot = CreateModuleSnapshot();
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (!Module32FirstW(snapshot, &module)) {
            CloseHandle(snapshot);
            return;
        }

        std::unordered_set<std::uintptr_t> current;
        BOOL hasModule = TRUE;
        do {
            const auto imageBase =
                reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            current.insert(imageBase);
            if (ObservedModules.find(imageBase) ==
                ObservedModules.end()) {
                QueueEntry entry{};
                entry.Kind = ModuleEventKind::Loaded;
                entry.ImageBase = imageBase;
                entry.ImageSize = module.modBaseSize;
                const std::size_t pathCharacters = wcsnlen_s(
                    module.szExePath,
                    _countof(module.szExePath));
                entry.PathCharacters =
                    static_cast<std::uint16_t>(pathCharacters);
                std::memcpy(
                    entry.Path,
                    module.szExePath,
                    pathCharacters * sizeof(wchar_t));
                entry.Path[pathCharacters] = L'\0';
                const std::size_t moduleNameCharacters = wcsnlen_s(
                    module.szModule,
                    _countof(module.szModule));
                entry.ModuleNameCharacters =
                    static_cast<std::uint16_t>(
                        moduleNameCharacters);
                std::memcpy(
                    entry.ModuleName,
                    module.szModule,
                    moduleNameCharacters * sizeof(wchar_t));
                entry.ModuleName[moduleNameCharacters] = L'\0';
                InterlockedIncrement(&Counters->LoadEventCount);
                Process(entry, algorithm);
            }
            hasModule = Module32NextW(snapshot, &module);
        } while (hasModule);
        CloseHandle(snapshot);

        std::vector<std::uintptr_t> missing;
        for (const std::uintptr_t imageBase : ObservedModules) {
            if (current.find(imageBase) == current.end()) {
                missing.push_back(imageBase);
            }
        }
        for (const std::uintptr_t imageBase : missing) {
            QueueEntry entry{};
            entry.Kind = ModuleEventKind::Unloaded;
            entry.ImageBase = imageBase;
            InterlockedIncrement(&Counters->UnloadEventCount);
            Process(entry, algorithm);
        }
    }

    void Process(
        const QueueEntry& entry,
        BCRYPT_ALG_HANDLE algorithm) noexcept {
        ProcessedModuleEvent processed{
            entry.Kind,
            entry.ImageBase,
            entry.ImageSize,
            0,
            {},
            entry.ModuleName,
            entry.ModuleNameCharacters,
            entry.Path,
            entry.PathCharacters,
            entry.PathTruncated,
            false,
            false,
            false,
            {},
        };

        const bool loadEvent = IsLoadEvent(entry.Kind);
        processed.Duplicate =
            loadEvent &&
            ObservedModules.find(entry.ImageBase) !=
                ObservedModules.end();
        const bool loaded =
            loadEvent &&
            ModuleStillLoaded(entry.ImageBase);
        std::array<wchar_t, ModulePathCapacity> canonicalPath{};
        if (loaded) {
            const DWORD canonicalCharacters = GetModuleFileNameW(
                reinterpret_cast<HMODULE>(entry.ImageBase),
                canonicalPath.data(),
                static_cast<DWORD>(canonicalPath.size()));
            if (canonicalCharacters != 0 &&
                canonicalCharacters < canonicalPath.size()) {
                processed.Path = canonicalPath.data();
                processed.PathCharacters = canonicalCharacters;
                const wchar_t* slash =
                    std::wcsrchr(canonicalPath.data(), L'\\');
                const wchar_t* forwardSlash =
                    std::wcsrchr(canonicalPath.data(), L'/');
                const wchar_t* separator = slash;
                if (forwardSlash != nullptr &&
                    (separator == nullptr ||
                     forwardSlash > separator)) {
                    separator = forwardSlash;
                }
                processed.ModuleName =
                    separator == nullptr
                        ? canonicalPath.data()
                        : separator + 1;
                processed.ModuleNameCharacters =
                    std::wcslen(processed.ModuleName);
            }
        }
        if (loaded && algorithm != nullptr &&
            processed.PathCharacters != 0) {
            processed.HashSucceeded =
                HashFile(
                    algorithm,
                    processed.Path,
                    &processed.FileSize,
                    &processed.Pe,
                    &processed.IdentitySucceeded,
                    &processed.Sha256);
            InterlockedIncrement(
                processed.HashSucceeded
                    ? &Counters->HashSuccessCount
                    : &Counters->HashFailureCount);
        } else if (loadEvent) {
            InterlockedIncrement(&Counters->HashFailureCount);
        }

        if (loaded && processed.HashSucceeded &&
            processed.IdentitySucceeded) {
            ActiveModules.insert(entry.ImageBase);
            InterlockedIncrement(
                &Counters->ActivationDispatchCount);
        }
        if (loaded) {
            ObservedModules.insert(entry.ImageBase);
        } else if (entry.Kind == ModuleEventKind::Unloaded) {
            ObservedModules.erase(entry.ImageBase);
            ActiveModules.erase(entry.ImageBase);
        }
        if (Processor != nullptr) {
            Processor(processed, ProcessorContext);
        }
        InterlockedIncrement(&Counters->ProcessedEventCount);
    }
};

ModuleObserver::~ModuleObserver() {
    Stop();
}

bool ModuleObserver::Start(
    ModuleObserverCounters* counters,
    volatile LONG* notificationCount,
    ModuleEventProcessor processor,
    void* processorContext,
    DWORD* errorCode) noexcept {
    if (errorCode != nullptr) {
        *errorCode = ERROR_SUCCESS;
    }
    if (Implementation_ != nullptr || counters == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    Impl* implementation = new (std::nothrow) Impl();
    if (implementation == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = ERROR_OUTOFMEMORY;
        }
        return false;
    }
    Implementation_ = implementation;
    implementation->Counters = counters;
    implementation->NotificationCount = notificationCount;
    implementation->Processor = processor;
    implementation->ProcessorContext = processorContext;
    ZeroCounters(counters);
    if (notificationCount != nullptr) {
        InterlockedExchange(notificationCount, 0);
    }

    InitializeSListHead(&implementation->FreeEntries);
    InitializeSListHead(&implementation->PendingEntries);
    for (auto& entry : implementation->Entries) {
        InterlockedPushEntrySList(
            &implementation->FreeEntries,
            &entry.Link);
    }
    implementation->WorkEvent =
        CreateEventW(nullptr, FALSE, FALSE, nullptr);
    implementation->StopEvent =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (implementation->WorkEvent == nullptr ||
        implementation->StopEvent == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = GetLastError();
        }
        Stop();
        return false;
    }
    implementation->WorkerThread = CreateThread(
        nullptr,
        0,
        Impl::WorkerMain,
        implementation,
        0,
        nullptr);
    if (implementation->WorkerThread == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = GetLastError();
        }
        Stop();
        return false;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const RegisterDllNotificationFn registerNotification =
        Export<RegisterDllNotificationFn>(
            ntdll,
            "LdrRegisterDllNotification");
    implementation->UnregisterNotification =
        Export<UnregisterDllNotificationFn>(
            ntdll,
            "LdrUnregisterDllNotification");
    if (registerNotification == nullptr ||
        implementation->UnregisterNotification == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = ERROR_PROC_NOT_FOUND;
        }
        Stop();
        return false;
    }
    const LONG status = registerNotification(
        0,
        Impl::OnDllNotification,
        implementation,
        &implementation->NotificationCookie);
    if (status < 0) {
        if (errorCode != nullptr) {
            *errorCode = static_cast<DWORD>(status);
        }
        Stop();
        return false;
    }

    HANDLE snapshot = CreateModuleSnapshot();
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (errorCode != nullptr) {
            *errorCode = GetLastError();
        }
        Stop();
        return false;
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot, &module)) {
        if (errorCode != nullptr) {
            *errorCode = GetLastError();
        }
        CloseHandle(snapshot);
        Stop();
        return false;
    }
    BOOL hasModule = TRUE;
    do {
        std::array<wchar_t, ModulePathCapacity> fullPath{};
        const DWORD fullPathCharacters = GetModuleFileNameW(
            reinterpret_cast<HMODULE>(module.modBaseAddr),
            fullPath.data(),
            static_cast<DWORD>(fullPath.size()));
        const wchar_t* observedPath =
            fullPathCharacters == 0
                ? module.szExePath
                : fullPath.data();
        const std::size_t observedPathCharacters =
            fullPathCharacters == 0
                ? wcsnlen_s(
                      module.szExePath,
                      _countof(module.szExePath))
                : std::min<std::size_t>(
                      fullPathCharacters,
                      fullPath.size());
        while (!implementation->TryEnqueue(
                   ModuleEventKind::Bootstrap,
                   module.modBaseAddr,
                   module.modBaseSize,
                   observedPath,
                   observedPathCharacters,
                   module.szModule,
                   wcsnlen_s(
                       module.szModule,
                       _countof(module.szModule)))) {
            Sleep(1);
        }
        hasModule = Module32NextW(snapshot, &module);
    } while (hasModule);
    const DWORD enumerationError = GetLastError();
    CloseHandle(snapshot);
    if (enumerationError != ERROR_NO_MORE_FILES) {
        if (errorCode != nullptr) {
            *errorCode = enumerationError;
        }
        Stop();
        return false;
    }
    return true;
}

bool ModuleObserver::WaitUntilIdle(
    DWORD timeoutMilliseconds) const noexcept {
    if (Implementation_ == nullptr) {
        return false;
    }
    const ULONGLONG start = GetTickCount64();
    while (InterlockedCompareExchange(
               &Implementation_->OutstandingEvents,
               0,
               0) != 0 ||
           InterlockedCompareExchange(
               &Implementation_->WorkerBusy,
               0,
               0) != 0) {
        if (GetTickCount64() - start >= timeoutMilliseconds) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

void ModuleObserver::RequestStop() noexcept {
    if (Implementation_ == nullptr) {
        return;
    }
    InterlockedExchange(
        &Implementation_->StopRequested,
        1);
    if (Implementation_->StopEvent != nullptr) {
        SetEvent(Implementation_->StopEvent);
    }
}

void ModuleObserver::Stop() noexcept {
    Impl* implementation = Implementation_;
    if (implementation == nullptr) {
        return;
    }
    InterlockedExchange(&implementation->StopRequested, 1);
    if (implementation->NotificationCookie != nullptr &&
        implementation->UnregisterNotification != nullptr) {
        implementation->UnregisterNotification(
            implementation->NotificationCookie);
        implementation->NotificationCookie = nullptr;
    }
    if (implementation->StopEvent != nullptr) {
        SetEvent(implementation->StopEvent);
    }
    if (implementation->WorkerThread != nullptr) {
        WaitForSingleObject(
            implementation->WorkerThread,
            INFINITE);
        CloseHandle(implementation->WorkerThread);
    }
    if (implementation->WorkEvent != nullptr) {
        CloseHandle(implementation->WorkEvent);
    }
    if (implementation->StopEvent != nullptr) {
        CloseHandle(implementation->StopEvent);
    }
    delete implementation;
    Implementation_ = nullptr;
}

}  // namespace elysium::capture
