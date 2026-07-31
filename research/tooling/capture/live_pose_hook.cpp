#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr DWORD kStudioObjectRva = 0x82B30;
constexpr DWORD kStudioVtableRva = 0x6C150;
constexpr DWORD kDrawModelRva = 0x4F00;
constexpr DWORD kDrawModelSlot = 0x58 / sizeof(void*);
constexpr DWORD kResolveVirtualModelPoseRva = 0x968A0;
constexpr DWORD kBuildTransformationsRva = 0x8FD00;
constexpr DWORD kGetStudioHdrRva = 0x8F900;
constexpr int kMaximumBones = 1024;
constexpr DWORD kInlinePatchBytes = 5;

#pragma pack(push, 1)
struct FileHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint64_t qpcFrequency;
    std::int64_t startQpc;
    std::uint32_t pid;
    std::uint32_t studioRenderBase;
    std::uint32_t studioObject;
    std::uint32_t studioVtable;
    std::uint32_t drawModelRva;
    char studioRenderSha256[65];
    char reserved[11];
};

struct PoseRecordHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t studioHdr;
    std::uint32_t clientEntity;
    std::uint32_t checksum;
    std::uint32_t boneCount;
    std::uint32_t modelInfo;
    std::uint32_t drawArguments[5];
    char modelName[64];
};

struct AnimationFileHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint64_t qpcFrequency;
    std::int64_t startQpc;
    std::uint32_t pid;
    std::uint32_t clientBase;
    std::uint32_t resolveVirtualModelPoseRva;
    std::uint32_t buildTransformationsRva;
    std::uint32_t targetChecksum;
    char clientSha256[65];
    char reserved[11];
};

struct AnimationRecordHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t clientEntity;
    std::uint32_t studioHdr;
    std::uint32_t checksum;
    std::uint32_t boneCount;
    std::int32_t studioSequence;
    float samplePhase;
    float entityCycle;
    std::int32_t result;
    std::uint32_t positions;
    std::uint32_t quaternions;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 128, "capture file header changed");
static_assert(sizeof(PoseRecordHeader) == 132, "pose record header changed");
static_assert(
    sizeof(AnimationFileHeader) == 128,
    "animation capture file header changed");
static_assert(
    sizeof(AnimationRecordHeader) == 68,
    "animation record header changed");

struct PendingRecord {
    PendingRecord* next;
    DWORD bytes;
    bool animation;
    unsigned char payload[1];
};

using DrawModelFn = std::uintptr_t(__thiscall*)(
    void*, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t);
using ResolveVirtualModelPoseFn = int(__thiscall*)(
    void*, float*, float*, int, float, void*, void*);
using BuildTransformationsFn = void(__thiscall*)(
    void*, float*, float*, float*, void*);
using GetStudioHdrFn = unsigned char*(__thiscall*)(void*, int);

struct InlineHook {
    unsigned char* target = nullptr;
    unsigned char original[kInlinePatchBytes]{};
    unsigned char* trampoline = nullptr;
};

HMODULE gSelf = nullptr;
DrawModelFn gOriginalDrawModel = nullptr;
ResolveVirtualModelPoseFn gOriginalResolveVirtualModelPose = nullptr;
BuildTransformationsFn gOriginalBuildTransformations = nullptr;
GetStudioHdrFn gGetStudioHdr = nullptr;
void** gDrawModelSlot = nullptr;
InlineHook gResolveVirtualModelPoseHook;
InlineHook gBuildTransformationsHook;
HANDLE gOutput = INVALID_HANDLE_VALUE;
HANDLE gAnimationOutput = INVALID_HANDLE_VALUE;
HANDLE gWake = nullptr;
CRITICAL_SECTION gQueueLock;
PendingRecord* gQueueHead = nullptr;
PendingRecord* gQueueTail = nullptr;
volatile LONG gCapturing = 0;
volatile LONG gActiveHooks = 0;
volatile LONG gSequence = 0;
volatile LONG gQueued = 0;
volatile LONG gWritten = 0;
volatile LONG gDropped = 0;
LARGE_INTEGER gStartQpc{};
wchar_t gReadyPath[MAX_PATH * 4]{};
wchar_t gStopPath[MAX_PATH * 4]{};
wchar_t gDonePath[MAX_PATH * 4]{};
DWORD gDurationSeconds = 300;
DWORD gTargetChecksum = 0;

