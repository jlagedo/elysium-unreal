#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <intrin.h>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "native/generated_binary_profiles.h"
#include "native/hook_backend.h"
#include "native/viewmodel_capture_format.h"

namespace {

using elysium::capture::ActiveBinaryProfile;
using elysium::capture::BinaryProfile;
using elysium::capture::BinaryTargetProfile;
using elysium::capture::HookBackendResult;
using elysium::capture::HookBackends;
using elysium::capture::HookHandle;
using elysium::capture::viewmodel::Boundary;
using elysium::capture::viewmodel::Fault;
using elysium::capture::viewmodel::FileHeader;
using elysium::capture::viewmodel::Phase;
using elysium::capture::viewmodel::Record;

constexpr std::uint32_t kInvalidHandle =
    elysium::capture::viewmodel::InvalidHandle;
constexpr DWORD kServerRefHandle = 0x448;
constexpr DWORD kServerOwnerHandle = 0x88c;
constexpr DWORD kServerViewmodelIndex = 0x890;
constexpr DWORD kServerSequence = 0x6f0;
constexpr DWORD kWeaponAmmo = 0x74c;
constexpr DWORD kClientRefHandle = 0xe4;
constexpr DWORD kClientWeaponHandle = 0x7d8;
constexpr DWORD kClientViewmodelIndex = 0x7d4;
constexpr DWORD kClientSequence = 0x63c;
constexpr DWORD kClientPlaybackRate = 0x640;
constexpr DWORD kClientCycle = 0x648;
constexpr DWORD kStudioName = 0x0c;
constexpr DWORD kStudioBoneCount = 0xf0;
constexpr DWORD kStudioBoneIndex = 0xf4;
constexpr DWORD kStudioBoneBytes = 160;
constexpr DWORD kEngineAnamorphicObjectRva = 0x00a6ca8c;
constexpr DWORD kMaximumBones = 1024;

using ViewmodelUpdateFn = void(__thiscall*)(void*, std::uint32_t, std::uint32_t);
using RangedAnimationEventFn = void(__thiscall*)(void*, int*, void*);
using ServerNoArgsFn = void(__thiscall*)(void*);
using DrawViewmodelsFn = void(__thiscall*)(void*, void*, std::uint32_t);
using SetupBonesFn = bool(__thiscall*)(
    void*, float*, std::uint32_t, std::uint32_t, std::uint32_t, void*);
using GetStudioHdrFn = unsigned char*(__thiscall*)(void*, int);
using ClientAnimationEventFn = void(__thiscall*)(
    void*, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t);
using ProjectionFn = void(__thiscall*)(
    void*, float, float, float, std::uint32_t);

struct ThreadState {
    std::uint32_t Frame;
    float PlayerFov;
    float ViewmodelFov;
    float NearPlane;
    float FarPlane;
    std::int32_t ViewportWidth;
    std::int32_t ViewportHeight;
    std::uint32_t DrawPolicy;
    char HandsModel[elysium::capture::viewmodel::ModelNameBytes];
    char WeaponModel[elysium::capture::viewmodel::ModelNameBytes];
};

HMODULE gSelf = nullptr;
HMODULE gVampire = nullptr;
HMODULE gClient = nullptr;
HMODULE gEngine = nullptr;
HANDLE gOutput = INVALID_HANDLE_VALUE;
CRITICAL_SECTION gWriteLock;
volatile LONG gCapturing = 0;
volatile LONG gActiveHooks = 0;
volatile LONG gFrame = 0;
volatile LONG64 gOrdinal = 0;
volatile LONG gWritten = 0;
volatile LONG gFaults = 0;
DWORD gDurationSeconds = 300;
LARGE_INTEGER gStartQpc{};
wchar_t gReadyPath[MAX_PATH * 4]{};
wchar_t gStopPath[MAX_PATH * 4]{};
wchar_t gDonePath[MAX_PATH * 4]{};
char gScenario[elysium::capture::viewmodel::ScenarioBytes]{};
char gVampireHash[65]{};
char gClientHash[65]{};
char gEngineHash[65]{};
__declspec(thread) ThreadState gThread{};

ViewmodelUpdateFn gOriginalViewmodelUpdate = nullptr;
RangedAnimationEventFn gOriginalRangedAnimationEvent = nullptr;
ServerNoArgsFn gOriginalShot = nullptr;
ServerNoArgsFn gOriginalDryFire = nullptr;
ServerNoArgsFn gOriginalReloadRequest = nullptr;
ServerNoArgsFn gOriginalReloadFrame = nullptr;
ServerNoArgsFn gOriginalReloadFinish = nullptr;
DrawViewmodelsFn gOriginalDrawViewmodels = nullptr;
SetupBonesFn gOriginalSetupBones = nullptr;
GetStudioHdrFn gGetStudioHdr = nullptr;
ClientAnimationEventFn gOriginalClientAnimationEvent = nullptr;
ProjectionFn gOriginalProjection = nullptr;

HookHandle gViewmodelUpdateHook;
HookHandle gRangedAnimationEventHook;
HookHandle gShotHook;
HookHandle gDryFireHook;
HookHandle gReloadRequestHook;
HookHandle gReloadFrameHook;
HookHandle gReloadFinishHook;
HookHandle gDrawViewmodelsHook;
HookHandle gSetupBonesHook;
HookHandle gClientAnimationEventHook;
HookHandle gProjectionHook;

std::uint32_t gTargetRvas[elysium::capture::viewmodel::TargetCount]{};

bool Exists(const wchar_t* path) {
    return path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

void WriteMarker(const wchar_t* path, const char* text) {
    if (!path[0]) {
        return;
    }
    HANDLE file = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(
        file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    CloseHandle(file);
}

template <typename T>
bool SafeRead(const unsigned char* base, DWORD offset, T* value) {
    if (base == nullptr || value == nullptr) {
        return false;
    }
    __try {
        *value = *reinterpret_cast<const T*>(base + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopy(const void* source, void* target, std::size_t bytes) {
    if (source == nullptr || target == nullptr) {
        return false;
    }
    __try {
        std::memcpy(target, source, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeString(
    const char* source, char* target, std::size_t targetBytes) {
    if (source == nullptr || target == nullptr || targetBytes == 0) {
        return false;
    }
    __try {
        std::size_t index = 0;
        for (; index + 1 < targetBytes && source[index] != '\0'; ++index) {
            const unsigned char value =
                static_cast<unsigned char>(source[index]);
            if (value < 0x20 || value > 0x7e) {
                return false;
            }
            target[index] = source[index];
        }
        target[index] = '\0';
        return index != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        target[0] = '\0';
        return false;
    }
}

void InitializeRecord(
    Record* record, Boundary boundary, Phase phase, HMODULE module,
    std::uint32_t rva) {
    std::memset(record, 0, sizeof(*record));
    std::memcpy(record->Magic, "VMR1", 4);
    record->RecordBytes = sizeof(*record);
    record->Ordinal = static_cast<std::uint64_t>(
        InterlockedIncrement64(&gOrdinal));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    record->Qpc = now.QuadPart;
    record->ThreadId = GetCurrentThreadId();
    record->Frame = gThread.Frame;
    record->BoundaryId = static_cast<std::uint32_t>(boundary);
    record->CallPhase = static_cast<std::uint32_t>(phase);
    record->ModuleBase = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(module));
    record->FunctionRva = rva;
    record->CallerAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    record->ServerHandle = kInvalidHandle;
    record->ServerOwnerHandle = kInvalidHandle;
    record->ClientHandle = kInvalidHandle;
    record->ClientWeaponHandle = kInvalidHandle;
    record->ViewmodelIndex = -1;
    record->ServerSequence = -1;
    record->ClientSequence = -1;
    record->EventId = -1;
    record->AmmoBefore = -1;
    record->AmmoAfter = -1;
    record->RootBoneIndex = kInvalidHandle;
    record->AlignmentBoneIndex = kInvalidHandle;
    record->PlayerFov = gThread.PlayerFov;
    record->ViewmodelFov = gThread.ViewmodelFov;
    record->NearPlane = gThread.NearPlane;
    record->FarPlane = gThread.FarPlane;
    record->ViewportWidth = gThread.ViewportWidth;
    record->ViewportHeight = gThread.ViewportHeight;
    record->DrawPolicy = gThread.DrawPolicy;
    strncpy_s(record->HandsModel, gThread.HandsModel, _TRUNCATE);
    strncpy_s(record->WeaponModel, gThread.WeaponModel, _TRUNCATE);
    strncpy_s(record->Scenario, gScenario, _TRUNCATE);
}

void WriteRecord(const Record& record) {
    if (gOutput == INVALID_HANDLE_VALUE ||
        InterlockedCompareExchange(&gCapturing, 0, 0) == 0) {
        return;
    }
    EnterCriticalSection(&gWriteLock);
    DWORD written = 0;
    const BOOL ok = WriteFile(
        gOutput, &record, sizeof(record), &written, nullptr);
    LeaveCriticalSection(&gWriteLock);
    if (!ok || written != sizeof(record)) {
        InterlockedIncrement(&gFaults);
        return;
    }
    InterlockedIncrement(&gWritten);
}

void ReadServerState(
    Record* record, unsigned char* entity, std::int32_t ammoBefore,
    std::int32_t ammoAfter) {
    record->ServerEntity = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(entity));
    record->AmmoBefore = ammoBefore;
    record->AmmoAfter = ammoAfter;
    if (entity == nullptr) {
        record->Faults |= Fault::FaultServerEntity;
        return;
    }
    SafeRead(entity, kServerRefHandle, &record->ServerHandle);
    SafeRead(entity, kServerOwnerHandle, &record->ServerOwnerHandle);
    SafeRead(entity, kServerViewmodelIndex, &record->ViewmodelIndex);
    SafeRead(entity, kServerSequence, &record->ServerSequence);
}

std::int32_t ReadAmmo(unsigned char* entity) {
    std::int32_t ammo = -1;
    SafeRead(entity, kWeaponAmmo, &ammo);
    return ammo;
}

void CaptureServer(
    Boundary boundary, Phase phase, void* self, std::int32_t eventId,
    std::int32_t ammoBefore, std::int32_t ammoAfter, std::uint32_t rva) {
    Record record{};
    InitializeRecord(&record, boundary, phase, gVampire, rva);
    record.EventId = eventId;
    ReadServerState(
        &record, static_cast<unsigned char*>(self), ammoBefore, ammoAfter);
    WriteRecord(record);
}

void __fastcall HookViewmodelUpdate(
    void* self, void*, std::uint32_t first, std::uint32_t second) {
    InterlockedIncrement(&gActiveHooks);
    CaptureServer(
        Boundary::ServerViewmodelUpdate, Phase::Entry, self, -1,
        ReadAmmo(static_cast<unsigned char*>(self)), -1, gTargetRvas[0]);
    gOriginalViewmodelUpdate(self, first, second);
    CaptureServer(
        Boundary::ServerViewmodelUpdate, Phase::Exit, self, -1, -1,
        ReadAmmo(static_cast<unsigned char*>(self)), gTargetRvas[0]);
    InterlockedDecrement(&gActiveHooks);
}

void __fastcall HookRangedAnimationEvent(
    void* self, void*, int* event, void* owner) {
    InterlockedIncrement(&gActiveHooks);
    std::int32_t eventId = -1;
    if (event != nullptr) {
        SafeRead(
            reinterpret_cast<const unsigned char*>(event), 0, &eventId);
    }
    CaptureServer(
        Boundary::ServerAnimationEvent, Phase::Entry, self, eventId,
        ReadAmmo(static_cast<unsigned char*>(self)), -1, gTargetRvas[1]);
    gOriginalRangedAnimationEvent(self, event, owner);
    CaptureServer(
        Boundary::ServerAnimationEvent, Phase::Exit, self, eventId, -1,
        ReadAmmo(static_cast<unsigned char*>(self)), gTargetRvas[1]);
    InterlockedDecrement(&gActiveHooks);
}

void CaptureNoArgs(
    Boundary boundary, ServerNoArgsFn original, void* self,
    std::uint32_t rva) {
    InterlockedIncrement(&gActiveHooks);
    const std::int32_t before = ReadAmmo(static_cast<unsigned char*>(self));
    CaptureServer(
        boundary, Phase::Entry, self, -1, before, -1, rva);
    original(self);
    CaptureServer(
        boundary, Phase::Exit, self, -1, before,
        ReadAmmo(static_cast<unsigned char*>(self)), rva);
    InterlockedDecrement(&gActiveHooks);
}

void __fastcall HookShot(void* self, void*) {
    CaptureNoArgs(Boundary::ServerShot, gOriginalShot, self, gTargetRvas[2]);
}

void __fastcall HookDryFire(void* self, void*) {
    CaptureNoArgs(
        Boundary::ServerDryFire, gOriginalDryFire, self, gTargetRvas[3]);
}

void __fastcall HookReloadRequest(void* self, void*) {
    CaptureNoArgs(
        Boundary::ServerReloadRequest, gOriginalReloadRequest, self,
        gTargetRvas[4]);
}

void __fastcall HookReloadFrame(void* self, void*) {
    CaptureNoArgs(
        Boundary::ServerReloadFrame, gOriginalReloadFrame, self,
        gTargetRvas[5]);
}

void __fastcall HookReloadFinish(void* self, void*) {
    CaptureNoArgs(
        Boundary::ServerReloadFinish, gOriginalReloadFinish, self,
        gTargetRvas[6]);
}

void ReadViewSetup(Record* record, void* setup, std::uint32_t drawPolicy) {
    record->DrawPolicy = drawPolicy & 0xffu;
    auto* bytes = static_cast<unsigned char*>(setup);
    if (bytes == nullptr) {
        record->Faults |= Fault::FaultClientEntity;
        return;
    }
    SafeRead(bytes, 0x0c, &record->ViewportWidth);
    SafeRead(bytes, 0x10, &record->ViewportHeight);
    SafeRead(bytes, 0x28, &record->PlayerFov);
    SafeRead(bytes, 0x2c, &record->ViewmodelFov);
    SafeRead(bytes, 0x64, &record->NearPlane);
    SafeRead(bytes, 0x68, &record->FarPlane);
}

void __fastcall HookDrawViewmodels(
    void* self, void*, void* setup, std::uint32_t drawPolicy) {
    InterlockedIncrement(&gActiveHooks);
    gThread.Frame = static_cast<std::uint32_t>(InterlockedIncrement(&gFrame));
    std::memset(gThread.HandsModel, 0, sizeof(gThread.HandsModel));
    std::memset(gThread.WeaponModel, 0, sizeof(gThread.WeaponModel));
    Record entry{};
    InitializeRecord(
        &entry, Boundary::ClientDrawPass, Phase::Entry, gClient,
        gTargetRvas[7]);
    ReadViewSetup(&entry, setup, drawPolicy);
    gThread.PlayerFov = entry.PlayerFov;
    gThread.ViewmodelFov = entry.ViewmodelFov;
    gThread.NearPlane = entry.NearPlane;
    gThread.FarPlane = entry.FarPlane;
    gThread.ViewportWidth = entry.ViewportWidth;
    gThread.ViewportHeight = entry.ViewportHeight;
    gThread.DrawPolicy = entry.DrawPolicy;
    WriteRecord(entry);
    gOriginalDrawViewmodels(self, setup, drawPolicy);
    Record exit{};
    InitializeRecord(
        &exit, Boundary::ClientDrawPass, Phase::Exit, gClient,
        gTargetRvas[7]);
    ReadViewSetup(&exit, setup, drawPolicy);
    WriteRecord(exit);
    gThread.Frame = 0;
    InterlockedDecrement(&gActiveHooks);
}

bool IsViewmodelName(const char* name, bool* hands) {
    if (name == nullptr || hands == nullptr) {
        return false;
    }
    if (std::strncmp(name, "models/hands/", 13) == 0) {
        *hands = true;
        return true;
    }
    if (std::strncmp(name, "models/weapons/", 15) == 0 &&
        std::strstr(name, "/view/") != nullptr) {
        *hands = false;
        return true;
    }
    // studiohdr_t::name is commonly only the MDL basename in this build.
    // World/wield/item models use w_/i_ prefixes; both first-person roles use
    // v_, with the hands role identified by its authored filename.
    if (std::strncmp(name, "v_", 2) == 0) {
        *hands = std::strstr(name, "hands") != nullptr;
        return true;
    }
    return false;
}

std::uint32_t FindBone(
    unsigned char* studio, const char* wanted, std::uint32_t* count,
    std::uint32_t* faults) {
    std::int32_t boneCount = 0;
    std::int32_t boneIndex = 0;
    if (!SafeRead(studio, kStudioBoneCount, &boneCount) ||
        !SafeRead(studio, kStudioBoneIndex, &boneIndex) || boneCount < 0 ||
        boneCount > static_cast<std::int32_t>(kMaximumBones) ||
        boneIndex <= 0) {
        *faults |= Fault::FaultBoneTable;
        return kInvalidHandle;
    }
    *count = static_cast<std::uint32_t>(boneCount);
    for (std::int32_t index = 0; index < boneCount; ++index) {
        auto* bone = studio + boneIndex + index * kStudioBoneBytes;
        std::int32_t nameOffset = 0;
        if (!SafeRead(bone, 0, &nameOffset) || nameOffset <= 0) {
            continue;
        }
        char name[64]{};
        if (SafeString(
                reinterpret_cast<const char*>(bone + nameOffset), name,
                sizeof(name)) &&
            std::strcmp(name, wanted) == 0) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return kInvalidHandle;
}

bool __fastcall HookSetupBones(
    void* self, void*, float* matrices, std::uint32_t maximumBones,
    std::uint32_t mask, std::uint32_t timeBits, void* context) {
    InterlockedIncrement(&gActiveHooks);
    const bool result = gOriginalSetupBones(
        self, matrices, maximumBones, mask, timeBits, context);
    auto* entity = static_cast<unsigned char*>(self) - 4;
    unsigned char* studio = nullptr;
    __try {
        studio = gGetStudioHdr != nullptr ? gGetStudioHdr(entity, -1) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        studio = nullptr;
    }
    char modelName[elysium::capture::viewmodel::ModelNameBytes]{};
    bool hands = false;
    if (studio != nullptr &&
        SafeString(
            reinterpret_cast<const char*>(studio + kStudioName), modelName,
            sizeof(modelName)) &&
        IsViewmodelName(modelName, &hands)) {
        if (hands) {
            strncpy_s(gThread.HandsModel, modelName, _TRUNCATE);
        } else {
            strncpy_s(gThread.WeaponModel, modelName, _TRUNCATE);
        }
        Record record{};
        InitializeRecord(
            &record, Boundary::ClientSetupBones, Phase::Exit, gClient,
            gTargetRvas[8]);
        record.ClientEntity = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(entity));
        SafeRead(entity, kClientRefHandle, &record.ClientHandle);
        SafeRead(entity, kClientWeaponHandle, &record.ClientWeaponHandle);
        SafeRead(entity, kClientViewmodelIndex, &record.ViewmodelIndex);
        SafeRead(entity, kClientSequence, &record.ClientSequence);
        SafeRead(entity, kClientCycle, &record.Cycle);
        SafeRead(entity, kClientPlaybackRate, &record.PlaybackRate);
        if (!result || matrices == nullptr) {
            record.Faults |= Fault::FaultBoneOutput;
        } else {
            record.RootBoneIndex = FindBone(
                studio, "Camera01", &record.BoneCount, &record.Faults);
            record.AlignmentBoneIndex = FindBone(
                studio, "Bip01 R Hand", &record.BoneCount,
                &record.Faults);
            if (record.RootBoneIndex != kInvalidHandle &&
                record.RootBoneIndex < maximumBones &&
                !SafeCopy(
                    matrices + record.RootBoneIndex * 12,
                    record.RootTransform, sizeof(record.RootTransform))) {
                record.Faults |= Fault::FaultBoneOutput;
            }
            if (record.AlignmentBoneIndex != kInvalidHandle &&
                record.AlignmentBoneIndex < maximumBones &&
                !SafeCopy(
                    matrices + record.AlignmentBoneIndex * 12,
                    record.AlignmentTransform,
                    sizeof(record.AlignmentTransform))) {
                record.Faults |= Fault::FaultBoneOutput;
            }
        }
        WriteRecord(record);
    }
    InterlockedDecrement(&gActiveHooks);
    return result;
}

void __fastcall HookClientAnimationEvent(
    void* self, void*, std::uint32_t first, std::uint32_t second,
    std::int32_t eventId, std::uint32_t options) {
    InterlockedIncrement(&gActiveHooks);
    Record entry{};
    InitializeRecord(
        &entry, Boundary::ClientAnimationEvent, Phase::Entry, gClient,
        gTargetRvas[9]);
    entry.EventId = eventId;
    auto* entity = static_cast<unsigned char*>(self);
    entry.ClientEntity = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(entity));
    SafeRead(entity, kClientRefHandle, &entry.ClientHandle);
    SafeRead(entity, kClientWeaponHandle, &entry.ClientWeaponHandle);
    SafeRead(entity, kClientSequence, &entry.ClientSequence);
    SafeRead(entity, kClientCycle, &entry.Cycle);
    WriteRecord(entry);
    gOriginalClientAnimationEvent(self, first, second, eventId, options);
    Record exit = entry;
    InitializeRecord(
        &exit, Boundary::ClientAnimationEvent, Phase::Exit, gClient,
        gTargetRvas[9]);
    exit.EventId = eventId;
    exit.ClientEntity = entry.ClientEntity;
    exit.ClientHandle = entry.ClientHandle;
    exit.ClientWeaponHandle = entry.ClientWeaponHandle;
    SafeRead(entity, kClientSequence, &exit.ClientSequence);
    SafeRead(entity, kClientCycle, &exit.Cycle);
    WriteRecord(exit);
    InterlockedDecrement(&gActiveHooks);
}

float ReadAspect(std::uint32_t flags, std::uint32_t* faults) {
    if ((flags & 0xffu) != 0) {
        return 1.0f;
    }
    if (gEngine == nullptr) {
        *faults |= Fault::FaultProjectionPolicy;
        return 0.0f;
    }
    auto* base = reinterpret_cast<unsigned char*>(gEngine);
    void* object = nullptr;
    if (!SafeRead(base, kEngineAnamorphicObjectRva, &object) ||
        object == nullptr) {
        *faults |= Fault::FaultProjectionPolicy;
        return 0.0f;
    }
    bool state = true;
    std::int32_t value = 0;
    __try {
        auto** table = *reinterpret_cast<void***>(object);
        auto function = reinterpret_cast<bool(__thiscall*)(void*)>(table[1]);
        state = function(object);
        value = *reinterpret_cast<std::int32_t*>(
            static_cast<unsigned char*>(object) + 0x2c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faults |= Fault::FaultProjectionPolicy;
        return 0.0f;
    }
    return !state && value != 0 ? (16.0f / 9.0f) : (4.0f / 3.0f);
}

void __fastcall HookProjection(
    void* self, void*, float fov, float nearPlane, float farPlane,
    std::uint32_t flags) {
    InterlockedIncrement(&gActiveHooks);
    gOriginalProjection(self, fov, nearPlane, farPlane, flags);
    Record record{};
    InitializeRecord(
        &record, Boundary::EngineProjection, Phase::Exit, gEngine,
        gTargetRvas[10]);
    record.ViewmodelFov = fov;
    record.NearPlane = nearPlane;
    record.FarPlane = farPlane;
    record.ProjectionFlags = flags;
    record.Aspect = ReadAspect(flags, &record.Faults);
    constexpr float kPi = 3.14159265358979323846f;
    record.TanHalfFov = std::tan(fov * kPi / 360.0f);
    WriteRecord(record);
    InterlockedDecrement(&gActiveHooks);
}

const BinaryProfile* FindProfile(const wchar_t* moduleName) {
    for (const auto& profile : elysium::capture::profiles::Registry) {
        if (_wcsicmp(profile.ModuleName, moduleName) == 0) {
            return &profile;
        }
    }
    return nullptr;
}

const BinaryTargetProfile* FindTarget(
    const BinaryProfile& profile, const char* label) {
    for (std::uint32_t index = 0; index < profile.TargetCount; ++index) {
        if (std::strcmp(profile.Targets[index].SemanticLabel, label) == 0) {
            return &profile.Targets[index];
        }
    }
    return nullptr;
}

bool InstallOne(
    const ActiveBinaryProfile& active, const BinaryTargetProfile* target,
    void* replacement, HookHandle* hook, void** original,
    std::uint32_t targetIndex) {
    if (target == nullptr || targetIndex >= elysium::capture::viewmodel::TargetCount) {
        return false;
    }
    const HookBackendResult result =
        HookBackends::Install(active, *target, replacement, hook);
    if (result != HookBackendResult::Installed) {
        return false;
    }
    *original = hook->Original;
    gTargetRvas[targetIndex] = target->Rva;
    return true;
}

bool InstallHooks() {
    const auto* vampireProfile = FindProfile(L"vampire.dll");
    const auto* clientProfile = FindProfile(L"client.dll");
    const auto* engineProfile = FindProfile(L"engine.dll");
    if (vampireProfile == nullptr || clientProfile == nullptr ||
        engineProfile == nullptr) {
        return false;
    }
    const ActiveBinaryProfile vampireActive{
        vampireProfile, reinterpret_cast<std::uintptr_t>(gVampire),
        vampireProfile->Pe.SizeOfImage};
    const ActiveBinaryProfile clientActive{
        clientProfile, reinterpret_cast<std::uintptr_t>(gClient),
        clientProfile->Pe.SizeOfImage};
    const ActiveBinaryProfile engineActive{
        engineProfile, reinterpret_cast<std::uintptr_t>(gEngine),
        engineProfile->Pe.SizeOfImage};

#define INSTALL(active, profile, label, hookfn, handle, original, index)       \
    if (!InstallOne(                                                         \
            active, FindTarget(*profile, label),                             \
            reinterpret_cast<void*>(hookfn), &handle,                        \
            reinterpret_cast<void**>(&original), index)) {                   \
        return false;                                                        \
    }

    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_update",
        &HookViewmodelUpdate, gViewmodelUpdateHook, gOriginalViewmodelUpdate,
        0);
    INSTALL(
        vampireActive, vampireProfile,
        "vampire.viewmodel_ranged_anim_event", &HookRangedAnimationEvent,
        gRangedAnimationEventHook, gOriginalRangedAnimationEvent, 1);
    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_ranged_shot",
        &HookShot, gShotHook, gOriginalShot, 2);
    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_dry_fire",
        &HookDryFire, gDryFireHook, gOriginalDryFire, 3);
    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_reload_request",
        &HookReloadRequest, gReloadRequestHook, gOriginalReloadRequest, 4);
    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_reload_frame",
        &HookReloadFrame, gReloadFrameHook, gOriginalReloadFrame, 5);
    INSTALL(
        vampireActive, vampireProfile, "vampire.viewmodel_reload_finish",
        &HookReloadFinish, gReloadFinishHook, gOriginalReloadFinish, 6);
    INSTALL(
        clientActive, clientProfile, "client.viewmodel_draw_pass",
        &HookDrawViewmodels, gDrawViewmodelsHook, gOriginalDrawViewmodels, 7);
    INSTALL(
        clientActive, clientProfile, "client.setup_bones", &HookSetupBones,
        gSetupBonesHook, gOriginalSetupBones, 8);
    INSTALL(
        clientActive, clientProfile, "client.viewmodel_fire_event",
        &HookClientAnimationEvent, gClientAnimationEventHook,
        gOriginalClientAnimationEvent, 9);
    INSTALL(
        engineActive, engineProfile, "engine.viewmodel_projection",
        &HookProjection, gProjectionHook, gOriginalProjection, 10);
#undef INSTALL

    const auto* getStudio = FindTarget(*clientProfile, "client.get_studio_hdr");
    if (getStudio == nullptr) {
        return false;
    }
    gGetStudioHdr = reinterpret_cast<GetStudioHdrFn>(
        reinterpret_cast<unsigned char*>(gClient) + getStudio->Rva);
    return true;
}

void RemoveHooks() {
    InterlockedExchange(&gCapturing, 0);
    HookBackends::Disable(&gProjectionHook);
    HookBackends::Disable(&gClientAnimationEventHook);
    HookBackends::Disable(&gSetupBonesHook);
    HookBackends::Disable(&gDrawViewmodelsHook);
    HookBackends::Disable(&gReloadFinishHook);
    HookBackends::Disable(&gReloadFrameHook);
    HookBackends::Disable(&gReloadRequestHook);
    HookBackends::Disable(&gDryFireHook);
    HookBackends::Disable(&gShotHook);
    HookBackends::Disable(&gRangedAnimationEventHook);
    HookBackends::Disable(&gViewmodelUpdateHook);
    while (InterlockedCompareExchange(&gActiveHooks, 0, 0) != 0) {
        Sleep(1);
    }
    HookBackends::Release(&gProjectionHook);
    HookBackends::Release(&gClientAnimationEventHook);
    HookBackends::Release(&gSetupBonesHook);
    HookBackends::Release(&gDrawViewmodelsHook);
    HookBackends::Release(&gReloadFinishHook);
    HookBackends::Release(&gReloadFrameHook);
    HookBackends::Release(&gReloadRequestHook);
    HookBackends::Release(&gDryFireHook);
    HookBackends::Release(&gShotHook);
    HookBackends::Release(&gRangedAnimationEventHook);
    HookBackends::Release(&gViewmodelUpdateHook);
}

bool ReadConfiguration(wchar_t* outputPath) {
    wchar_t modulePath[MAX_PATH * 4]{};
    const DWORD length =
        GetModuleFileNameW(gSelf, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return false;
    }
    wchar_t iniPath[MAX_PATH * 4]{};
    wcsncpy_s(iniPath, modulePath, _TRUNCATE);
    wchar_t* extension = std::wcsrchr(iniPath, L'.');
    if (extension == nullptr) {
        return false;
    }
    wcscpy_s(
        extension,
        ARRAYSIZE(iniPath) - static_cast<std::size_t>(extension - iniPath),
        L".ini");
    GetPrivateProfileStringW(
        L"capture", L"output", L"", outputPath, MAX_PATH * 4, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"ready", L"", gReadyPath, ARRAYSIZE(gReadyPath),
        iniPath);
    GetPrivateProfileStringW(
        L"capture", L"stop", L"", gStopPath, ARRAYSIZE(gStopPath), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"done", L"", gDonePath, ARRAYSIZE(gDonePath), iniPath);
    wchar_t scenario[elysium::capture::viewmodel::ScenarioBytes]{};
    wchar_t vampireHash[80]{};
    wchar_t clientHash[80]{};
    wchar_t engineHash[80]{};
    GetPrivateProfileStringW(
        L"capture", L"scenario", L"", scenario, ARRAYSIZE(scenario),
        iniPath);
    GetPrivateProfileStringW(
        L"capture", L"vampire_sha256", L"", vampireHash,
        ARRAYSIZE(vampireHash), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"client_sha256", L"", clientHash,
        ARRAYSIZE(clientHash), iniPath);
    GetPrivateProfileStringW(
        L"capture", L"engine_sha256", L"", engineHash,
        ARRAYSIZE(engineHash), iniPath);
    std::size_t converted = 0;
    wcstombs_s(
        &converted, gScenario, sizeof(gScenario), scenario, _TRUNCATE);
    wcstombs_s(
        &converted, gVampireHash, sizeof(gVampireHash), vampireHash,
        _TRUNCATE);
    wcstombs_s(
        &converted, gClientHash, sizeof(gClientHash), clientHash, _TRUNCATE);
    wcstombs_s(
        &converted, gEngineHash, sizeof(gEngineHash), engineHash, _TRUNCATE);
    gDurationSeconds = GetPrivateProfileIntW(
        L"capture", L"duration_seconds", 300, iniPath);
    return outputPath[0] && gReadyPath[0] && gStopPath[0] && gDonePath[0] &&
        gScenario[0] && gVampireHash[0] && gClientHash[0] && gEngineHash[0];
}

bool WriteHeader() {
    FileHeader header{};
    std::memcpy(header.Magic, "ELGVM1", 6);
    header.FormatVersion = elysium::capture::viewmodel::Version;
    header.HeaderBytes = sizeof(header);
    header.RecordBytes = sizeof(Record);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&gStartQpc);
    header.QpcFrequency = static_cast<std::uint64_t>(frequency.QuadPart);
    header.StartQpc = gStartQpc.QuadPart;
    header.ProcessId = GetCurrentProcessId();
    header.VampireBase = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(gVampire));
    header.ClientBase = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(gClient));
    header.EngineBase = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(gEngine));
    std::memcpy(header.TargetRvas, gTargetRvas, sizeof(header.TargetRvas));
    strncpy_s(header.VampireSha256, gVampireHash, _TRUNCATE);
    strncpy_s(header.ClientSha256, gClientHash, _TRUNCATE);
    strncpy_s(header.EngineSha256, gEngineHash, _TRUNCATE);
    strncpy_s(header.Scenario, gScenario, _TRUNCATE);
    DWORD written = 0;
    return WriteFile(
               gOutput, &header, sizeof(header), &written, nullptr) &&
        written == sizeof(header);
}

