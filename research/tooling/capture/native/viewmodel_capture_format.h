#pragma once

#include <cstdint>

namespace elysium::capture::viewmodel {

constexpr std::uint32_t Version = 1;
constexpr std::uint32_t InvalidHandle = 0xffffffffu;
constexpr std::uint32_t TransformFloats = 12;
constexpr std::uint32_t ModelNameBytes = 96;
constexpr std::uint32_t ScenarioBytes = 32;
constexpr std::uint32_t TargetCount = 11;

enum class Boundary : std::uint32_t {
    ServerViewmodelUpdate = 1,
    ServerAnimationEvent = 2,
    ServerShot = 3,
    ServerDryFire = 4,
    ServerReloadRequest = 5,
    ServerReloadFrame = 6,
    ServerReloadFinish = 7,
    ClientDrawPass = 20,
    ClientSetupBones = 21,
    ClientAnimationEvent = 22,
    EngineProjection = 30,
};

enum class Phase : std::uint32_t {
    Snapshot = 0,
    Entry = 1,
    Exit = 2,
};

enum Fault : std::uint32_t {
    FaultNone = 0,
    FaultServerEntity = 1u << 0,
    FaultClientEntity = 1u << 1,
    FaultStudioHeader = 1u << 2,
    FaultModelName = 1u << 3,
    FaultBoneTable = 1u << 4,
    FaultBoneOutput = 1u << 5,
    FaultProjectionPolicy = 1u << 6,
};

#pragma pack(push, 1)

struct FileHeader {
    char Magic[8];
    std::uint32_t FormatVersion;
    std::uint32_t HeaderBytes;
    std::uint32_t RecordBytes;
    std::uint64_t QpcFrequency;
    std::int64_t StartQpc;
    std::uint32_t ProcessId;
    std::uint32_t VampireBase;
    std::uint32_t ClientBase;
    std::uint32_t EngineBase;
    std::uint32_t TargetRvas[TargetCount];
    char VampireSha256[65];
    char ClientSha256[65];
    char EngineSha256[65];
    char Scenario[ScenarioBytes];
    char Reserved[5];
};

struct Record {
    char Magic[4];
    std::uint32_t RecordBytes;
    std::uint64_t Ordinal;
    std::int64_t Qpc;
    std::uint32_t ThreadId;
    std::uint32_t Frame;
    std::uint32_t BoundaryId;
    std::uint32_t CallPhase;
    std::uint32_t ModuleBase;
    std::uint32_t FunctionRva;
    std::uint32_t CallerAddress;
    std::uint32_t ServerEntity;
    std::uint32_t ServerHandle;
    std::uint32_t ServerOwnerHandle;
    std::uint32_t ClientEntity;
    std::uint32_t ClientHandle;
    std::uint32_t ClientWeaponHandle;
    std::int32_t ViewmodelIndex;
    std::int32_t ServerSequence;
    std::int32_t ClientSequence;
    std::int32_t EventId;
    std::int32_t AmmoBefore;
    std::int32_t AmmoAfter;
    float Cycle;
    float PlaybackRate;
    float PlayerFov;
    float ViewmodelFov;
    float NearPlane;
    float FarPlane;
    float Aspect;
    float TanHalfFov;
    std::int32_t ViewportWidth;
    std::int32_t ViewportHeight;
    std::uint32_t DrawPolicy;
    std::uint32_t ProjectionFlags;
    std::uint32_t BoneCount;
    std::uint32_t RootBoneIndex;
    std::uint32_t AlignmentBoneIndex;
    std::uint32_t Faults;
    float RootTransform[TransformFloats];
    float AlignmentTransform[TransformFloats];
    char HandsModel[ModelNameBytes];
    char WeaponModel[ModelNameBytes];
    char Scenario[ScenarioBytes];
};

#pragma pack(pop)

static_assert(sizeof(FileHeader) == 328, "ELGVM1 file header changed");
static_assert(sizeof(Record) == 484, "ELGVM1 record changed");

}  // namespace elysium::capture::viewmodel