bool WriteAll(HANDLE file, const void* data, DWORD bytes) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    while (bytes) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, bytes, &written, nullptr) || !written) {
            return false;
        }
        cursor += written;
        bytes -= written;
    }
    return true;
}

void WriteMarker(const wchar_t* path, const char* text) {
    HANDLE file = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteAll(file, text, static_cast<DWORD>(std::strlen(text)));
    CloseHandle(file);
}

bool Exists(const wchar_t* path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

void Enqueue(PendingRecord* record) {
    EnterCriticalSection(&gQueueLock);
    if (gQueueTail) {
        gQueueTail->next = record;
    } else {
        gQueueHead = record;
    }
    gQueueTail = record;
    LeaveCriticalSection(&gQueueLock);
    InterlockedIncrement(&gQueued);
    SetEvent(gWake);
}

PendingRecord* TakeQueue() {
    EnterCriticalSection(&gQueueLock);
    PendingRecord* list = gQueueHead;
    gQueueHead = nullptr;
    gQueueTail = nullptr;
    LeaveCriticalSection(&gQueueLock);
    return list;
}

void DrainQueue() {
    PendingRecord* record = TakeQueue();
    while (record) {
        PendingRecord* next = record->next;
        const HANDLE output =
            record->animation ? gAnimationOutput : gOutput;
        if (output != INVALID_HANDLE_VALUE &&
            WriteAll(output, record->payload, record->bytes)) {
            InterlockedIncrement(&gWritten);
        } else {
            InterlockedIncrement(&gDropped);
        }
        HeapFree(GetProcessHeap(), 0, record);
        record = next;
    }
}

void CapturePose(
    void* self, std::uintptr_t modelInfo, const std::uintptr_t* drawArguments) {
    if (!modelInfo) {
        return;
    }

    __try {
        auto* info = reinterpret_cast<unsigned char*>(modelInfo);
        auto* studioHdr = *reinterpret_cast<unsigned char**>(info);
        if (!studioHdr) {
            return;
        }
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            return;
        }
        auto* object = static_cast<unsigned char*>(self);
        auto* boneToWorld = *reinterpret_cast<unsigned char**>(object + 0x5C);
        auto* skinPalette = *reinterpret_cast<unsigned char**>(object + 0x60);
        if (!boneToWorld || !skinPalette) {
            return;
        }

        const DWORD matrixBytes = static_cast<DWORD>(boneCount) * 12u * 4u;
        const DWORD diskBytes =
            static_cast<DWORD>(sizeof(PoseRecordHeader)) + matrixBytes * 2u;
        const SIZE_T allocation =
            offsetof(PendingRecord, payload) + static_cast<SIZE_T>(diskBytes);
        auto* pending = static_cast<PendingRecord*>(
            HeapAlloc(GetProcessHeap(), 0, allocation));
        if (!pending) {
            InterlockedIncrement(&gDropped);
            return;
        }

        pending->next = nullptr;
        pending->bytes = diskBytes;
        pending->animation = false;
        auto* header =
            reinterpret_cast<PoseRecordHeader*>(pending->payload);
        std::memset(header, 0, sizeof(*header));
        std::memcpy(header->magic, "POSE", 4);
        header->recordBytes = diskBytes;
        header->sequence =
            static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        header->qpc = now.QuadPart;
        header->threadId = GetCurrentThreadId();
        header->studioHdr =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(studioHdr));
        header->clientEntity =
            *reinterpret_cast<std::uint32_t*>(info + 0x18);
        header->checksum = *reinterpret_cast<std::uint32_t*>(studioHdr + 0x08);
        header->boneCount = static_cast<std::uint32_t>(boneCount);
        header->modelInfo = static_cast<std::uint32_t>(modelInfo);
        for (int index = 0; index < 5; ++index) {
            header->drawArguments[index] =
                static_cast<std::uint32_t>(drawArguments[index]);
        }
        std::memcpy(header->modelName, studioHdr + 0x0C, 64);
        std::memcpy(pending->payload + sizeof(*header), boneToWorld, matrixBytes);
        std::memcpy(
            pending->payload + sizeof(*header) + matrixBytes,
            skinPalette, matrixBytes);
        Enqueue(pending);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&gDropped);
    }
}