DWORD WINAPI Worker(void*) {
    wchar_t outputPath[MAX_PATH * 4]{};
    if (!ReadConfiguration(outputPath)) {
        return 1;
    }
    for (int attempt = 0;
         attempt < 1200 && (!gVampire || !gClient || !gEngine); ++attempt) {
        gVampire = GetModuleHandleW(L"vampire.dll");
        gClient = GetModuleHandleW(L"client.dll");
        gEngine = GetModuleHandleW(L"engine.dll");
        if (!gVampire || !gClient || !gEngine) {
            Sleep(25);
        }
    }
    if (!gVampire || !gClient || !gEngine) {
        WriteMarker(gDonePath, "error=required module not loaded\n");
        return 2;
    }
    InitializeCriticalSection(&gWriteLock);
    gOutput = CreateFileW(
        outputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (gOutput == INVALID_HANDLE_VALUE) {
        WriteMarker(gDonePath, "error=cannot create ELGVM1 output\n");
        DeleteCriticalSection(&gWriteLock);
        return 3;
    }
    if (!InstallHooks() || !WriteHeader()) {
        RemoveHooks();
        CloseHandle(gOutput);
        gOutput = INVALID_HANDLE_VALUE;
        WriteMarker(gDonePath, "error=hook install or header write failed\n");
        DeleteCriticalSection(&gWriteLock);
        return 4;
    }
    InterlockedExchange(&gCapturing, 1);
    WriteMarker(gReadyPath, "ready=1\nformat=ELGVM1\n");
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    for (;;) {
        Sleep(25);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const bool expired = gDurationSeconds != 0 &&
            static_cast<std::uint64_t>(now.QuadPart - gStartQpc.QuadPart) >=
                static_cast<std::uint64_t>(frequency.QuadPart) *
                    gDurationSeconds;
        if (Exists(gStopPath) || expired) {
            break;
        }
    }
    RemoveHooks();
    FlushFileBuffers(gOutput);
    CloseHandle(gOutput);
    gOutput = INVALID_HANDLE_VALUE;
    char done[256]{};
    std::snprintf(
        done, sizeof(done),
        "complete=1\nformat=ELGVM1\nrecords=%ld\nfaults=%ld\n",
        gWritten, gFaults);
    WriteMarker(gDonePath, done);
    DeleteCriticalSection(&gWriteLock);
    FreeLibraryAndExitThread(gSelf, 0);
}

}  // namespace

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        gSelf = module;
        DisableThreadLibraryCalls(module);
        HANDLE worker = CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
    }
    return TRUE;
}