void CaptureAnimation(
    const char (&magic)[5], void* self, unsigned char* studioHdr,
    float* positions, float* quaternions, int studioSequence,
    float samplePhase, float entityCycle, int result,
    const void* selectedBones) {
    if (gAnimationOutput == INVALID_HANDLE_VALUE || !studioHdr ||
        !positions || !quaternions) {
        return;
    }

    __try {
        const DWORD checksum =
            *reinterpret_cast<DWORD*>(studioHdr + 0x08);
        if (gTargetChecksum && checksum != gTargetChecksum) {
            return;
        }
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            return;
        }

        const DWORD positionBytes =
            static_cast<DWORD>(boneCount) * 3u * sizeof(float);
        const DWORD quaternionBytes =
            static_cast<DWORD>(boneCount) * 4u * sizeof(float);
        const DWORD selectedBytes =
            static_cast<DWORD>((boneCount + 31) / 32) * sizeof(DWORD);
        const DWORD diskBytes =
            static_cast<DWORD>(sizeof(AnimationRecordHeader)) +
            positionBytes + quaternionBytes + selectedBytes;
        const SIZE_T allocation =
            offsetof(PendingRecord, payload) + static_cast<SIZE_T>(diskBytes);
        auto* pending = static_cast<PendingRecord*>(
            HeapAlloc(GetProcessHeap(), 0, allocation));
        if (!pending) {
            InterlockedIncrement(&gDropped);
            return;
        }

        pending->next = nullptr;
        pending->bytes = diskBytes;
        pending->animation = true;
        auto* header =
            reinterpret_cast<AnimationRecordHeader*>(pending->payload);
        std::memset(header, 0, sizeof(*header));
        std::memcpy(header->magic, magic, 4);
        header->recordBytes = diskBytes;
        header->sequence =
            static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        header->qpc = now.QuadPart;
        header->threadId = GetCurrentThreadId();
        header->clientEntity = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(self));
        header->studioHdr = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(studioHdr));
        header->checksum = checksum;
        header->boneCount = static_cast<std::uint32_t>(boneCount);
        header->studioSequence = studioSequence;
        header->samplePhase = samplePhase;
        header->entityCycle = entityCycle;
        header->result = result;
        header->positions = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(positions));
        header->quaternions = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(quaternions));
        std::memcpy(
            pending->payload + sizeof(*header), positions, positionBytes);
        std::memcpy(
            pending->payload + sizeof(*header) + positionBytes,
            quaternions, quaternionBytes);
        auto* selectedOutput =
            pending->payload + sizeof(*header) +
            positionBytes + quaternionBytes;
        if (selectedBones) {
            std::memcpy(
                selectedOutput,
                static_cast<const unsigned char*>(selectedBones) + 4,
                selectedBytes);
        } else {
            std::memset(selectedOutput, 0, selectedBytes);
        }
        Enqueue(pending);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&gDropped);
    }
}

std::uintptr_t __fastcall HookDrawModel(
    void* self, void*, std::uintptr_t argument0, std::uintptr_t argument1,
    std::uintptr_t argument2, std::uintptr_t argument3,
    std::uintptr_t argument4) {
    // Cover the entire detour lifetime. RemoveHook first disables capture and
    // restores the vtable slot, then waits for this count to reach zero before
    // the DLL unloads. Counting only after the original call returns would
    // leave its detour return address resident in a DLL that may be unloaded.
    InterlockedIncrement(&gActiveHooks);
    const std::uintptr_t result = gOriginalDrawModel(
        self, argument0, argument1, argument2, argument3, argument4);
    if (InterlockedCompareExchange(&gCapturing, 0, 0)) {
        const std::uintptr_t arguments[5] = {
            argument0, argument1, argument2, argument3, argument4};
        CapturePose(self, argument0, arguments);
    }
    InterlockedDecrement(&gActiveHooks);
    return result;
}

int __fastcall HookResolveVirtualModelPose(
    void* self, void*, float* positions, float* quaternions,
    int studioSequence, float samplePhase, void* poseParameters,
    void* selectedBones) {
    InterlockedIncrement(&gActiveHooks);
    const int result = gOriginalResolveVirtualModelPose(
        self, positions, quaternions, studioSequence, samplePhase,
        poseParameters, selectedBones);
    if (InterlockedCompareExchange(&gCapturing, 0, 0)) {
        unsigned char* studioHdr = nullptr;
        float entityCycle = 0.0f;
        __try {
            studioHdr = gGetStudioHdr(self, -1);
            entityCycle =
                *reinterpret_cast<float*>(
                    static_cast<unsigned char*>(self) + 0x648);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            studioHdr = nullptr;
            entityCycle = 0.0f;
        }
        CaptureAnimation(
            "BASE", self, studioHdr, positions, quaternions,
            studioSequence, samplePhase, entityCycle, result,
            selectedBones);
    }
    InterlockedDecrement(&gActiveHooks);
    return result;
}

void __fastcall HookBuildTransformations(
    void* self, void*, float* positions, float* quaternions,
    float* rootTransform, void* selectedBones) {
    InterlockedIncrement(&gActiveHooks);
    gOriginalBuildTransformations(
        self, positions, quaternions, rootTransform, selectedBones);
    if (InterlockedCompareExchange(&gCapturing, 0, 0)) {
        unsigned char* studioHdr = nullptr;
        __try {
            studioHdr = gGetStudioHdr(self, -1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            studioHdr = nullptr;
        }
        int studioSequence = -1;
        float cycle = 0.0f;
        __try {
            studioSequence =
                *reinterpret_cast<int*>(
                    static_cast<unsigned char*>(self) + 0x63C);
            cycle =
                *reinterpret_cast<float*>(
                    static_cast<unsigned char*>(self) + 0x648);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            studioSequence = -1;
            cycle = 0.0f;
        }
        CaptureAnimation(
            "FINL", self, studioHdr, positions, quaternions,
            studioSequence, -1.0f, cycle, 0, selectedBones);
    }
    InterlockedDecrement(&gActiveHooks);
}

bool PrepareInlineHook(
    InlineHook& hook, unsigned char* target,
    const unsigned char* expected) {
    if (std::memcmp(target, expected, kInlinePatchBytes) != 0) {
        return false;
    }
    hook.target = target;
    std::memcpy(hook.original, target, kInlinePatchBytes);
    hook.trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kInlinePatchBytes + 5, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!hook.trampoline) {
        return false;
    }
    std::memcpy(hook.trampoline, target, kInlinePatchBytes);
    hook.trampoline[kInlinePatchBytes] = 0xE9;
    *reinterpret_cast<std::int32_t*>(
        hook.trampoline + kInlinePatchBytes + 1) =
        static_cast<std::int32_t>(
            (target + kInlinePatchBytes) -
            (hook.trampoline + kInlinePatchBytes + 5));
    return true;
}

bool EnableInlineHook(InlineHook& hook, void* replacement) {
    unsigned char* target = hook.target;
    if (!target || !hook.trampoline) {
        return false;
    }
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            target, kInlinePatchBytes, PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        VirtualFree(hook.trampoline, 0, MEM_RELEASE);
        hook.trampoline = nullptr;
        hook.target = nullptr;
        return false;
    }
    target[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target + 1) =
        static_cast<std::int32_t>(
            static_cast<unsigned char*>(replacement) - (target + 5));
    DWORD ignored = 0;
    VirtualProtect(
        target, kInlinePatchBytes, previousProtection, &ignored);
    FlushInstructionCache(
        GetCurrentProcess(), target, kInlinePatchBytes);
    return true;
}

void RestoreInlineHook(InlineHook& hook) {
    if (!hook.target || !hook.trampoline) {
        return;
    }
    DWORD previousProtection = 0;
    if (VirtualProtect(
            hook.target, kInlinePatchBytes, PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        std::memcpy(
            hook.target, hook.original, kInlinePatchBytes);
        DWORD ignored = 0;
        VirtualProtect(
            hook.target, kInlinePatchBytes, previousProtection, &ignored);
        FlushInstructionCache(
            GetCurrentProcess(), hook.target, kInlinePatchBytes);
    }
}

bool InstallHooks(HMODULE studioRender, HMODULE client) {
    auto* base = reinterpret_cast<unsigned char*>(studioRender);
    auto* object = base + kStudioObjectRva;
    auto** vtable = *reinterpret_cast<void***>(object);
    if (vtable != reinterpret_cast<void**>(base + kStudioVtableRva)) {
        return false;
    }
    if (vtable[kDrawModelSlot] != base + kDrawModelRva) {
        return false;
    }

    gDrawModelSlot = &vtable[kDrawModelSlot];
    DWORD previousProtection = 0;
    if (!VirtualProtect(
            gDrawModelSlot, sizeof(void*), PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        return false;
    }
    gOriginalDrawModel = reinterpret_cast<DrawModelFn>(
        InterlockedExchangePointer(
            reinterpret_cast<void* volatile*>(gDrawModelSlot),
            reinterpret_cast<void*>(&HookDrawModel)));
    DWORD ignored = 0;
    VirtualProtect(
        gDrawModelSlot, sizeof(void*), previousProtection, &ignored);
    FlushInstructionCache(
        GetCurrentProcess(), gDrawModelSlot, sizeof(void*));
    if (gOriginalDrawModel != reinterpret_cast<DrawModelFn>(
            base + kDrawModelRva)) {
        return false;
    }

    if (gAnimationOutput == INVALID_HANDLE_VALUE) {
        return true;
    }

    auto* clientBase = reinterpret_cast<unsigned char*>(client);
    gGetStudioHdr = reinterpret_cast<GetStudioHdrFn>(
        clientBase + kGetStudioHdrRva);
    const unsigned char resolveExpected[kInlinePatchBytes] = {
        0x83, 0xEC, 0x34, 0x53, 0x55};
    const unsigned char buildExpected[kInlinePatchBytes] = {
        0xB8, 0x4C, 0x6F, 0x00, 0x00};
    if (!PrepareInlineHook(
            gResolveVirtualModelPoseHook,
            clientBase + kResolveVirtualModelPoseRva,
            resolveExpected)) {
        return false;
    }
    gOriginalResolveVirtualModelPose =
        reinterpret_cast<ResolveVirtualModelPoseFn>(
            gResolveVirtualModelPoseHook.trampoline);
    if (!EnableInlineHook(
            gResolveVirtualModelPoseHook,
            reinterpret_cast<void*>(&HookResolveVirtualModelPose))) {
        return false;
    }
    if (!PrepareInlineHook(
            gBuildTransformationsHook,
            clientBase + kBuildTransformationsRva,
            buildExpected)) {
        RestoreInlineHook(gResolveVirtualModelPoseHook);
        return false;
    }
    gOriginalBuildTransformations =
        reinterpret_cast<BuildTransformationsFn>(
            gBuildTransformationsHook.trampoline);
    if (!EnableInlineHook(
            gBuildTransformationsHook,
            reinterpret_cast<void*>(&HookBuildTransformations))) {
        RestoreInlineHook(gResolveVirtualModelPoseHook);
        return false;
    }
    return true;
}

void RemoveHooks() {
    InterlockedExchange(&gCapturing, 0);
    RestoreInlineHook(gBuildTransformationsHook);
    RestoreInlineHook(gResolveVirtualModelPoseHook);
    if (!gDrawModelSlot || !gOriginalDrawModel) {
        return;
    }
    DWORD previousProtection = 0;
    if (VirtualProtect(
            gDrawModelSlot, sizeof(void*), PAGE_EXECUTE_READWRITE,
            &previousProtection)) {
        InterlockedExchangePointer(
            reinterpret_cast<void* volatile*>(gDrawModelSlot),
            reinterpret_cast<void*>(gOriginalDrawModel));
        DWORD ignored = 0;
        VirtualProtect(
            gDrawModelSlot, sizeof(void*), previousProtection, &ignored);
        FlushInstructionCache(
            GetCurrentProcess(), gDrawModelSlot, sizeof(void*));
    }
    while (InterlockedCompareExchange(&gActiveHooks, 0, 0)) {
        Sleep(1);
    }
    if (gBuildTransformationsHook.trampoline) {
        VirtualFree(
            gBuildTransformationsHook.trampoline, 0, MEM_RELEASE);
        gBuildTransformationsHook.trampoline = nullptr;
    }
    if (gResolveVirtualModelPoseHook.trampoline) {
        VirtualFree(
            gResolveVirtualModelPoseHook.trampoline, 0, MEM_RELEASE);
        gResolveVirtualModelPoseHook.trampoline = nullptr;
    }
}

bool ReadConfiguration(
    wchar_t* iniPath, wchar_t* outputPath, wchar_t* animationOutputPath,
    wchar_t* studioHash, wchar_t* clientHash) {
    wchar_t modulePath[MAX_PATH * 4]{};
    const DWORD length =
        GetModuleFileNameW(gSelf, modulePath, ARRAYSIZE(modulePath));
    if (!length || length >= ARRAYSIZE(modulePath)) {
        return false;
    }
    wcsncpy_s(iniPath, MAX_PATH * 4, modulePath, _TRUNCATE);
    wchar_t* extension = std::wcsrchr(iniPath, L'.');
    if (!extension) {
        return false;
    }
    const std::size_t remaining =
        MAX_PATH * 4 - static_cast<std::size_t>(extension - iniPath);
    wcscpy_s(extension, remaining, L".ini");

    GetPrivateProfileStringW(
        L"capture", L"output", L"", outputPath, MAX_PATH * 4, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"animation_output", L"", animationOutputPath,
        MAX_PATH * 4, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"ready", L"", gReadyPath, ARRAYSIZE(gReadyPath), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"stop", L"", gStopPath, ARRAYSIZE(gStopPath), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"done", L"", gDonePath, ARRAYSIZE(gDonePath), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"studiorender_sha256", L"", studioHash, 80, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"client_sha256", L"", clientHash, 80, iniPath);
    wchar_t targetChecksum[32]{};
    GetPrivateProfileStringW(
        L"capture", L"target_checksum", L"0", targetChecksum,
        ARRAYSIZE(targetChecksum), iniPath);
    gTargetChecksum = std::wcstoul(targetChecksum, nullptr, 0);
    gDurationSeconds = GetPrivateProfileIntW(
        L"capture", L"duration_seconds", 300, iniPath);
    return outputPath[0] && gReadyPath[0] && gStopPath[0] && gDonePath[0];
}

DWORD WINAPI CaptureWorker(void*) {
    wchar_t iniPath[MAX_PATH * 4]{};
    wchar_t outputPath[MAX_PATH * 4]{};
    wchar_t animationOutputPath[MAX_PATH * 4]{};
    wchar_t studioHash[80]{};
    wchar_t clientHash[80]{};
    if (!ReadConfiguration(
            iniPath, outputPath, animationOutputPath,
            studioHash, clientHash)) {
        return 1;
    }

    HMODULE studioRender = nullptr;
    for (int attempt = 0; attempt < 200 && !studioRender; ++attempt) {
        studioRender = GetModuleHandleW(L"StudioRender.dll");
        if (!studioRender) {
            Sleep(25);
        }
    }
    if (!studioRender) {
        WriteMarker(gDonePath, "error=StudioRender.dll not loaded\n");
        return 2;
    }
    HMODULE client = GetModuleHandleW(L"client.dll");
    if (animationOutputPath[0] && !client) {
        WriteMarker(gDonePath, "error=client.dll not loaded\n");
        return 2;
    }

    InitializeCriticalSection(&gQueueLock);
    gWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gOutput = CreateFileW(
        outputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (animationOutputPath[0]) {
        gAnimationOutput = CreateFileW(
            animationOutputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    if (!gWake || gOutput == INVALID_HANDLE_VALUE ||
        (animationOutputPath[0] &&
         gAnimationOutput == INVALID_HANDLE_VALUE)) {
        WriteMarker(gDonePath, "error=cannot create capture output\n");
        return 3;
    }

    FileHeader fileHeader{};
    std::memcpy(fileHeader.magic, "ELPOSE2", 7);
    fileHeader.version = 2;
    fileHeader.headerBytes = sizeof(fileHeader);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&gStartQpc);
    fileHeader.qpcFrequency = frequency.QuadPart;
    fileHeader.startQpc = gStartQpc.QuadPart;
    fileHeader.pid = GetCurrentProcessId();
    fileHeader.studioRenderBase =
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(studioRender));
    fileHeader.studioObject = fileHeader.studioRenderBase + kStudioObjectRva;
    fileHeader.studioVtable = fileHeader.studioRenderBase + kStudioVtableRva;
    fileHeader.drawModelRva = kDrawModelRva;
    std::size_t converted = 0;
    wcstombs_s(
        &converted, fileHeader.studioRenderSha256,
        sizeof(fileHeader.studioRenderSha256), studioHash, _TRUNCATE);
    if (!WriteAll(gOutput, &fileHeader, sizeof(fileHeader))) {
        WriteMarker(gDonePath, "error=cannot write capture header\n");
        return 4;
    }

    if (gAnimationOutput != INVALID_HANDLE_VALUE) {
        AnimationFileHeader animationHeader{};
        std::memcpy(animationHeader.magic, "ELANIM2", 7);
        animationHeader.version = 2;
        animationHeader.headerBytes = sizeof(animationHeader);
        animationHeader.qpcFrequency = frequency.QuadPart;
        animationHeader.startQpc = gStartQpc.QuadPart;
        animationHeader.pid = GetCurrentProcessId();
        animationHeader.clientBase = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(client));
        animationHeader.resolveVirtualModelPoseRva =
            kResolveVirtualModelPoseRva;
        animationHeader.buildTransformationsRva =
            kBuildTransformationsRva;
        animationHeader.targetChecksum = gTargetChecksum;
        converted = 0;
        wcstombs_s(
            &converted, animationHeader.clientSha256,
            sizeof(animationHeader.clientSha256), clientHash, _TRUNCATE);
        if (!WriteAll(
                gAnimationOutput, &animationHeader,
                sizeof(animationHeader))) {
            WriteMarker(
                gDonePath,
                "error=cannot write animation capture header\n");
            return 4;
        }
    }

    if (!InstallHooks(studioRender, client)) {
        RemoveHooks();
        WriteMarker(gDonePath, "error=capture hook validation failed\n");
        return 5;
    }
    InterlockedExchange(&gCapturing, 1);
    WriteMarker(
        gReadyPath,
        gAnimationOutput == INVALID_HANDLE_VALUE
            ? "ready=1\nformat=ELPOSE2\n"
            : "ready=1\nformat=ELPOSE2+ELANIM2\n");

    for (;;) {
        WaitForSingleObject(gWake, 50);
        DrainQueue();
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const bool expired =
            gDurationSeconds &&
            static_cast<std::uint64_t>(now.QuadPart - gStartQpc.QuadPart) >=
                static_cast<std::uint64_t>(frequency.QuadPart) *
                    gDurationSeconds;
        if (Exists(gStopPath) || expired) {
            break;
        }
    }

    RemoveHooks();
    DrainQueue();
    FlushFileBuffers(gOutput);
    CloseHandle(gOutput);
    gOutput = INVALID_HANDLE_VALUE;
    if (gAnimationOutput != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(gAnimationOutput);
        CloseHandle(gAnimationOutput);
        gAnimationOutput = INVALID_HANDLE_VALUE;
    }

    char done[256]{};
    std::snprintf(
        done, sizeof(done),
        "complete=1\nqueued=%ld\nwritten=%ld\ndropped=%ld\n", gQueued,
        gWritten, gDropped);
    WriteMarker(gDonePath, done);
    CloseHandle(gWake);
    DeleteCriticalSection(&gQueueLock);
    FreeLibraryAndExitThread(gSelf, 0);
}

}  // namespace

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        gSelf = module;
        DisableThreadLibraryCalls(module);
        HANDLE worker =
            CreateThread(nullptr, 0, &CaptureWorker, nullptr, 0, nullptr);
        if (worker) {
            CloseHandle(worker);
        }
    }
    return TRUE;
}
