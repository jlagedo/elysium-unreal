#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "native/generated_binary_profiles.h"
#include "native/hook_backend.h"

namespace {

constexpr int kMaximumBones = 1024;
// A declaration must cover every byte the detour overwrites, so a signature
// is as long as the target's relocated prologue rather than a fixed width.
constexpr DWORD kMaximumSignatureBytes = 32;

struct ConfiguredSignature {
    unsigned char bytes[kMaximumSignatureBytes];
    DWORD count;
};
// One draw frame enclosing one pose build is two levels. The rest is headroom
// for nesting the run has not shown; a bracket beyond it is refused and
// counted rather than silently overwriting a live entry.
constexpr std::uint32_t kMaximumBracketDepth = 8;

// The census keeps identity per sighting and bytes once per checksum, so the
// two live at different table sizes: a model loaded at several addresses costs
// several observations and one image.
constexpr std::uint32_t kCensusSlots = 1024;
constexpr std::uint32_t kCensusImageSlots = 512;
constexpr std::uint32_t kCensusProbeLimit = 64;
// The largest model in the install is under 7 MB. The cap bounds one copy off
// a validated header rather than trusting Length; a model above it is recorded
// truncated and flagged instead of being skipped.
constexpr DWORD kCensusImageCap = 8u * 1024u * 1024u;
constexpr std::uint32_t kStudioMagic = 0x54534449u;  // "IDST"
constexpr std::uint32_t kStudioVersion = 2531u;

// One slot per skeletal actor address. A theatre run animates 49 of them, so
// the table is sized for two orders of growth rather than for the corpus.
constexpr std::uint32_t kActorSlots = 512;
constexpr std::uint32_t kActorProbeLimit = 64;
// The root/entity transform BuildTransformations receives: a 3x4 row-major
// matrix, appended to the composed-pose record rather than to every evaluation.
constexpr DWORD kRootTransformFloats = 12;
constexpr DWORD kRootTransformBytes = kRootTransformFloats * sizeof(float);
// The span of the render info the engine builds and the studio draw consumes.
// The producer writes eight dwords at +0x00 through +0x1c and the consumer
// reads no further, so this is the whole struct as both sides use it.
constexpr DWORD kRenderInfoBytes = 32;

// A sequence blends over two axes, and the include-model group remaps exactly
// 0x18 pose parameter slots, so both bounds come from the code rather than from
// a guess about how many a model declares.
constexpr std::uint32_t kBlendAxes = 2;
constexpr std::uint32_t kMaximumPoseParameters = 24;
constexpr DWORD kPoseParameterBytes =
    kMaximumPoseParameters * sizeof(float);
// Displacements inside the studio header and the sequence descriptor that the
// contribution hooks read. Named here because the record stores identity rather
// than the descriptor bytes: the model census already holds the whole owner
// image, and the runtime header is that image at offset 0, so a descriptor
// pointer minus the header base is the offset CAP2.5 needs.
constexpr DWORD kStudioLocalSeqCount = 0x110;
constexpr DWORD kStudioLocalSeqIndex = 0x114;
constexpr DWORD kStudioLocalAnimIndex = 0x10c;
constexpr DWORD kSequenceDescriptorBytes = 764;
constexpr DWORD kAnimationDescriptorBytes = 72;
constexpr DWORD kSequenceNumBlends = 0x34;
constexpr DWORD kSequenceGroupSize = 0x23c;
constexpr DWORD kSequenceParamIndex = 0x244;
// Strides the selected-bone loop advances by. The probe never computes an
// address from either: it latches the first pointer each decoder was handed and
// divides by the stride, so a pointer that is not on the stride is a fault
// rather than a silently wrong bone index.
constexpr DWORD kAnimationRecordBytes = 32;
constexpr DWORD kBoneRecordBytes = 160;
// Why a contribution record could not name something it should have. A fault is
// per record rather than a global counter, because CAP2.4 asks which
// contribution failed to resolve, not how many did.
enum ContributionFault : std::uint32_t {
    kFaultOwnerHeader = 1u << 0,
    kFaultSequenceDescriptor = 1u << 1,
    kFaultAnimationDescriptor = 1u << 2,
    kFaultPoseParameters = 1u << 3,
    kFaultSelectedBones = 1u << 4,
    kFaultBlendUnwitnessed = 1u << 5,
    kFaultSequenceOutOfRange = 1u << 6,
    kFaultNoContributionScope = 1u << 7,
    kFaultChannelsUnwitnessed = 1u << 8,
    kFaultChannelStride = 1u << 9,
    kFaultChannelNested = 1u << 10,
    kFaultChannelOverflow = 1u << 11,
};

// Which stream a queued record belongs to. The writer routes on this and frees
// to the heap it names, so census payloads never touch the game's heap.
enum StreamIndex : std::uint32_t {
    kStreamPose = 0,
    kStreamAnimation = 1,
    kStreamCensus = 2,
    kStreamActor = 3,
    kStreamContribution = 4,
};

// Why an observation was emitted. Reuse is a pointer that served one checksum
// and now serves another; residency is what the sweep at capture stop reports
// in place of the unload event no hooked target can currently see.
enum ObservationReason : std::uint32_t {
    kObservationFirst = 1,
    kObservationReplacement = 2,
    kObservationResidentAtStop = 3,
};

// Why an actor observation was emitted. The three reasons here are what the
// hooks already armed can witness: a skeletal entity this run has not posed at
// this address, an address whose model identity changed under it, and the sweep
// at capture stop. Construction and destruction are separate reasons carried by
// a later stream version, once a target for them is pinned.
enum ActorReason : std::uint32_t {
    kActorFirst = 1,
    kActorIdentityChange = 2,
    kActorResidentAtStop = 3,
    // A lifetime record names an address and nothing else. The constructor has
    // not given the object a model yet and the destructor is taking it away, so
    // reading identity at either point would read a half-built or dying object.
    kActorConstruct = 4,
    kActorDestruct = 5,
};

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
    // The enclosing draw bracket. The pose build has already closed by the
    // time a draw fires, so the two carry fields name the pose build this
    // draw should belong to; the offline verifier decides whether they agree
    // with the enclosing generation instead of the hook assuming they do.
    std::uint32_t generation;
    std::uint32_t generationEntity;
    std::uint32_t carryGeneration;
    // The render info the draw is submitted with, copied verbatim. Two fields
    // in it are already decoded — the studio header at +0x00 and the entity at
    // +0x18 — and the rest are kept as bytes rather than named, because what
    // they mean is a hypothesis and a wrong name would outlive the run.
    std::uint32_t renderInfoBytes;
    unsigned char renderInfo[kRenderInfoBytes];
};

struct BracketRecordHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::int64_t entryQpc;
    std::uint32_t threadId;
    std::uint32_t generation;
    std::uint32_t parentGeneration;
    std::uint32_t depth;
    // The bracket's `this`. For a pose build that is the renderable; for the
    // engine draw frame it is the CModelRender singleton, so it names the
    // frame rather than an actor.
    std::uint32_t clientEntity;
    // Nine slots because the engine draw frame stages nine arguments; a
    // narrower record would drop four of them without saying so.
    std::uint32_t arguments[9];
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
    std::uint32_t setupBonesRva;
    std::uint32_t modelRenderDrawModelRva;
    char reserved[3];
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
    std::uint32_t generation;
    std::uint32_t generationDepth;
    // The root/entity transform, which only the composed-pose stage receives.
    // Its byte count is stored rather than implied so record size stays a
    // closed form of kind and bone count for a stream carrying both kinds.
    std::uint32_t rootTransform;
    std::uint32_t rootTransformBytes;
};

struct CensusFileHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint64_t qpcFrequency;
    std::int64_t startQpc;
    std::uint32_t pid;
    std::uint32_t clientBase;
    std::uint32_t studioRenderBase;
    std::uint32_t imageCap;
    std::uint32_t slotCount;
    char reserved[76];
};

// One sighting of a studio header at one address. The name is the full 128
// bytes the header carries, not the 64 a draw record keeps, so a model path
// longer than a draw record's field is recoverable here.
struct ModelObservationHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t studioHdr;
    std::uint32_t checksum;
    std::uint32_t previousChecksum;
    std::uint32_t boneCount;
    std::uint32_t boneIndex;
    std::uint32_t modelLength;
    std::uint32_t includeModelCount;
    std::uint32_t includeModelIndex;
    std::uint32_t studioVersion;
    std::uint32_t reason;
    std::uint32_t imageCaptured;
    std::uint32_t generation;
    char modelName[128];
};

// The model image the header sits at the front of, stored once per checksum.
// Every index field in the header is an offset from the header base, so
// keeping the image is what makes those offsets resolvable offline and what
// lets a captured runtime pointer become a model-image offset.
struct ModelImageHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t studioHdr;
    std::uint32_t checksum;
    std::uint32_t modelLength;
    std::uint32_t capturedBytes;
    std::uint32_t capped;
    std::uint32_t reserved;
};

struct ActorFileHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint64_t qpcFrequency;
    std::int64_t startQpc;
    std::uint32_t pid;
    std::uint32_t clientBase;
    std::uint32_t slotCount;
    std::uint32_t setupBonesRva;
    char reserved[80];
};

// One sighting of a skeletal client entity at one address. Both terms of
// CAP1.3's relation are stored rather than one and a delta: `entity` is the
// C_BaseAnimating the evaluators run on, `renderable` the subobject four bytes
// above it that the draw stream records, so a reader joins either stream
// without re-deriving the offset the run is supposed to be evidence for.
struct ActorObservationHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t entity;
    std::uint32_t renderable;
    std::uint32_t reason;
    std::uint32_t generation;
    std::uint32_t studioHdr;
    std::uint32_t checksum;
    std::uint32_t previousChecksum;
    std::uint32_t boneCount;
    char modelName[64];
};

struct ContributionFileHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint64_t qpcFrequency;
    std::int64_t startQpc;
    std::uint32_t pid;
    std::uint32_t clientBase;
    std::uint32_t evaluateSequencePoseRva;
    std::uint32_t decodeSelectedBonesRva;
    std::uint32_t resolveBlendAxisWeightRva;
    std::uint32_t decodeBoneQuaternionRva;
    std::uint32_t decodeBonePositionRva;
    char clientSha256[65];
    char reserved[3];
};

// One fired contribution. `SEQP` is a sequence evaluation and `ANIM` one of the
// blend cells it decoded, so a repeated call is a repeated record and shared
// payload never collapses two of them.
//
// Bytes are deliberately absent. The sequence and animation descriptors are
// stored as pointers because the model census already holds the whole owner
// image and the runtime studio header is that image at offset zero, so a
// pointer minus `ownerStudioHdr` is the file offset without a second copy. The
// live pose parameters are the one thing no image carries, so they are the one
// span that travels.
//
// A cell additionally carries what the two channel decoders were handed: the
// first animation record and bone they read, the frame and fraction they were
// given, and one bit per bone each of them ran for. Those are witnessed at the
// hook boundary and decoded by nobody here, so an offline walker that predicts
// the same pointers from the image is checked rather than trusted.
struct ContributionRecordHeader {
    char magic[4];
    std::uint32_t recordBytes;
    std::uint64_t sequence;
    std::int64_t qpc;
    std::uint32_t threadId;
    std::uint32_t generation;
    std::uint32_t generationDepth;
    std::uint32_t generationEntity;
    // Opened by the sequence frame before it runs, so the cells it decodes carry
    // it even though their records reach the queue first.
    std::uint32_t contribution;
    // The immediate call site. Resolved offline against the case specification
    // rather than named here, because which builder asked for a contribution is
    // an analyzer conclusion.
    std::uint32_t callerAddress;
    std::uint32_t ownerStudioHdr;
    std::uint32_t ownerChecksum;
    std::uint32_t ownerBoneCount;
    std::int32_t sequenceIndex;
    std::int32_t animationIndex;
    std::uint32_t sequenceDescriptor;
    std::uint32_t animationDescriptor;
    std::uint32_t boneMask;
    float cycle;
    std::int32_t numBlends;
    std::int32_t groupSize[kBlendAxes];
    std::int32_t paramIndex[kBlendAxes];
    // Witnessed from the blend resolver rather than recomputed from the pose
    // parameters, which is the difference between evidence and our own decoder.
    std::int32_t blendCell[kBlendAxes];
    float blendWeight[kBlendAxes];
    std::uint32_t faults;
    std::uint32_t poseParameterBytes;
    std::uint32_t selectedBoneBytes;
    // The first animation record and StudioBone each channel decoder was handed
    // for this cell. Latched from the first call rather than computed from
    // animindex, so nothing in this record depends on the format being what we
    // think it is.
    std::uint32_t channelRecordBase;
    std::uint32_t channelBoneBase;
    std::uint32_t channelQuaternionCalls;
    std::uint32_t channelPositionCalls;
    // The frame the cell frame selected and the fraction it interpolated with,
    // which is what makes floor((numframes - 1) * cycle) refutable.
    std::int32_t channelFrame;
    float channelFraction;
    // Both witnessed-bone bitmaps together; each is ((ownerBoneCount + 31) / 32)
    // dwords, quaternion first.
    std::uint32_t channelBoneBytes;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 128, "capture file header changed");
static_assert(sizeof(PoseRecordHeader) == 180, "pose record header changed");
static_assert(
    sizeof(BracketRecordHeader) == 88,
    "pose-build bracket record header changed");
static_assert(
    sizeof(AnimationFileHeader) == 128,
    "animation capture file header changed");
static_assert(
    sizeof(AnimationRecordHeader) == 84,
    "animation record header changed");
static_assert(
    sizeof(CensusFileHeader) == 128, "census capture file header changed");
static_assert(
    sizeof(ModelObservationHeader) == 204,
    "model observation record header changed");
static_assert(
    sizeof(ModelImageHeader) == 52, "model image record header changed");
static_assert(
    sizeof(ActorFileHeader) == 128, "actor capture file header changed");
static_assert(
    sizeof(ActorObservationHeader) == 124,
    "actor observation record header changed");
static_assert(
    sizeof(ContributionFileHeader) == 128,
    "contribution capture file header changed");
static_assert(
    sizeof(ContributionRecordHeader) == 160,
    "contribution record header changed");

struct PendingRecord {
    PendingRecord* next;
    DWORD bytes;
    std::uint32_t stream;
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
// Pinned from the callee: no stack arguments, returning `this` in EAX. This is
// the shared base of the client entity hierarchy, not a skeletal one.
using BaseEntityConstructFn = void*(__thiscall*)(void*);
// Pinned from the callee: no stack arguments, no return value, one exit that
// tail-calls its base teardown.
using BaseEntityDestructFn = void(__thiscall*)(void*);
// Pinned from the callee: every SetupBones exit is RET 0x14 and returns bool
// in AL.
using SetupBonesFn = bool(__thiscall*)(
    void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t);
// Nine callee-cleaned dword arguments, returning int in EAX (RET 0x24 at
// both exits of 0x200a6640).
using ModelRenderDrawModelFn = int(__thiscall*)(
    void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t);
// Four callee-cleaned dword arguments and no return value (RET 0x10 at both
// exits of 0x200a6990, EAX untouched).
using ModelRenderDrawModelShadowFn = void(__thiscall*)(
    void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
// The three contribution frames share one seven-dword __cdecl contract with
// resolve_virtual_model_pose, which forwards its own arguments to them
// unchanged: (studiohdr, positions, quaternions, sequence, cycle,
// poseParameters, boneMask). Every exit is a bare RET and each caller cleans
// 0x1c, so the studio header is argument zero rather than something to infer.
using EvaluateSequencePoseFn = void(__cdecl*)(
    unsigned char*, float*, float*, int, float, float*, void*);
// The per-cell decoder takes the owning header and the animation descriptor of
// the cell that fired, cleaned with ADD ESP,0x18 at every call site.
using DecodeSelectedBonesFn = void(__cdecl*)(
    unsigned char*, float*, float*, unsigned char*, float, void*);
// One blend axis: (studiohdr, poseParameters, sequenceDescriptor, axis,
// outWeight, outCell), cleaned 0x18.
using ResolveBlendAxisWeightFn = void(__cdecl*)(
    unsigned char*, float*, unsigned char*, int, float*, int*);
// The two channel decoders share one six-dword __cdecl contract
// (studiohdr, frame, fraction, StudioBone, animation record, output), cleaned
// 0x18 by the one call site each has. Neither reads the header it is given; the
// two pointers in the middle are what a consumed span is measured from.
using DecodeBoneChannelFn = void(__cdecl*)(
    unsigned char*, int, float, unsigned char*, unsigned char*, float*);

// The bracket a record was produced inside. Nothing here is shared between
// threads, so a push and a pop cost no interlocked operation and no
// allocation on the render path.
struct ThreadPoseState {
    std::uint32_t generations[kMaximumBracketDepth];
    std::uint32_t entities[kMaximumBracketDepth];
    std::int64_t entryQpc[kMaximumBracketDepth];
    std::uint32_t depth;
    std::uint32_t lastPoseGeneration;
    std::uint32_t lastPoseEntity;
    // The sequence frame recurses through the include dispatcher for autolayers,
    // so contributions nest the same way pose builds do and get the same stack
    // rather than a single slot.
    std::uint32_t contributions[kMaximumBracketDepth];
    std::uint32_t contributionDepth;
    // The blend resolver writes its result here and the sequence frame that
    // called it reads it back, so a witnessed weight rides on the contribution
    // record instead of costing a record of its own. Keyed by the descriptor
    // because 0x1008c060 calls the same resolver for its own purposes, and a
    // stale or foreign entry is refused rather than trusted.
    std::uint32_t blendDescriptor[kBlendAxes];
    std::int32_t blendCell[kBlendAxes];
    float blendWeight[kBlendAxes];
    // The two channel decoders emit no record of their own either. They fold one
    // bone each into the cell frame's accumulator and that frame reads it back,
    // so a witnessed per-bone decode costs no record volume at all.
    //
    // Keyed by an epoch rather than by a pointer, because neither decoder
    // receives anything that names the animation descriptor. The cell frame
    // stamps an epoch before it runs and refuses an accumulator carrying any
    // other one, which is what makes a nested or orphaned frame a reported fault
    // instead of another cell's bones.
    std::uint32_t channelEpoch;
    std::uint32_t channelBase;
    std::uint32_t channelBoneBase;
    std::uint32_t channelQuaternionCalls;
    std::uint32_t channelPositionCalls;
    std::int32_t channelFrame;
    float channelFraction;
    std::uint32_t channelFaults;
    std::uint32_t channelQuaternionBits[kMaximumBones / 32];
    std::uint32_t channelPositionBits[kMaximumBones / 32];
};

// An open-addressed slot claimed by its own key rather than by a separate
// state word: the interlocked exchange that writes `key` is the claim, so a
// slot never carries a key a racing reader cannot trust. `published` says the
// record for that key has reached the queue, which is the only point at which
// a second thread may stop looking.
struct CensusSlot {
    volatile LONG key;
    volatile LONG value;
    volatile LONG published;
    LONG reserved;
};

// The same claim-by-key discipline for actors, plus the studio header the actor
// last named. The sweep at capture stop runs on the writer thread, so it
// re-reads that address directly instead of calling a game function to resolve
// the header from an entity that may no longer be there.
struct ActorSlot {
    volatile LONG key;
    volatile LONG value;
    volatile LONG published;
    volatile LONG studioHdr;
};

HMODULE gSelf = nullptr;
DrawModelFn gOriginalDrawModel = nullptr;
ResolveVirtualModelPoseFn gOriginalResolveVirtualModelPose = nullptr;
BuildTransformationsFn gOriginalBuildTransformations = nullptr;
GetStudioHdrFn gGetStudioHdr = nullptr;
SetupBonesFn gOriginalSetupBones = nullptr;
ModelRenderDrawModelFn gOriginalModelRenderDrawModel = nullptr;
ModelRenderDrawModelShadowFn gOriginalModelRenderDrawModelShadow = nullptr;
BaseEntityConstructFn gOriginalBaseEntityConstruct = nullptr;
BaseEntityDestructFn gOriginalBaseEntityDestruct = nullptr;
EvaluateSequencePoseFn gOriginalEvaluateSequencePose = nullptr;
DecodeSelectedBonesFn gOriginalDecodeSelectedBones = nullptr;
ResolveBlendAxisWeightFn gOriginalResolveBlendAxisWeight = nullptr;
DecodeBoneChannelFn gOriginalDecodeBoneQuaternion = nullptr;
DecodeBoneChannelFn gOriginalDecodeBonePosition = nullptr;
elysium::capture::HookHandle gDrawModelHook;
elysium::capture::HookHandle gResolveVirtualModelPoseHook;
elysium::capture::HookHandle gBuildTransformationsHook;
elysium::capture::HookHandle gSetupBonesHook;
elysium::capture::HookHandle gModelRenderDrawModelHook;
elysium::capture::HookHandle gModelRenderDrawModelShadowHook;
elysium::capture::HookHandle gBaseEntityConstructHook;
elysium::capture::HookHandle gBaseEntityDestructHook;
elysium::capture::HookHandle gEvaluateSequencePoseHook;
elysium::capture::HookHandle gDecodeSelectedBonesHook;
elysium::capture::HookHandle gResolveBlendAxisWeightHook;
elysium::capture::HookHandle gDecodeBoneQuaternionHook;
elysium::capture::HookHandle gDecodeBonePositionHook;
__declspec(thread) ThreadPoseState gThread{};
HANDLE gOutput = INVALID_HANDLE_VALUE;
HANDLE gAnimationOutput = INVALID_HANDLE_VALUE;
HANDLE gCensusOutput = INVALID_HANDLE_VALUE;
HANDLE gActorOutput = INVALID_HANDLE_VALUE;
HANDLE gContributionOutput = INVALID_HANDLE_VALUE;
// Census payloads are the largest allocation the probe ever makes. Taking the
// game's process heap lock for a multi-megabyte block from a render callback
// is the one hitch this design could introduce, so they come from a heap of
// our own and DrainQueue frees to the heap the record's stream names.
HANDLE gCensusHeap = nullptr;
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
volatile LONG gQueueDepth = 0;
volatile LONG gQueuePeak = 0;
volatile LONG gSkipped = 0;
volatile LONG gFiltered = 0;
// Generation 0 is the unassigned sentinel, so the counter starts at 1.
volatile LONG gGeneration = 0;
volatile LONG gUnbracketed = 0;
volatile LONG gBracketOverflow = 0;
volatile LONG gCensusRecords = 0;
volatile LONG gCensusImages = 0;
volatile LONG gCensusReplacements = 0;
volatile LONG gCensusResident = 0;
volatile LONG gCensusVanished = 0;
volatile LONG gCensusOverflow = 0;
volatile LONG gCensusFaults = 0;
volatile LONG gCensusCapped = 0;
volatile LONG64 gCensusBytes = 0;
volatile LONG gActorRecords = 0;
volatile LONG gActorIdentityChanges = 0;
volatile LONG gActorResident = 0;
volatile LONG gActorVanished = 0;
volatile LONG gActorOverflow = 0;
volatile LONG gActorFaults = 0;
volatile LONG gActorConstructions = 0;
volatile LONG gActorDestructions = 0;
volatile LONG64 gActorBytes = 0;
volatile LONG gContributionSequences = 0;
volatile LONG gContributionAnimations = 0;
volatile LONG gContributionFaults = 0;
volatile LONG gContributionOverflow = 0;
volatile LONG gContributionUnscoped = 0;
volatile LONG64 gContributionBytes = 0;
// Per-call totals are absent on purpose. A decoder runs tens of millions of
// times per cutscene and a global counter would put a locked read-modify-write
// on every one of them; each cell already carries its own two counts, so the
// totals are a sum offline instead of a cost on the render thread.
volatile LONG gChannelUnwitnessed = 0;
volatile LONG gChannelStrideFaults = 0;
volatile LONG gChannelNested = 0;
// Contribution scope 0 is the unassigned sentinel, so the counter starts at 1.
volatile LONG gContributionScope = 0;
// Headers keyed by address, images keyed by checksum.
CensusSlot gCensusHeaders[kCensusSlots]{};
CensusSlot gCensusImageSlots[kCensusImageSlots]{};
// Actors keyed by the C_BaseAnimating address.
ActorSlot gActors[kActorSlots]{};
volatile LONG64 gBytesWritten = 0;
LARGE_INTEGER gStartQpc{};
wchar_t gReadyPath[MAX_PATH * 4]{};
wchar_t gStopPath[MAX_PATH * 4]{};
wchar_t gDonePath[MAX_PATH * 4]{};
DWORD gDurationSeconds = 300;
DWORD gTargetChecksum = 0;
DWORD gStudioObjectRva = 0;
DWORD gStudioVtableRva = 0;
DWORD gDrawModelRva = 0;
DWORD gDrawModelSlotIndex = 0;
DWORD gResolveVirtualModelPoseRva = 0;
DWORD gBuildTransformationsRva = 0;
DWORD gGetStudioHdrRva = 0;
DWORD gSetupBonesRva = 0;
DWORD gModelRenderDrawModelRva = 0;
DWORD gModelRenderDrawModelShadowRva = 0;
DWORD gBaseEntityConstructRva = 0;
DWORD gBaseEntityDestructRva = 0;
DWORD gEvaluateSequencePoseRva = 0;
DWORD gDecodeSelectedBonesRva = 0;
DWORD gResolveBlendAxisWeightRva = 0;
DWORD gDecodeBoneQuaternionRva = 0;
DWORD gDecodeBonePositionRva = 0;
ConfiguredSignature gResolveExpected{};
ConfiguredSignature gBuildExpected{};
ConfiguredSignature gSetupBonesExpected{};
ConfiguredSignature gModelRenderDrawModelExpected{};
ConfiguredSignature gModelRenderDrawModelShadowExpected{};
ConfiguredSignature gBaseEntityConstructExpected{};
ConfiguredSignature gBaseEntityDestructExpected{};
ConfiguredSignature gEvaluateSequencePoseExpected{};
ConfiguredSignature gDecodeSelectedBonesExpected{};
ConfiguredSignature gResolveBlendAxisWeightExpected{};
ConfiguredSignature gDecodeBoneQuaternionExpected{};
ConfiguredSignature gDecodeBonePositionExpected{};
char gHookInstallError[128] = "unspecified";

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
    const LONG depth = InterlockedIncrement(&gQueueDepth);
    for (;;) {
        const LONG peak = InterlockedCompareExchange(&gQueuePeak, 0, 0);
        if (depth <= peak ||
            InterlockedCompareExchange(&gQueuePeak, depth, peak) == peak) {
            break;
        }
    }
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

HANDLE StreamHandle(std::uint32_t stream) {
    switch (stream) {
        case kStreamAnimation:
            return gAnimationOutput;
        case kStreamCensus:
            return gCensusOutput;
        case kStreamActor:
            return gActorOutput;
        case kStreamContribution:
            return gContributionOutput;
        default:
            return gOutput;
    }
}

HANDLE StreamHeap(std::uint32_t stream) {
    return stream == kStreamCensus && gCensusHeap ? gCensusHeap
                                                  : GetProcessHeap();
}

void DrainQueue() {
    PendingRecord* record = TakeQueue();
    while (record) {
        PendingRecord* next = record->next;
        const HANDLE output = StreamHandle(record->stream);
        if (output != INVALID_HANDLE_VALUE &&
            WriteAll(output, record->payload, record->bytes)) {
            InterlockedIncrement(&gWritten);
            InterlockedExchangeAdd64(
                &gBytesWritten, static_cast<LONG64>(record->bytes));
        } else {
            InterlockedIncrement(&gDropped);
        }
        InterlockedDecrement(&gQueueDepth);
        HeapFree(StreamHeap(record->stream), 0, record);
        record = next;
    }
}

void CaptureBracket(
    const char (&magic)[5], std::uint32_t generation,
    std::uint32_t parentGeneration, std::uint32_t depth, std::int64_t entryQpc,
    void* self, const std::uint32_t* arguments) {
    if (gAnimationOutput == INVALID_HANDLE_VALUE) {
        return;
    }
    const DWORD diskBytes = static_cast<DWORD>(sizeof(BracketRecordHeader));
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
    pending->stream = kStreamAnimation;
    auto* header = reinterpret_cast<BracketRecordHeader*>(pending->payload);
    std::memset(header, 0, sizeof(*header));
    std::memcpy(header->magic, magic, 4);
    header->recordBytes = diskBytes;
    header->sequence =
        static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    header->qpc = now.QuadPart;
    header->entryQpc = entryQpc;
    header->threadId = GetCurrentThreadId();
    header->generation = generation;
    header->parentGeneration = parentGeneration;
    header->depth = depth;
    header->clientEntity =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    for (int index = 0; index < 9; ++index) {
        header->arguments[index] = arguments[index];
    }
    Enqueue(pending);
}

// Returns the opened generation, or 0 when the bracket was not opened. A
// zero return is what tells the matching EndBracket to leave the stack alone.
std::uint32_t BeginBracket(void* self) {
    if (!InterlockedCompareExchange(&gCapturing, 0, 0)) {
        return 0;
    }
    if (gThread.depth >= kMaximumBracketDepth) {
        InterlockedIncrement(&gBracketOverflow);
        return 0;
    }
    const auto generation =
        static_cast<std::uint32_t>(InterlockedIncrement(&gGeneration));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    gThread.generations[gThread.depth] = generation;
    gThread.entities[gThread.depth] =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    gThread.entryQpc[gThread.depth] = now.QuadPart;
    ++gThread.depth;
    return generation;
}

void EndBracket(
    std::uint32_t generation, const char (&magic)[5], void* self,
    const std::uint32_t* arguments, bool poseBuild) {
    if (!generation || !gThread.depth ||
        gThread.generations[gThread.depth - 1] != generation) {
        return;
    }
    --gThread.depth;
    const std::uint32_t parentGeneration =
        gThread.depth ? gThread.generations[gThread.depth - 1] : 0;
    if (poseBuild) {
        gThread.lastPoseGeneration = generation;
        gThread.lastPoseEntity = gThread.entities[gThread.depth];
    }
    CaptureBracket(
        magic, generation, parentGeneration, gThread.depth,
        gThread.entryQpc[gThread.depth], self, arguments);
}

std::uint32_t CurrentGeneration() {
    if (!gThread.depth) {
        InterlockedIncrement(&gUnbracketed);
        return 0;
    }
    return gThread.generations[gThread.depth - 1];
}

// A contribution scope is opened before the sequence frame runs, because the
// cells it decodes emit their records while it is still on the stack. The
// dispatcher recurses for autolayers, so the scopes nest.
std::uint32_t BeginContribution() {
    if (!InterlockedCompareExchange(&gCapturing, 0, 0)) {
        return 0;
    }
    if (gThread.contributionDepth >= kMaximumBracketDepth) {
        InterlockedIncrement(&gContributionOverflow);
        return 0;
    }
    const auto scope =
        static_cast<std::uint32_t>(InterlockedIncrement(&gContributionScope));
    gThread.contributions[gThread.contributionDepth] = scope;
    ++gThread.contributionDepth;
    return scope;
}

void EndContribution(std::uint32_t scope) {
    if (!scope || !gThread.contributionDepth ||
        gThread.contributions[gThread.contributionDepth - 1] != scope) {
        return;
    }
    --gThread.contributionDepth;
}

std::uint32_t CurrentContribution() {
    if (!gThread.contributionDepth) {
        InterlockedIncrement(&gContributionUnscoped);
        return 0;
    }
    return gThread.contributions[gThread.contributionDepth - 1];
}

// The generation a census record sits inside, if any. Unlike CurrentGeneration
// this does not count an unbracketed record: a header sighting is not an
// evaluation, and the sweep at capture stop runs on the writer thread with no
// bracket at all, so counting either would move a total CAP2.1 measured.
std::uint32_t EnclosingGeneration() {
    return gThread.depth ? gThread.generations[gThread.depth - 1] : 0;
}

PendingRecord* AllocateCensusRecord(DWORD diskBytes) {
    const SIZE_T allocation =
        offsetof(PendingRecord, payload) + static_cast<SIZE_T>(diskBytes);
    auto* pending = static_cast<PendingRecord*>(
        HeapAlloc(StreamHeap(kStreamCensus), 0, allocation));
    if (!pending) {
        return nullptr;
    }
    pending->next = nullptr;
    pending->bytes = diskBytes;
    pending->stream = kStreamCensus;
    return pending;
}

// Reads the fixed studio-header fields the census keeps. Returns false when
// the pointer does not carry a v2531 studio header, so a stale or half-loaded
// pointer is rejected before anything large is copied off it.
bool ReadStudioHeaderFields(
    unsigned char* studioHdr, ModelObservationHeader* fields) {
    __try {
        const std::uint32_t magic =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x00);
        const std::uint32_t version =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x04);
        if (magic != kStudioMagic || version != kStudioVersion) {
            return false;
        }
        const std::uint32_t modelLength =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x8C);
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        const int boneIndex = *reinterpret_cast<int*>(studioHdr + 0xF4);
        if (!modelLength || boneCount <= 0 || boneCount > kMaximumBones ||
            boneIndex < 0) {
            return false;
        }
        fields->checksum =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x08);
        fields->boneCount = static_cast<std::uint32_t>(boneCount);
        fields->boneIndex = static_cast<std::uint32_t>(boneIndex);
        fields->modelLength = modelLength;
        fields->includeModelCount =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x194);
        fields->includeModelIndex =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x198);
        fields->studioVersion = version;
        std::memcpy(fields->modelName, studioHdr + 0x0C, 128);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Copies the model image the header sits at the front of. The sequence number
// is stamped only once the copy has succeeded, so a fault costs a counted
// failure rather than a gap in the counter that proves nothing was lost.
bool CaptureModelImage(
    unsigned char* studioHdr, const ModelObservationHeader& fields) {
    DWORD capturedBytes = fields.modelLength;
    DWORD capped = 0;
    if (capturedBytes > kCensusImageCap) {
        capturedBytes = kCensusImageCap;
        capped = 1;
    }
    const DWORD diskBytes =
        static_cast<DWORD>(sizeof(ModelImageHeader)) + capturedBytes;
    PendingRecord* pending = AllocateCensusRecord(diskBytes);
    if (!pending) {
        return false;
    }
    auto* header = reinterpret_cast<ModelImageHeader*>(pending->payload);
    std::memset(header, 0, sizeof(*header));
    __try {
        std::memcpy(
            pending->payload + sizeof(*header), studioHdr, capturedBytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HeapFree(StreamHeap(kStreamCensus), 0, pending);
        return false;
    }

    std::memcpy(header->magic, "MIMG", 4);
    header->recordBytes = diskBytes;
    header->sequence =
        static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    header->qpc = now.QuadPart;
    header->threadId = GetCurrentThreadId();
    header->studioHdr =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(studioHdr));
    header->checksum = fields.checksum;
    header->modelLength = fields.modelLength;
    header->capturedBytes = capturedBytes;
    header->capped = capped;
    if (capped) {
        InterlockedIncrement(&gCensusCapped);
    }
    InterlockedExchangeAdd64(
        &gCensusBytes, static_cast<LONG64>(capturedBytes));
    Enqueue(pending);
    return true;
}

// Stores the image once per checksum. A model loaded at two addresses produces
// two observations and one image, which is what keeps immutable source bytes a
// dictionary rather than a stream.
bool ObserveModelImage(
    unsigned char* studioHdr, const ModelObservationHeader& fields) {
    const LONG key = static_cast<LONG>(fields.checksum);
    if (!key) {
        return false;
    }
    std::uint32_t index =
        static_cast<std::uint32_t>(key) & (kCensusImageSlots - 1);
    for (std::uint32_t probe = 0; probe < kCensusProbeLimit; ++probe) {
        CensusSlot& slot = gCensusImageSlots[index];
        const LONG occupant = InterlockedCompareExchange(&slot.key, key, 0);
        if (occupant == 0) {
            if (CaptureModelImage(studioHdr, fields)) {
                InterlockedExchange(&slot.published, 1);
                InterlockedIncrement(&gCensusImages);
                return true;
            }
            InterlockedExchange(&slot.key, 0);
            InterlockedIncrement(&gCensusFaults);
            return false;
        }
        if (occupant == key) {
            return false;
        }
        index = (index + 1) & (kCensusImageSlots - 1);
    }
    InterlockedIncrement(&gCensusOverflow);
    return false;
}

bool EmitObservation(
    unsigned char* studioHdr, CensusSlot* slot,
    const ModelObservationHeader& fields, std::uint32_t reason,
    std::uint32_t previousChecksum) {
    const bool imageCaptured = ObserveModelImage(studioHdr, fields);
    const DWORD diskBytes =
        static_cast<DWORD>(sizeof(ModelObservationHeader));
    PendingRecord* pending = AllocateCensusRecord(diskBytes);
    if (!pending) {
        return false;
    }
    auto* header =
        reinterpret_cast<ModelObservationHeader*>(pending->payload);
    std::memcpy(header, &fields, sizeof(*header));
    std::memcpy(header->magic, "MOBS", 4);
    header->recordBytes = diskBytes;
    header->sequence =
        static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    header->qpc = now.QuadPart;
    header->threadId = GetCurrentThreadId();
    header->studioHdr =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(studioHdr));
    header->previousChecksum = previousChecksum;
    header->reason = reason;
    header->imageCaptured = imageCaptured ? 1u : 0u;
    header->generation = EnclosingGeneration();
    Enqueue(pending);
    InterlockedIncrement(&gCensusRecords);
    // Publishing after the record reaches the queue is the whole ordering: a
    // slot marked published is a promise that the header was recorded, so a
    // racing thread that stops looking cannot be the reason one goes missing.
    InterlockedExchange(&slot->value, static_cast<LONG>(fields.checksum));
    InterlockedExchange(&slot->published, 1);
    return true;
}

// Emits the census for a studio header this run has not seen at this address.
// The common case is one hash, one interlocked compare and a return, on a path
// that is already copying bone matrices.
void ObserveStudioHeader(unsigned char* studioHdr, std::uint32_t checksum) {
    if (gCensusOutput == INVALID_HANDLE_VALUE || !studioHdr) {
        return;
    }
    const LONG key =
        static_cast<LONG>(reinterpret_cast<std::uintptr_t>(studioHdr));
    const LONG value = static_cast<LONG>(checksum);
    if (!key) {
        return;
    }
    std::uint32_t index =
        (static_cast<std::uint32_t>(key) >> 4) & (kCensusSlots - 1);
    for (std::uint32_t probe = 0; probe < kCensusProbeLimit; ++probe) {
        CensusSlot& slot = gCensusHeaders[index];
        const LONG occupant = InterlockedCompareExchange(&slot.key, key, 0);
        if (occupant == 0) {
            ModelObservationHeader fields{};
            if (ReadStudioHeaderFields(studioHdr, &fields) &&
                EmitObservation(
                    studioHdr, &slot, fields, kObservationFirst, 0)) {
                return;
            }
            // Releasing the claim rather than stranding it keeps the header
            // recoverable on a later sighting. The cost is that a probe chain
            // another key walked past this slot can miss once, which re-emits
            // one observation and is counted; stranding it would lose the
            // header for the whole run instead.
            InterlockedExchange(&slot.key, 0);
            InterlockedIncrement(&gCensusFaults);
            return;
        }
        if (occupant == key) {
            if (!InterlockedCompareExchange(&slot.published, 0, 0)) {
                return;
            }
            const LONG previous =
                InterlockedCompareExchange(&slot.value, 0, 0);
            if (previous == value) {
                return;
            }
            // This address served one model and now serves another, which is
            // the only free this capture can observe. Only the thread that
            // wins the exchange emits, so one change cannot be recorded twice.
            if (InterlockedCompareExchange(&slot.value, value, previous) !=
                previous) {
                return;
            }
            ModelObservationHeader fields{};
            if (ReadStudioHeaderFields(studioHdr, &fields) &&
                EmitObservation(
                    studioHdr, &slot, fields, kObservationReplacement,
                    static_cast<std::uint32_t>(previous))) {
                InterlockedIncrement(&gCensusReplacements);
            } else {
                InterlockedIncrement(&gCensusFaults);
            }
            return;
        }
        index = (index + 1) & (kCensusSlots - 1);
    }
    InterlockedIncrement(&gCensusOverflow);
}

// Re-reads every published header at capture stop. No hooked target sees a
// model-cache free, so residency is what this capture can state instead: a
// header that still reads as its own studio header was never unloaded, and one
// that no longer does is counted rather than claimed as an unload event.
void SweepResidentHeaders() {
    if (gCensusOutput == INVALID_HANDLE_VALUE) {
        return;
    }
    for (std::uint32_t index = 0; index < kCensusSlots; ++index) {
        CensusSlot& slot = gCensusHeaders[index];
        if (!InterlockedCompareExchange(&slot.published, 0, 0)) {
            continue;
        }
        const LONG key = InterlockedCompareExchange(&slot.key, 0, 0);
        if (!key) {
            continue;
        }
        const LONG previous = InterlockedCompareExchange(&slot.value, 0, 0);
        auto* studioHdr = reinterpret_cast<unsigned char*>(
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(key)));
        ModelObservationHeader fields{};
        if (!ReadStudioHeaderFields(studioHdr, &fields)) {
            InterlockedIncrement(&gCensusVanished);
            continue;
        }
        if (EmitObservation(
                studioHdr, &slot, fields, kObservationResidentAtStop,
                static_cast<std::uint32_t>(previous))) {
            InterlockedIncrement(&gCensusResident);
        } else {
            InterlockedIncrement(&gCensusFaults);
        }
    }
}

// The identity an actor's studio header carries. The caller has already
// validated the header, so this is the same read the census performs, narrowed
// to the fields an actor record keeps.
bool ReadActorIdentity(
    unsigned char* studioHdr, ActorObservationHeader* fields) {
    __try {
        const std::uint32_t magic =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x00);
        const std::uint32_t version =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x04);
        if (magic != kStudioMagic || version != kStudioVersion) {
            return false;
        }
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            return false;
        }
        fields->checksum =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x08);
        fields->boneCount = static_cast<std::uint32_t>(boneCount);
        std::memcpy(fields->modelName, studioHdr + 0x0C, 64);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The renderable subobject the enclosing pose build was entered on. Taking it
// from the open bracket rather than computing it from the entity is what keeps
// CAP1.3's relation evidence: both addresses are recorded as observed, and
// whether they differ by four stays a question the offline verifier answers.
std::uint32_t EnclosingBracketEntity() {
    return gThread.depth ? gThread.entities[gThread.depth - 1] : 0;
}

bool EmitActorObservation(
    ActorSlot* slot, unsigned char* entity, std::uint32_t renderable,
    unsigned char* studioHdr, const ActorObservationHeader& fields,
    std::uint32_t reason, std::uint32_t previousChecksum) {
    const DWORD diskBytes = static_cast<DWORD>(sizeof(ActorObservationHeader));
    const SIZE_T allocation =
        offsetof(PendingRecord, payload) + static_cast<SIZE_T>(diskBytes);
    auto* pending = static_cast<PendingRecord*>(
        HeapAlloc(GetProcessHeap(), 0, allocation));
    if (!pending) {
        return false;
    }
    pending->next = nullptr;
    pending->bytes = diskBytes;
    pending->stream = kStreamActor;
    auto* header = reinterpret_cast<ActorObservationHeader*>(pending->payload);
    std::memcpy(header, &fields, sizeof(*header));
    std::memcpy(header->magic, "ACTR", 4);
    header->recordBytes = diskBytes;
    header->sequence =
        static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    header->qpc = now.QuadPart;
    header->threadId = GetCurrentThreadId();
    header->entity =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entity));
    header->renderable = renderable;
    header->reason = reason;
    header->generation = EnclosingGeneration();
    header->studioHdr =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(studioHdr));
    header->previousChecksum = previousChecksum;
    Enqueue(pending);
    InterlockedIncrement(&gActorRecords);
    InterlockedExchangeAdd64(&gActorBytes, static_cast<LONG64>(diskBytes));
    // Published after the record reaches the queue, for the same reason the
    // census publishes late: a slot marked published promises the actor was
    // recorded, so a racing thread that stops looking cannot lose one.
    InterlockedExchange(
        &slot->studioHdr, static_cast<LONG>(header->studioHdr));
    InterlockedExchange(&slot->value, static_cast<LONG>(fields.checksum));
    InterlockedExchange(&slot->published, 1);
    return true;
}

// Emits the actor census for a skeletal entity this run has not evaluated at
// this address. The header and checksum come from the caller, which validated
// them to build its own record, so the steady-state cost is one hash, one
// interlocked compare and a return.
void ObserveActor(
    unsigned char* entity, unsigned char* studioHdr, std::uint32_t checksum) {
    if (gActorOutput == INVALID_HANDLE_VALUE || !entity || !studioHdr) {
        return;
    }
    const LONG key =
        static_cast<LONG>(reinterpret_cast<std::uintptr_t>(entity));
    const LONG value = static_cast<LONG>(checksum);
    if (!key) {
        return;
    }
    const std::uint32_t renderable = EnclosingBracketEntity();
    std::uint32_t index =
        (static_cast<std::uint32_t>(key) >> 4) & (kActorSlots - 1);
    for (std::uint32_t probe = 0; probe < kActorProbeLimit; ++probe) {
        ActorSlot& slot = gActors[index];
        const LONG occupant = InterlockedCompareExchange(&slot.key, key, 0);
        if (occupant == 0) {
            ActorObservationHeader fields{};
            if (ReadActorIdentity(studioHdr, &fields) &&
                EmitActorObservation(
                    &slot, entity, renderable, studioHdr, fields, kActorFirst,
                    0)) {
                return;
            }
            // Released rather than stranded, as the census releases: an actor
            // whose header does not read stays recoverable on a later pose
            // build, at the cost of one re-emitted observation that is counted.
            InterlockedExchange(&slot.key, 0);
            InterlockedIncrement(&gActorFaults);
            return;
        }
        if (occupant == key) {
            if (!InterlockedCompareExchange(&slot.published, 0, 0)) {
                return;
            }
            const LONG previous =
                InterlockedCompareExchange(&slot.value, 0, 0);
            if (previous == value) {
                return;
            }
            // This address evaluated one model and now evaluates another.
            // Without a destruction target it is the only reuse this capture
            // witnesses, and it is what bounds an address join to an interval.
            if (InterlockedCompareExchange(&slot.value, value, previous) !=
                previous) {
                return;
            }
            ActorObservationHeader fields{};
            if (ReadActorIdentity(studioHdr, &fields) &&
                EmitActorObservation(
                    &slot, entity, renderable, studioHdr, fields,
                    kActorIdentityChange,
                    static_cast<std::uint32_t>(previous))) {
                InterlockedIncrement(&gActorIdentityChanges);
            } else {
                InterlockedIncrement(&gActorFaults);
            }
            return;
        }
        index = (index + 1) & (kActorSlots - 1);
    }
    InterlockedIncrement(&gActorOverflow);
}

// Emits a lifetime record: an address, a generation and a time, and nothing
// read off the object. A constructor has not given it a model yet and a
// destructor is taking it away, so any identity read at either point would be
// read from an object that does not have one.
bool EmitActorLifetime(unsigned char* entity, std::uint32_t reason) {
    if (gActorOutput == INVALID_HANDLE_VALUE || !entity) {
        return false;
    }
    const DWORD diskBytes = static_cast<DWORD>(sizeof(ActorObservationHeader));
    const SIZE_T allocation =
        offsetof(PendingRecord, payload) + static_cast<SIZE_T>(diskBytes);
    auto* pending = static_cast<PendingRecord*>(
        HeapAlloc(GetProcessHeap(), 0, allocation));
    if (!pending) {
        InterlockedIncrement(&gActorFaults);
        return false;
    }
    pending->next = nullptr;
    pending->bytes = diskBytes;
    pending->stream = kStreamActor;
    auto* header = reinterpret_cast<ActorObservationHeader*>(pending->payload);
    std::memset(header, 0, sizeof(*header));
    std::memcpy(header->magic, "ACTR", 4);
    header->recordBytes = diskBytes;
    header->sequence =
        static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    header->qpc = now.QuadPart;
    header->threadId = GetCurrentThreadId();
    header->entity =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(entity));
    header->reason = reason;
    header->generation = EnclosingGeneration();
    Enqueue(pending);
    InterlockedIncrement(&gActorRecords);
    InterlockedExchangeAdd64(&gActorBytes, static_cast<LONG64>(diskBytes));
    return true;
}

// Releases the slot an address held, so the next actor at that address is a
// first sighting rather than a continuation of the one that just died.
void RetireActor(unsigned char* entity) {
    const LONG key =
        static_cast<LONG>(reinterpret_cast<std::uintptr_t>(entity));
    if (!key) {
        return;
    }
    std::uint32_t index =
        (static_cast<std::uint32_t>(key) >> 4) & (kActorSlots - 1);
    for (std::uint32_t probe = 0; probe < kActorProbeLimit; ++probe) {
        ActorSlot& slot = gActors[index];
        const LONG occupant = InterlockedCompareExchange(&slot.key, 0, 0);
        if (occupant == key) {
            InterlockedExchange(&slot.published, 0);
            InterlockedExchange(&slot.value, 0);
            InterlockedExchange(&slot.studioHdr, 0);
            InterlockedExchange(&slot.key, 0);
            return;
        }
        if (!occupant) {
            return;
        }
        index = (index + 1) & (kActorSlots - 1);
    }
}

// True when this address is one the actor census already published. The
// destructor fires for every client entity, not only the skeletal ones, so this
// is what keeps the destruction record on the population CAP2.3 names.
bool IsPublishedActor(unsigned char* entity) {
    const LONG key =
        static_cast<LONG>(reinterpret_cast<std::uintptr_t>(entity));
    if (!key) {
        return false;
    }
    std::uint32_t index =
        (static_cast<std::uint32_t>(key) >> 4) & (kActorSlots - 1);
    for (std::uint32_t probe = 0; probe < kActorProbeLimit; ++probe) {
        ActorSlot& slot = gActors[index];
        const LONG occupant = InterlockedCompareExchange(&slot.key, 0, 0);
        if (occupant == key) {
            return InterlockedCompareExchange(&slot.published, 0, 0) != 0;
        }
        if (!occupant) {
            return false;
        }
        index = (index + 1) & (kActorSlots - 1);
    }
    return false;
}

// Re-reads every recorded actor's studio header at capture stop. No hooked
// target sees a client entity destructed, so residency is what this capture can
// state instead, and it is a statement about the model the actor named rather
// than about the object: a header that still reads as the same model was never
// replaced under that actor. The renderable is zero because the sweep runs on
// the writer thread with no bracket open, so no subobject address was observed.
void SweepResidentActors() {
    if (gActorOutput == INVALID_HANDLE_VALUE) {
        return;
    }
    for (std::uint32_t index = 0; index < kActorSlots; ++index) {
        ActorSlot& slot = gActors[index];
        if (!InterlockedCompareExchange(&slot.published, 0, 0)) {
            continue;
        }
        const LONG key = InterlockedCompareExchange(&slot.key, 0, 0);
        const LONG recorded = InterlockedCompareExchange(&slot.studioHdr, 0, 0);
        if (!key || !recorded) {
            continue;
        }
        const LONG previous = InterlockedCompareExchange(&slot.value, 0, 0);
        auto* entity = reinterpret_cast<unsigned char*>(
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(key)));
        auto* studioHdr = reinterpret_cast<unsigned char*>(
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(recorded)));
        ActorObservationHeader fields{};
        if (!ReadActorIdentity(studioHdr, &fields)) {
            InterlockedIncrement(&gActorVanished);
            continue;
        }
        if (EmitActorObservation(
                &slot, entity, 0, studioHdr, fields, kActorResidentAtStop,
                static_cast<std::uint32_t>(previous))) {
            InterlockedIncrement(&gActorResident);
        } else {
            InterlockedIncrement(&gActorFaults);
        }
    }
}

void CapturePose(
    void* self, std::uintptr_t modelInfo, const std::uintptr_t* drawArguments) {
    if (!modelInfo) {
        InterlockedIncrement(&gSkipped);
        return;
    }

    __try {
        auto* info = reinterpret_cast<unsigned char*>(modelInfo);
        auto* studioHdr = *reinterpret_cast<unsigned char**>(info);
        if (!studioHdr) {
            InterlockedIncrement(&gSkipped);
            return;
        }
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            InterlockedIncrement(&gSkipped);
            return;
        }
        const std::uint32_t checksum =
            *reinterpret_cast<std::uint32_t*>(studioHdr + 0x08);
        // Before the matrix buffers are checked: a header this run has not
        // seen is worth recording even on a draw whose pose buffers are not
        // allocated yet.
        ObserveStudioHeader(studioHdr, checksum);
        auto* object = static_cast<unsigned char*>(self);
        auto* boneToWorld = *reinterpret_cast<unsigned char**>(object + 0x5C);
        auto* skinPalette = *reinterpret_cast<unsigned char**>(object + 0x60);
        if (!boneToWorld || !skinPalette) {
            InterlockedIncrement(&gSkipped);
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
        pending->stream = kStreamPose;
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
        header->checksum = checksum;
        header->boneCount = static_cast<std::uint32_t>(boneCount);
        header->modelInfo = static_cast<std::uint32_t>(modelInfo);
        for (int index = 0; index < 5; ++index) {
            header->drawArguments[index] =
                static_cast<std::uint32_t>(drawArguments[index]);
        }
        std::memcpy(header->modelName, studioHdr + 0x0C, 64);
        header->generation = CurrentGeneration();
        header->generationEntity = gThread.lastPoseEntity;
        header->carryGeneration = gThread.lastPoseGeneration;
        // The info pointer was validated above by reading the studio header
        // through it, so this copy is bounded and off a page already touched.
        std::memcpy(header->renderInfo, info, kRenderInfoBytes);
        header->renderInfoBytes = kRenderInfoBytes;
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
    const void* selectedBones, const float* rootTransform) {
    if (gAnimationOutput == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!studioHdr || !positions || !quaternions) {
        InterlockedIncrement(&gSkipped);
        return;
    }

    __try {
        const DWORD checksum =
            *reinterpret_cast<DWORD*>(studioHdr + 0x08);
        if (gTargetChecksum && checksum != gTargetChecksum) {
            InterlockedIncrement(&gFiltered);
            return;
        }
        const int boneCount = *reinterpret_cast<int*>(studioHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            InterlockedIncrement(&gSkipped);
            return;
        }
        // After the configured filter: a run that asked for one model must not
        // pay a model image for every other one it happened to see.
        ObserveStudioHeader(studioHdr, checksum);
        // `self` is the C_BaseAnimating on both evaluation paths, and the
        // header and checksum are already validated here, so the actor census
        // costs no further read off the entity.
        ObserveActor(
            static_cast<unsigned char*>(self), studioHdr, checksum);

        const DWORD positionBytes =
            static_cast<DWORD>(boneCount) * 3u * sizeof(float);
        const DWORD quaternionBytes =
            static_cast<DWORD>(boneCount) * 4u * sizeof(float);
        const DWORD selectedBytes =
            static_cast<DWORD>((boneCount + 31) / 32) * sizeof(DWORD);
        const DWORD rootBytes = rootTransform ? kRootTransformBytes : 0u;
        const DWORD diskBytes =
            static_cast<DWORD>(sizeof(AnimationRecordHeader)) +
            positionBytes + quaternionBytes + selectedBytes + rootBytes;
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
        pending->stream = kStreamAnimation;
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
        header->generation = CurrentGeneration();
        header->generationDepth = gThread.depth;
        header->rootTransform = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(rootTransform));
        header->rootTransformBytes = rootBytes;
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
        if (rootBytes) {
            std::memcpy(selectedOutput + selectedBytes, rootTransform,
                        rootBytes);
        }
        Enqueue(pending);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&gDropped);
    }
}

// One fired contribution. `ownerHdr` is argument zero of the frame that fired,
// so the owner is witnessed rather than resolved from the entity's own model —
// which is the whole point of the task: a bank model is nobody's entity model.
void CaptureContribution(
    const char (&magic)[5], std::uint32_t scope, unsigned char* ownerHdr,
    int sequenceIndex, unsigned char* animationDescriptor, float cycle,
    const float* poseParameters, const void* selectedBones,
    std::uintptr_t callerAddress, std::uint32_t channelEpoch = 0) {
    if (gContributionOutput == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!ownerHdr) {
        InterlockedIncrement(&gSkipped);
        return;
    }

    __try {
        const DWORD checksum = *reinterpret_cast<DWORD*>(ownerHdr + 0x08);
        if (gTargetChecksum && checksum != gTargetChecksum) {
            InterlockedIncrement(&gFiltered);
            return;
        }
        const int boneCount =
            *reinterpret_cast<int*>(ownerHdr + 0xF0);
        if (boneCount <= 0 || boneCount > kMaximumBones) {
            InterlockedIncrement(&gSkipped);
            return;
        }
        // The owner is what a later join needs the bytes of, and a bank model
        // reaches no other census path, so the contribution is where it is
        // observed.
        ObserveStudioHeader(ownerHdr, checksum);

        std::uint32_t faults = 0;
        if (!scope) {
            faults |= kFaultNoContributionScope;
        }

        const bool isSequence = std::memcmp(magic, "SEQP", 4) == 0;
        unsigned char* sequenceDescriptor = nullptr;
        std::int32_t numBlends = 0;
        std::int32_t groupSize[kBlendAxes] = {0, 0};
        std::int32_t paramIndex[kBlendAxes] = {0, 0};
        std::int32_t blendCell[kBlendAxes] = {0, 0};
        float blendWeight[kBlendAxes] = {0.0f, 0.0f};
        std::int32_t resolvedSequence = sequenceIndex;
        if (isSequence) {
            __try {
                const int localCount = *reinterpret_cast<int*>(
                    ownerHdr + kStudioLocalSeqCount);
                // The callee clamps an out-of-range index to zero before it
                // indexes, so the record names the descriptor that was actually
                // read and flags that the argument disagreed.
                if (resolvedSequence < 0 || resolvedSequence >= localCount) {
                    resolvedSequence = 0;
                    faults |= kFaultSequenceOutOfRange;
                }
                sequenceDescriptor =
                    ownerHdr +
                    *reinterpret_cast<int*>(ownerHdr + kStudioLocalSeqIndex) +
                    static_cast<DWORD>(resolvedSequence) *
                        kSequenceDescriptorBytes;
                numBlends = *reinterpret_cast<std::int32_t*>(
                    sequenceDescriptor + kSequenceNumBlends);
                for (std::uint32_t axis = 0; axis < kBlendAxes; ++axis) {
                    groupSize[axis] = *reinterpret_cast<std::int32_t*>(
                        sequenceDescriptor + kSequenceGroupSize + axis * 4);
                    paramIndex[axis] = *reinterpret_cast<std::int32_t*>(
                        sequenceDescriptor + kSequenceParamIndex + axis * 4);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                sequenceDescriptor = nullptr;
                faults |= kFaultSequenceDescriptor;
            }
            const auto descriptorKey = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sequenceDescriptor));
            for (std::uint32_t axis = 0; axis < kBlendAxes; ++axis) {
                // A stash entry from 0x1008c060's own use of the resolver names
                // a different descriptor, so it is refused rather than read.
                if (descriptorKey &&
                    gThread.blendDescriptor[axis] == descriptorKey) {
                    blendCell[axis] = gThread.blendCell[axis];
                    blendWeight[axis] = gThread.blendWeight[axis];
                } else {
                    faults |= kFaultBlendUnwitnessed;
                }
            }
        }

        std::int32_t animationIndex = -1;
        if (animationDescriptor) {
            __try {
                const std::ptrdiff_t offset =
                    animationDescriptor - ownerHdr -
                    *reinterpret_cast<int*>(ownerHdr + kStudioLocalAnimIndex);
                animationIndex =
                    offset >= 0 && offset % kAnimationDescriptorBytes == 0
                        ? static_cast<std::int32_t>(
                              offset / kAnimationDescriptorBytes)
                        : -1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                animationIndex = -1;
                faults |= kFaultAnimationDescriptor;
            }
        }

        // The mask travels with the sequence frame only. Every cell inside it
        // receives the same pointer, so repeating it per cell would pay for the
        // same bytes once per blend.
        const DWORD maskWords = static_cast<DWORD>((boneCount + 31) / 32);
        const DWORD selectedBytes =
            isSequence ? maskWords * sizeof(DWORD) : 0u;
        // A cell carries which bones each decoder actually ran for. The width is
        // the same closed form as the mask, doubled, so a reader re-derives it
        // from bone count rather than trusting a stored length.
        const DWORD channelBytes =
            isSequence ? 0u : 2u * maskWords * sizeof(DWORD);
        const DWORD poseBytes = isSequence ? kPoseParameterBytes : 0u;
        const DWORD diskBytes =
            static_cast<DWORD>(sizeof(ContributionRecordHeader)) + poseBytes +
            selectedBytes + channelBytes;
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
        pending->stream = kStreamContribution;
        auto* header =
            reinterpret_cast<ContributionRecordHeader*>(pending->payload);
        std::memset(header, 0, sizeof(*header));
        std::memcpy(header->magic, magic, 4);
        header->recordBytes = diskBytes;
        header->sequence =
            static_cast<std::uint32_t>(InterlockedIncrement(&gSequence));
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        header->qpc = now.QuadPart;
        header->threadId = GetCurrentThreadId();
        header->generation = CurrentGeneration();
        header->generationDepth = gThread.depth;
        header->generationEntity =
            gThread.depth ? gThread.entities[gThread.depth - 1] : 0;
        header->contribution = scope;
        header->callerAddress = static_cast<std::uint32_t>(callerAddress);
        header->ownerStudioHdr = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(ownerHdr));
        header->ownerChecksum = checksum;
        header->ownerBoneCount = static_cast<std::uint32_t>(boneCount);
        header->sequenceIndex = isSequence ? resolvedSequence : -1;
        header->animationIndex = animationIndex;
        header->sequenceDescriptor = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(sequenceDescriptor));
        header->animationDescriptor = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(animationDescriptor));
        header->boneMask = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(selectedBones));
        header->cycle = cycle;
        header->numBlends = numBlends;
        for (std::uint32_t axis = 0; axis < kBlendAxes; ++axis) {
            header->groupSize[axis] = groupSize[axis];
            header->paramIndex[axis] = paramIndex[axis];
            header->blendCell[axis] = blendCell[axis];
            header->blendWeight[axis] = blendWeight[axis];
        }
        // An accumulator carrying any epoch but this frame's belongs to another
        // cell, on the same terms the blend stash is refused above.
        const bool witnessed = !isSequence && channelEpoch != 0 &&
            gThread.channelEpoch == channelEpoch && gThread.channelBase != 0;
        if (!isSequence) {
            if (witnessed) {
                header->channelRecordBase = gThread.channelBase;
                header->channelBoneBase = gThread.channelBoneBase;
                header->channelQuaternionCalls = gThread.channelQuaternionCalls;
                header->channelPositionCalls = gThread.channelPositionCalls;
                header->channelFrame = gThread.channelFrame;
                header->channelFraction = gThread.channelFraction;
                faults |= gThread.channelFaults;
            } else {
                // A cell whose every animation record carries a zero weight
                // decodes nothing at all, so an absent accumulator is a real
                // outcome and is recorded as one rather than as a loss.
                faults |= kFaultChannelsUnwitnessed;
                InterlockedIncrement(&gChannelUnwitnessed);
            }
        }

        auto* cursor = pending->payload + sizeof(*header);
        if (poseBytes) {
            if (poseParameters) {
                __try {
                    std::memcpy(cursor, poseParameters, poseBytes);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    std::memset(cursor, 0, poseBytes);
                    faults |= kFaultPoseParameters;
                }
            } else {
                std::memset(cursor, 0, poseBytes);
                faults |= kFaultPoseParameters;
            }
            cursor += poseBytes;
        }
        if (selectedBytes) {
            if (selectedBones) {
                __try {
                    std::memcpy(
                        cursor,
                        static_cast<const unsigned char*>(selectedBones) + 4,
                        selectedBytes);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    std::memset(cursor, 0, selectedBytes);
                    faults |= kFaultSelectedBones;
                }
            } else {
                std::memset(cursor, 0, selectedBytes);
                faults |= kFaultSelectedBones;
            }
            cursor += selectedBytes;
        }
        if (channelBytes) {
            // Copied from thread-local memory this frame owns, so there is
            // nothing here that can fault and nothing to guard.
            const DWORD half = maskWords * sizeof(DWORD);
            if (witnessed) {
                std::memcpy(cursor, gThread.channelQuaternionBits, half);
                std::memcpy(cursor + half, gThread.channelPositionBits, half);
            } else {
                std::memset(cursor, 0, channelBytes);
            }
        }
        header->faults = faults;
        header->poseParameterBytes = poseBytes;
        header->selectedBoneBytes = selectedBytes;
        header->channelBoneBytes = channelBytes;
        if (faults) {
            InterlockedIncrement(&gContributionFaults);
        }
        InterlockedIncrement(
            isSequence ? &gContributionSequences : &gContributionAnimations);
        InterlockedExchangeAdd64(
            &gContributionBytes, static_cast<LONG64>(diskBytes));
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
            selectedBones, nullptr);
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
        // The root/entity transform is the third argument the composed-pose
        // stage already receives; nothing else on the skeletal path carries the
        // frame the pose is placed into.
        CaptureAnimation(
            "FINL", self, studioHdr, positions, quaternions,
            studioSequence, -1.0f, cycle, 0, selectedBones, rootTransform);
    }
    InterlockedDecrement(&gActiveHooks);
}

void ResetChannelAccumulator() {
    gThread.channelBase = 0;
    gThread.channelBoneBase = 0;
    gThread.channelQuaternionCalls = 0;
    gThread.channelPositionCalls = 0;
    gThread.channelFrame = 0;
    gThread.channelFraction = 0.0f;
    gThread.channelFaults = 0;
    std::memset(
        gThread.channelQuaternionBits, 0,
        sizeof(gThread.channelQuaternionBits));
    std::memset(
        gThread.channelPositionBits, 0, sizeof(gThread.channelPositionBits));
}

// Fold one decoded bone into the enclosing cell's accumulator. The bone index
// comes from the pointer the decoder was handed rather than from a counter,
// because the point of the record is to witness that pointer: a value off the
// stride is a fault rather than a rounded-down index.
//
// Nothing is dereferenced. Both arguments are pointers the callee has already
// read, so there is no page to validate and no __try worth the frame.
void AccumulateChannel(
    bool quaternion, unsigned char* animationRecord, unsigned char* bone,
    int frame, float fraction) {
    if (!gThread.channelEpoch) {
        return;
    }
    if (!gThread.channelBase) {
        gThread.channelBase =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(animationRecord));
        gThread.channelBoneBase =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bone));
        gThread.channelFrame = frame;
        gThread.channelFraction = fraction;
    }
    const std::uint32_t delta =
        static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(animationRecord)) -
        gThread.channelBase;
    if (delta % kAnimationRecordBytes) {
        gThread.channelFaults |= kFaultChannelStride;
        InterlockedIncrement(&gChannelStrideFaults);
        return;
    }
    const std::uint32_t index = delta / kAnimationRecordBytes;
    if (index >= static_cast<std::uint32_t>(kMaximumBones)) {
        gThread.channelFaults |= kFaultChannelOverflow;
        return;
    }
    if (quaternion) {
        gThread.channelQuaternionBits[index >> 5] |= 1u << (index & 31);
        ++gThread.channelQuaternionCalls;
    } else {
        gThread.channelPositionBits[index >> 5] |= 1u << (index & 31);
        ++gThread.channelPositionCalls;
    }
}

// The two channel decoders emit no record of their own. Each folds one bone into
// the accumulator the cell frame below opened, so a witnessed per-bone decode
// costs record volume only as two bitmaps on the cell.
//
// gActiveHooks is still taken. The enclosing cell frame holds it too and is the
// only caller either decoder has, so the count looks redundant — but RemoveHooks
// disables that frame first, and every call after that reaches these detours
// without it. The count a release waits on has to be this frame's own.
void __cdecl HookDecodeBoneQuaternion(
    unsigned char* studioHdr, int frame, float fraction, unsigned char* bone,
    unsigned char* animationRecord, float* output) {
    InterlockedIncrement(&gActiveHooks);
    gOriginalDecodeBoneQuaternion(
        studioHdr, frame, fraction, bone, animationRecord, output);
    AccumulateChannel(true, animationRecord, bone, frame, fraction);
    InterlockedDecrement(&gActiveHooks);
}

void __cdecl HookDecodeBonePosition(
    unsigned char* studioHdr, int frame, float fraction, unsigned char* bone,
    unsigned char* animationRecord, float* output) {
    InterlockedIncrement(&gActiveHooks);
    gOriginalDecodeBonePosition(
        studioHdr, frame, fraction, bone, animationRecord, output);
    AccumulateChannel(false, animationRecord, bone, frame, fraction);
    InterlockedDecrement(&gActiveHooks);
}

// The blend resolver emits no record of its own. It stashes the axis it just
// resolved on the calling thread, and the sequence frame that asked for it
// folds both axes into one contribution record, so witnessed weights cost no
// record volume at all.
void __cdecl HookResolveBlendAxisWeight(
    unsigned char* studioHdr, float* poseParameters,
    unsigned char* sequenceDescriptor, int axis, float* outWeight,
    int* outCell) {
    InterlockedIncrement(&gActiveHooks);
    gOriginalResolveBlendAxisWeight(
        studioHdr, poseParameters, sequenceDescriptor, axis, outWeight,
        outCell);
    if (InterlockedCompareExchange(&gCapturing, 0, 0) &&
        axis >= 0 && static_cast<std::uint32_t>(axis) < kBlendAxes) {
        __try {
            gThread.blendCell[axis] = outCell ? *outCell : 0;
            gThread.blendWeight[axis] = outWeight ? *outWeight : 0.0f;
            gThread.blendDescriptor[axis] = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sequenceDescriptor));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            gThread.blendDescriptor[axis] = 0;
        }
    }
    InterlockedDecrement(&gActiveHooks);
}

// The sequence frame opens its scope before running, because the cells it
// decodes reach the queue while it is still on the stack. __finally keeps the
// pop and the unload refcount correct across an unwind out of retail, on the
// same terms as the bracket detours below.
void __cdecl HookEvaluateSequencePose(
    unsigned char* studioHdr, float* positions, float* quaternions,
    int sequence, float cycle, float* poseParameters, void* selectedBones) {
    InterlockedIncrement(&gActiveHooks);
    const std::uintptr_t caller =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uint32_t scope = BeginContribution();
    __try {
        gOriginalEvaluateSequencePose(
            studioHdr, positions, quaternions, sequence, cycle, poseParameters,
            selectedBones);
    } __finally {
        if (InterlockedCompareExchange(&gCapturing, 0, 0)) {
            CaptureContribution(
                "SEQP", scope, studioHdr, sequence, nullptr, cycle,
                poseParameters, selectedBones, caller);
        }
        EndContribution(scope);
        InterlockedDecrement(&gActiveHooks);
    }
}

// Unlike the blend stash, which a different frame reads, this frame both opens
// the accumulator and reads it back, and its record is emitted after the
// original returns — so the reset has to happen before the call rather than
// after it. __finally is what restores the epoch across an unwind out of retail;
// without it an orphaned epoch would let the next cell claim these bones.
void __cdecl HookDecodeSelectedBones(
    unsigned char* studioHdr, float* positions, float* quaternions,
    unsigned char* animationDescriptor, float cycle, void* selectedBones) {
    InterlockedIncrement(&gActiveHooks);
    const std::uintptr_t caller =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uint32_t enclosing = gThread.channelEpoch;
    const std::uint32_t epoch = enclosing + 1;
    gThread.channelEpoch = epoch;
    ResetChannelAccumulator();
    if (enclosing) {
        // The call graph says this frame does not recurse — the autolayer
        // recursion is a level above, in the dispatcher. Stamping it makes that
        // a claim the capture can refute rather than one it assumes.
        gThread.channelFaults |= kFaultChannelNested;
        InterlockedIncrement(&gChannelNested);
    }
    __try {
        gOriginalDecodeSelectedBones(
            studioHdr, positions, quaternions, animationDescriptor, cycle,
            selectedBones);
    } __finally {
        if (InterlockedCompareExchange(&gCapturing, 0, 0)) {
            CaptureContribution(
                "ANIM", CurrentContribution(), studioHdr, -1,
                animationDescriptor, cycle, nullptr, selectedBones, caller,
                epoch);
        }
        gThread.channelEpoch = enclosing;
        InterlockedDecrement(&gActiveHooks);
    }
}

// The two bracket detours are the only hooks that run work before the
// original. __finally is what makes the pop and the unload refcount survive an
// unwind out of retail; without it a single unwind would leak a stack entry and
// leave RemoveHooks spinning on gActiveHooks forever.
bool __fastcall HookSetupBones(
    void* self, void*, std::uint32_t argument0, std::uint32_t argument1,
    std::uint32_t argument2, std::uint32_t argument3,
    std::uint32_t argument4) {
    InterlockedIncrement(&gActiveHooks);
    const std::uint32_t arguments[9] = {
        argument0, argument1, argument2, argument3, argument4, 0, 0, 0, 0};
    const std::uint32_t generation = BeginBracket(self);
    bool result = false;
    __try {
        result = gOriginalSetupBones(
            self, argument0, argument1, argument2, argument3, argument4);
    } __finally {
        EndBracket(generation, "PBLD", self, arguments, true);
        InterlockedDecrement(&gActiveHooks);
    }
    return result;
}

// The ordinary engine draw frame, not the client one.
// C_BaseAnimating::InternalDrawModel encloses only the draws routed through
// that class; this reaches the StudioRender draw through
// CModelRender::RenderModel, and calls SetupBones on the way, so one bracket
// covers both siblings.
int __fastcall HookModelRenderDrawModel(
    void* self, void*, std::uint32_t argument0, std::uint32_t argument1,
    std::uint32_t argument2, std::uint32_t argument3, std::uint32_t argument4,
    std::uint32_t argument5, std::uint32_t argument6, std::uint32_t argument7,
    std::uint32_t argument8) {
    InterlockedIncrement(&gActiveHooks);
    const std::uint32_t arguments[9] = {
        argument0, argument1, argument2, argument3, argument4,
        argument5, argument6, argument7, argument8};
    const std::uint32_t generation = BeginBracket(self);
    int result = 0;
    __try {
        result = gOriginalModelRenderDrawModel(
            self, argument0, argument1, argument2, argument3, argument4,
            argument5, argument6, argument7, argument8);
    } __finally {
        EndBracket(generation, "DBLD", self, arguments, false);
        InterlockedDecrement(&gActiveHooks);
    }
    return result;
}

// The other engine draw frame. The client shadow manager enters it through
// VEngineModel006 slot +0x44, and it reaches the StudioRender draw directly
// rather than through CModelRender::RenderModel, so no ordinary draw bracket
// is open around it. It builds its own pose through the same renderable
// SetupBones slot, which is why it is a bracket and not a bare record.
void __fastcall HookModelRenderDrawModelShadow(
    void* self, void*, std::uint32_t argument0, std::uint32_t argument1,
    std::uint32_t argument2, std::uint32_t argument3) {
    InterlockedIncrement(&gActiveHooks);
    const std::uint32_t arguments[9] = {
        argument0, argument1, argument2, argument3, 0, 0, 0, 0, 0};
    const std::uint32_t generation = BeginBracket(self);
    __try {
        gOriginalModelRenderDrawModelShadow(
            self, argument0, argument1, argument2, argument3);
    } __finally {
        EndBracket(generation, "SHDW", self, arguments, false);
        InterlockedDecrement(&gActiveHooks);
    }
}

// Construction is recorded after the original returns, because that is when the
// subobject vtables exist and the address is an entity rather than raw storage.
//
// This is the shared base constructor, so it fires for every client entity and
// not only the skeletal ones. A live filter is impossible here — the object has
// no model yet, and the derived constructor that would give it one has not run
// — so the record keeps the address and the offline join against the actor
// census is what narrows it to the population CAP2.3 names.
void RecordActorConstruction(void* self) {
    if (!InterlockedCompareExchange(&gCapturing, 0, 0)) {
        return;
    }
    // A constructor reusing an address the census still holds means the
    // previous actor died without this run seeing it, so the slot is released
    // before the record rather than after.
    RetireActor(static_cast<unsigned char*>(self));
    if (EmitActorLifetime(
            static_cast<unsigned char*>(self), kActorConstruct)) {
        InterlockedIncrement(&gActorConstructions);
    }
}

void* __fastcall HookBaseEntityConstruct(void* self, void*) {
    InterlockedIncrement(&gActiveHooks);
    void* result = nullptr;
    __try {
        result = gOriginalBaseEntityConstruct(self);
    } __finally {
        RecordActorConstruction(self);
        InterlockedDecrement(&gActiveHooks);
    }
    return result;
}

// Destruction is recorded before the original runs and only for an address the
// census already published. Every client entity passes through here, so the
// filter is what keeps the record on the skeletal population; and recording
// first is what stops a slot being re-claimed at the same address ahead of its
// own destruction record.
void __fastcall HookBaseEntityDestruct(void* self, void*) {
    InterlockedIncrement(&gActiveHooks);
    if (InterlockedCompareExchange(&gCapturing, 0, 0) &&
        IsPublishedActor(static_cast<unsigned char*>(self))) {
        if (EmitActorLifetime(
                static_cast<unsigned char*>(self), kActorDestruct)) {
            InterlockedIncrement(&gActorDestructions);
        }
        RetireActor(static_cast<unsigned char*>(self));
    }
    __try {
        gOriginalBaseEntityDestruct(self);
    } __finally {
        InterlockedDecrement(&gActiveHooks);
    }
}

const elysium::capture::BinaryProfile* FindProfile(
    const wchar_t* moduleName) {
    for (const auto& profile : elysium::capture::profiles::Registry) {
        if (_wcsicmp(profile.ModuleName, moduleName) == 0) {
            return &profile;
        }
    }
    return nullptr;
}

const elysium::capture::BinaryTargetProfile* FindTarget(
    const elysium::capture::BinaryProfile& profile,
    const char* semanticLabel) {
    for (std::uint32_t index = 0; index < profile.TargetCount; ++index) {
        if (std::strcmp(
                profile.Targets[index].SemanticLabel,
                semanticLabel) == 0) {
            return &profile.Targets[index];
        }
    }
    return nullptr;
}

bool MatchesConfiguredSignature(
    const elysium::capture::BinaryTargetProfile& target,
    DWORD configuredRva,
    const ConfiguredSignature& configured) {
    return target.Rva == configuredRva &&
        target.ExpectedByteCount == configured.count &&
        std::memcmp(
            target.ExpectedBytes,
            configured.bytes,
            configured.count) == 0;
}

bool MatchesConfiguredProfile(
    const elysium::capture::BinaryTargetProfile& drawModel,
    const elysium::capture::BinaryTargetProfile& resolvePose,
    const elysium::capture::BinaryTargetProfile& buildTransformations,
    const elysium::capture::BinaryTargetProfile& getStudioHdr,
    const elysium::capture::BinaryTargetProfile& setupBones,
    const elysium::capture::BinaryTargetProfile& modelRenderDrawModel,
    const elysium::capture::BinaryTargetProfile& modelRenderDrawModelShadow,
    const elysium::capture::BinaryTargetProfile& baseEntityConstruct,
    const elysium::capture::BinaryTargetProfile& baseEntityDestruct,
    const elysium::capture::BinaryTargetProfile& evaluateSequencePose,
    const elysium::capture::BinaryTargetProfile& decodeSelectedBones,
    const elysium::capture::BinaryTargetProfile& resolveBlendAxisWeight,
    const elysium::capture::BinaryTargetProfile& decodeBoneQuaternion,
    const elysium::capture::BinaryTargetProfile& decodeBonePosition,
    bool animationEnabled, bool lifetimeEnabled, bool contributionEnabled) {
    const bool drawMatches = drawModel.Rva == gDrawModelRva &&
        drawModel.ObjectRva == gStudioObjectRva &&
        drawModel.ExpectedVtableRva == gStudioVtableRva &&
        drawModel.VtableSlot == gDrawModelSlotIndex;
    if (!drawMatches || !animationEnabled) {
        return drawMatches;
    }
    if (lifetimeEnabled &&
        !(MatchesConfiguredSignature(
              baseEntityConstruct, gBaseEntityConstructRva,
              gBaseEntityConstructExpected) &&
          MatchesConfiguredSignature(
              baseEntityDestruct, gBaseEntityDestructRva,
              gBaseEntityDestructExpected))) {
        return false;
    }
    if (contributionEnabled &&
        !(MatchesConfiguredSignature(
              evaluateSequencePose, gEvaluateSequencePoseRva,
              gEvaluateSequencePoseExpected) &&
          MatchesConfiguredSignature(
              decodeSelectedBones, gDecodeSelectedBonesRva,
              gDecodeSelectedBonesExpected) &&
          MatchesConfiguredSignature(
              resolveBlendAxisWeight, gResolveBlendAxisWeightRva,
              gResolveBlendAxisWeightExpected) &&
          MatchesConfiguredSignature(
              decodeBoneQuaternion, gDecodeBoneQuaternionRva,
              gDecodeBoneQuaternionExpected) &&
          MatchesConfiguredSignature(
              decodeBonePosition, gDecodeBonePositionRva,
              gDecodeBonePositionExpected))) {
        return false;
    }
    return getStudioHdr.Rva == gGetStudioHdrRva &&
        MatchesConfiguredSignature(
            resolvePose, gResolveVirtualModelPoseRva, gResolveExpected) &&
        MatchesConfiguredSignature(
            buildTransformations, gBuildTransformationsRva, gBuildExpected) &&
        MatchesConfiguredSignature(
            setupBones, gSetupBonesRva, gSetupBonesExpected) &&
        MatchesConfiguredSignature(
            modelRenderDrawModel, gModelRenderDrawModelRva,
            gModelRenderDrawModelExpected) &&
        MatchesConfiguredSignature(
            modelRenderDrawModelShadow, gModelRenderDrawModelShadowRva,
            gModelRenderDrawModelShadowExpected);
}

bool InstallHooks(HMODULE studioRender, HMODULE client, HMODULE engine) {
    using elysium::capture::ActiveBinaryProfile;
    using elysium::capture::HookBackendResult;
    using elysium::capture::HookBackends;

    const auto* studioProfile = FindProfile(L"StudioRender.dll");
    const auto* clientProfile = FindProfile(L"client.dll");
    const auto* engineProfile = FindProfile(L"engine.dll");
    if (studioProfile == nullptr || clientProfile == nullptr ||
        engineProfile == nullptr) {
        strcpy_s(gHookInstallError, "profile-not-found");
        return false;
    }
    const auto* drawModel = FindTarget(
        *studioProfile, "studiorender.draw_model");
    const auto* resolvePose = FindTarget(
        *clientProfile, "client.resolve_virtual_model_pose");
    const auto* buildTransformations = FindTarget(
        *clientProfile, "client.build_transformations");
    const auto* getStudioHdr = FindTarget(
        *clientProfile, "client.get_studio_hdr");
    const auto* setupBones = FindTarget(
        *clientProfile, "client.setup_bones");
    const auto* modelRenderDrawModel = FindTarget(
        *engineProfile, "engine.model_render_draw_model");
    const auto* modelRenderDrawModelShadow = FindTarget(
        *engineProfile, "engine.model_render_draw_model_shadow");
    const auto* baseEntityConstruct = FindTarget(
        *clientProfile, "client.base_entity_construct");
    const auto* baseEntityDestruct = FindTarget(
        *clientProfile, "client.base_entity_destruct");
    const auto* evaluateSequencePose = FindTarget(
        *clientProfile, "client.evaluate_sequence_pose");
    const auto* decodeSelectedBones = FindTarget(
        *clientProfile, "client.decode_selected_bones");
    const auto* resolveBlendAxisWeight = FindTarget(
        *clientProfile, "client.resolve_blend_axis_weight");
    const auto* decodeBoneQuaternion = FindTarget(
        *clientProfile, "client.decode_bone_quaternion");
    const auto* decodeBonePosition = FindTarget(
        *clientProfile, "client.decode_bone_position");
    const bool lifetimeEnabled = gActorOutput != INVALID_HANDLE_VALUE;
    const bool contributionEnabled =
        gContributionOutput != INVALID_HANDLE_VALUE;
    if (drawModel == nullptr || resolvePose == nullptr ||
        buildTransformations == nullptr || getStudioHdr == nullptr ||
        setupBones == nullptr || modelRenderDrawModel == nullptr ||
        modelRenderDrawModelShadow == nullptr ||
        baseEntityConstruct == nullptr || baseEntityDestruct == nullptr ||
        evaluateSequencePose == nullptr || decodeSelectedBones == nullptr ||
        resolveBlendAxisWeight == nullptr ||
        decodeBoneQuaternion == nullptr || decodeBonePosition == nullptr ||
        !MatchesConfiguredProfile(
            *drawModel,
            *resolvePose,
            *buildTransformations,
            *getStudioHdr,
            *setupBones,
            *modelRenderDrawModel,
            *modelRenderDrawModelShadow,
            *baseEntityConstruct,
            *baseEntityDestruct,
            *evaluateSequencePose,
            *decodeSelectedBones,
            *resolveBlendAxisWeight,
            *decodeBoneQuaternion,
            *decodeBonePosition,
            gAnimationOutput != INVALID_HANDLE_VALUE,
            lifetimeEnabled,
            contributionEnabled)) {
        strcpy_s(gHookInstallError, "declaration-mismatch");
        return false;
    }

    const ActiveBinaryProfile studioActive{
        studioProfile,
        reinterpret_cast<std::uintptr_t>(studioRender),
        studioProfile->Pe.SizeOfImage,
    };
    const ActiveBinaryProfile clientActive{
        clientProfile,
        reinterpret_cast<std::uintptr_t>(client),
        clientProfile->Pe.SizeOfImage,
    };
    const ActiveBinaryProfile engineActive{
        engineProfile,
        reinterpret_cast<std::uintptr_t>(engine),
        engineProfile->Pe.SizeOfImage,
    };
    const HookBackendResult drawResult = HookBackends::Install(
            studioActive,
            *drawModel,
            reinterpret_cast<void*>(&HookDrawModel),
            &gDrawModelHook);
    if (drawResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "draw-model-backend-%u",
            static_cast<unsigned>(drawResult));
        return false;
    }
    gOriginalDrawModel = reinterpret_cast<DrawModelFn>(
        gDrawModelHook.Original);

    if (gAnimationOutput == INVALID_HANDLE_VALUE) {
        return true;
    }

    auto* clientBase = reinterpret_cast<unsigned char*>(client);
    gGetStudioHdr = reinterpret_cast<GetStudioHdrFn>(
        clientBase + getStudioHdr->Rva);
    const HookBackendResult resolveResult = HookBackends::Install(
            clientActive,
            *resolvePose,
            reinterpret_cast<void*>(&HookResolveVirtualModelPose),
            &gResolveVirtualModelPoseHook);
    if (resolveResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "resolve-pose-backend-%u",
            static_cast<unsigned>(resolveResult));
        return false;
    }
    gOriginalResolveVirtualModelPose =
        reinterpret_cast<ResolveVirtualModelPoseFn>(
            gResolveVirtualModelPoseHook.Original);
    const HookBackendResult buildResult = HookBackends::Install(
            clientActive,
            *buildTransformations,
            reinterpret_cast<void*>(&HookBuildTransformations),
            &gBuildTransformationsHook);
    if (buildResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "build-transformations-backend-%u",
            static_cast<unsigned>(buildResult));
        return false;
    }
    gOriginalBuildTransformations =
        reinterpret_cast<BuildTransformationsFn>(
            gBuildTransformationsHook.Original);
    const HookBackendResult setupResult = HookBackends::Install(
            clientActive,
            *setupBones,
            reinterpret_cast<void*>(&HookSetupBones),
            &gSetupBonesHook);
    if (setupResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "setup-bones-backend-%u",
            static_cast<unsigned>(setupResult));
        return false;
    }
    gOriginalSetupBones =
        reinterpret_cast<SetupBonesFn>(gSetupBonesHook.Original);
    const HookBackendResult modelRenderResult = HookBackends::Install(
            engineActive,
            *modelRenderDrawModel,
            reinterpret_cast<void*>(&HookModelRenderDrawModel),
            &gModelRenderDrawModelHook);
    if (modelRenderResult != HookBackendResult::Installed) {
        // A backend refusal is fail-closed but opaque on its own, so the
        // declaration it refused travels with the code.
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "model-render-draw-model-backend-%u"
            " base=%08x size=%08x rva=%08x kind=%u backend=%u bytes=%u",
            static_cast<unsigned>(modelRenderResult),
            static_cast<unsigned>(engineActive.ImageBase),
            static_cast<unsigned>(engineActive.ImageSize),
            static_cast<unsigned>(modelRenderDrawModel->Rva),
            static_cast<unsigned>(modelRenderDrawModel->Kind),
            static_cast<unsigned>(modelRenderDrawModel->Backend),
            static_cast<unsigned>(modelRenderDrawModel->ExpectedByteCount));
        return false;
    }
    gOriginalModelRenderDrawModel =
        reinterpret_cast<ModelRenderDrawModelFn>(
            gModelRenderDrawModelHook.Original);
    const HookBackendResult shadowResult = HookBackends::Install(
            engineActive,
            *modelRenderDrawModelShadow,
            reinterpret_cast<void*>(&HookModelRenderDrawModelShadow),
            &gModelRenderDrawModelShadowHook);
    if (shadowResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "model-render-draw-model-shadow-backend-%u"
            " rva=%08x kind=%u backend=%u bytes=%u",
            static_cast<unsigned>(shadowResult),
            static_cast<unsigned>(modelRenderDrawModelShadow->Rva),
            static_cast<unsigned>(modelRenderDrawModelShadow->Kind),
            static_cast<unsigned>(modelRenderDrawModelShadow->Backend),
            static_cast<unsigned>(
                modelRenderDrawModelShadow->ExpectedByteCount));
        return false;
    }
    gOriginalModelRenderDrawModelShadow =
        reinterpret_cast<ModelRenderDrawModelShadowFn>(
            gModelRenderDrawModelShadowHook.Original);

    if (contributionEnabled) {
        // The blend resolver goes in first: it only stashes, and the sequence
        // frame that reads the stash must never be live while the resolver
        // feeding it is not, or a contribution would report an unwitnessed
        // blend that in fact happened.
        const HookBackendResult blendResult = HookBackends::Install(
                clientActive,
                *resolveBlendAxisWeight,
                reinterpret_cast<void*>(&HookResolveBlendAxisWeight),
                &gResolveBlendAxisWeightHook);
        if (blendResult != HookBackendResult::Installed) {
            std::snprintf(
                gHookInstallError,
                sizeof(gHookInstallError),
                "resolve-blend-axis-weight-backend-%u rva=%08x bytes=%u",
                static_cast<unsigned>(blendResult),
                static_cast<unsigned>(resolveBlendAxisWeight->Rva),
                static_cast<unsigned>(
                    resolveBlendAxisWeight->ExpectedByteCount));
            return false;
        }
        gOriginalResolveBlendAxisWeight =
            reinterpret_cast<ResolveBlendAxisWeightFn>(
                gResolveBlendAxisWeightHook.Original);
        const HookBackendResult sequenceResult = HookBackends::Install(
                clientActive,
                *evaluateSequencePose,
                reinterpret_cast<void*>(&HookEvaluateSequencePose),
                &gEvaluateSequencePoseHook);
        if (sequenceResult != HookBackendResult::Installed) {
            std::snprintf(
                gHookInstallError,
                sizeof(gHookInstallError),
                "evaluate-sequence-pose-backend-%u rva=%08x bytes=%u",
                static_cast<unsigned>(sequenceResult),
                static_cast<unsigned>(evaluateSequencePose->Rva),
                static_cast<unsigned>(
                    evaluateSequencePose->ExpectedByteCount));
            return false;
        }
        gOriginalEvaluateSequencePose =
            reinterpret_cast<EvaluateSequencePoseFn>(
                gEvaluateSequencePoseHook.Original);
        // The two channel decoders are stash producers like the blend resolver,
        // so they go in before the cell frame that reads them back: a cell must
        // never be live while the decoders feeding its accumulator are not, or
        // it would report an unwitnessed decode that in fact happened.
        const HookBackendResult quaternionResult = HookBackends::Install(
                clientActive,
                *decodeBoneQuaternion,
                reinterpret_cast<void*>(&HookDecodeBoneQuaternion),
                &gDecodeBoneQuaternionHook);
        if (quaternionResult != HookBackendResult::Installed) {
            std::snprintf(
                gHookInstallError,
                sizeof(gHookInstallError),
                "decode-bone-quaternion-backend-%u rva=%08x bytes=%u",
                static_cast<unsigned>(quaternionResult),
                static_cast<unsigned>(decodeBoneQuaternion->Rva),
                static_cast<unsigned>(
                    decodeBoneQuaternion->ExpectedByteCount));
            return false;
        }
        gOriginalDecodeBoneQuaternion =
            reinterpret_cast<DecodeBoneChannelFn>(
                gDecodeBoneQuaternionHook.Original);
        const HookBackendResult positionResult = HookBackends::Install(
                clientActive,
                *decodeBonePosition,
                reinterpret_cast<void*>(&HookDecodeBonePosition),
                &gDecodeBonePositionHook);
        if (positionResult != HookBackendResult::Installed) {
            std::snprintf(
                gHookInstallError,
                sizeof(gHookInstallError),
                "decode-bone-position-backend-%u rva=%08x bytes=%u",
                static_cast<unsigned>(positionResult),
                static_cast<unsigned>(decodeBonePosition->Rva),
                static_cast<unsigned>(decodeBonePosition->ExpectedByteCount));
            return false;
        }
        gOriginalDecodeBonePosition =
            reinterpret_cast<DecodeBoneChannelFn>(
                gDecodeBonePositionHook.Original);
        // Last of the five, so a cell can never be recorded before the scope
        // that owns it can be opened.
        const HookBackendResult decodeResult = HookBackends::Install(
                clientActive,
                *decodeSelectedBones,
                reinterpret_cast<void*>(&HookDecodeSelectedBones),
                &gDecodeSelectedBonesHook);
        if (decodeResult != HookBackendResult::Installed) {
            std::snprintf(
                gHookInstallError,
                sizeof(gHookInstallError),
                "decode-selected-bones-backend-%u rva=%08x bytes=%u",
                static_cast<unsigned>(decodeResult),
                static_cast<unsigned>(decodeSelectedBones->Rva),
                static_cast<unsigned>(
                    decodeSelectedBones->ExpectedByteCount));
            return false;
        }
        gOriginalDecodeSelectedBones =
            reinterpret_cast<DecodeSelectedBonesFn>(
                gDecodeSelectedBonesHook.Original);
    }

    if (!lifetimeEnabled) {
        return true;
    }
    // Last, so a lifetime hook can never be live while the census it feeds is
    // not: an actor construction recorded before the evaluation hooks exist
    // would open an interval nothing could close.
    const HookBackendResult constructResult = HookBackends::Install(
            clientActive,
            *baseEntityConstruct,
            reinterpret_cast<void*>(&HookBaseEntityConstruct),
            &gBaseEntityConstructHook);
    if (constructResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "base-entity-construct-backend-%u rva=%08x bytes=%u",
            static_cast<unsigned>(constructResult),
            static_cast<unsigned>(baseEntityConstruct->Rva),
            static_cast<unsigned>(baseEntityConstruct->ExpectedByteCount));
        return false;
    }
    gOriginalBaseEntityConstruct =
        reinterpret_cast<BaseEntityConstructFn>(
            gBaseEntityConstructHook.Original);
    const HookBackendResult destructResult = HookBackends::Install(
            clientActive,
            *baseEntityDestruct,
            reinterpret_cast<void*>(&HookBaseEntityDestruct),
            &gBaseEntityDestructHook);
    if (destructResult != HookBackendResult::Installed) {
        std::snprintf(
            gHookInstallError,
            sizeof(gHookInstallError),
            "base-entity-destruct-backend-%u rva=%08x bytes=%u",
            static_cast<unsigned>(destructResult),
            static_cast<unsigned>(baseEntityDestruct->Rva),
            static_cast<unsigned>(baseEntityDestruct->ExpectedByteCount));
        return false;
    }
    gOriginalBaseEntityDestruct =
        reinterpret_cast<BaseEntityDestructFn>(
            gBaseEntityDestructHook.Original);
    return true;
}

void RemoveHooks() {
    using elysium::capture::HookBackends;

    InterlockedExchange(&gCapturing, 0);
    HookBackends::Disable(&gBaseEntityDestructHook);
    HookBackends::Disable(&gBaseEntityConstructHook);
    HookBackends::Disable(&gDecodeSelectedBonesHook);
    HookBackends::Disable(&gDecodeBonePositionHook);
    HookBackends::Disable(&gDecodeBoneQuaternionHook);
    HookBackends::Disable(&gEvaluateSequencePoseHook);
    HookBackends::Disable(&gResolveBlendAxisWeightHook);
    HookBackends::Disable(&gModelRenderDrawModelShadowHook);
    HookBackends::Disable(&gModelRenderDrawModelHook);
    HookBackends::Disable(&gSetupBonesHook);
    HookBackends::Disable(&gBuildTransformationsHook);
    HookBackends::Disable(&gResolveVirtualModelPoseHook);
    HookBackends::Disable(&gDrawModelHook);
    while (InterlockedCompareExchange(&gActiveHooks, 0, 0)) {
        Sleep(1);
    }
    HookBackends::Release(&gBaseEntityDestructHook);
    HookBackends::Release(&gBaseEntityConstructHook);
    HookBackends::Release(&gDecodeSelectedBonesHook);
    HookBackends::Release(&gDecodeBonePositionHook);
    HookBackends::Release(&gDecodeBoneQuaternionHook);
    HookBackends::Release(&gEvaluateSequencePoseHook);
    HookBackends::Release(&gResolveBlendAxisWeightHook);
    HookBackends::Release(&gModelRenderDrawModelShadowHook);
    HookBackends::Release(&gModelRenderDrawModelHook);
    HookBackends::Release(&gSetupBonesHook);
    HookBackends::Release(&gBuildTransformationsHook);
    HookBackends::Release(&gResolveVirtualModelPoseHook);
    HookBackends::Release(&gDrawModelHook);
    gOriginalBaseEntityDestruct = nullptr;
    gOriginalBaseEntityConstruct = nullptr;
    gOriginalDecodeSelectedBones = nullptr;
    gOriginalDecodeBonePosition = nullptr;
    gOriginalDecodeBoneQuaternion = nullptr;
    gOriginalEvaluateSequencePose = nullptr;
    gOriginalResolveBlendAxisWeight = nullptr;
    gOriginalModelRenderDrawModelShadow = nullptr;
    gOriginalModelRenderDrawModel = nullptr;
    gOriginalSetupBones = nullptr;
    gOriginalBuildTransformations = nullptr;
    gOriginalResolveVirtualModelPose = nullptr;
    gOriginalDrawModel = nullptr;
}

bool ReadProfileDword(
    const wchar_t* iniPath,
    const wchar_t* key,
    DWORD* value) {
    wchar_t text[32]{};
    GetPrivateProfileStringW(
        L"capture",
        key,
        L"",
        text,
        ARRAYSIZE(text),
        iniPath);
    if (!text[0]) {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 0);
    if (end == text || *end != L'\0') {
        return false;
    }
    *value = static_cast<DWORD>(parsed);
    return true;
}

int HexDigit(wchar_t value) {
    if (value >= L'0' && value <= L'9') {
        return value - L'0';
    }
    if (value >= L'a' && value <= L'f') {
        return value - L'a' + 10;
    }
    if (value >= L'A' && value <= L'F') {
        return value - L'A' + 10;
    }
    return -1;
}

bool ReadExpectedBytes(
    const wchar_t* iniPath,
    const wchar_t* key,
    ConfiguredSignature* signature) {
    wchar_t text[kMaximumSignatureBytes * 2 + 1]{};
    GetPrivateProfileStringW(
        L"capture",
        key,
        L"",
        text,
        ARRAYSIZE(text),
        iniPath);
    const std::size_t digits = std::wcslen(text);
    if (!digits || digits % 2 ||
        digits > kMaximumSignatureBytes * 2) {
        return false;
    }
    signature->count = static_cast<DWORD>(digits / 2);
    for (DWORD index = 0; index < signature->count; ++index) {
        const int high = HexDigit(text[index * 2]);
        const int low = HexDigit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        signature->bytes[index] =
            static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}

bool ReadConfiguration(
    wchar_t* iniPath, wchar_t* outputPath, wchar_t* animationOutputPath,
    wchar_t* censusOutputPath, wchar_t* actorOutputPath,
    wchar_t* contributionOutputPath, wchar_t* studioHash,
    wchar_t* clientHash) {
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
        L"capture", L"census_output", L"", censusOutputPath,
        MAX_PATH * 4, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"actor_output", L"", actorOutputPath,
        MAX_PATH * 4, iniPath);
    GetPrivateProfileStringW(
        L"capture", L"contribution_output", L"", contributionOutputPath,
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
    const bool studioProfile =
        ReadProfileDword(
            iniPath,
            L"studio_object_rva",
            &gStudioObjectRva) &&
        ReadProfileDword(
            iniPath,
            L"studio_vtable_rva",
            &gStudioVtableRva) &&
        ReadProfileDword(
            iniPath,
            L"draw_model_rva",
            &gDrawModelRva) &&
        ReadProfileDword(
            iniPath,
            L"draw_model_slot",
            &gDrawModelSlotIndex);
    const bool clientProfile =
        !animationOutputPath[0] ||
        (ReadProfileDword(
             iniPath,
             L"resolve_virtual_model_pose_rva",
             &gResolveVirtualModelPoseRva) &&
         ReadExpectedBytes(
             iniPath,
             L"resolve_virtual_model_pose_expected",
             &gResolveExpected) &&
         ReadProfileDword(
             iniPath,
             L"build_transformations_rva",
             &gBuildTransformationsRva) &&
         ReadExpectedBytes(
             iniPath,
             L"build_transformations_expected",
             &gBuildExpected) &&
         ReadProfileDword(
             iniPath,
             L"get_studio_hdr_rva",
             &gGetStudioHdrRva) &&
         ReadProfileDword(
             iniPath,
             L"setup_bones_rva",
             &gSetupBonesRva) &&
         ReadExpectedBytes(
             iniPath,
             L"setup_bones_expected",
             &gSetupBonesExpected) &&
         ReadProfileDword(
             iniPath,
             L"model_render_draw_model_rva",
             &gModelRenderDrawModelRva) &&
         ReadExpectedBytes(
             iniPath,
             L"model_render_draw_model_expected",
             &gModelRenderDrawModelExpected) &&
         ReadProfileDword(
             iniPath,
             L"model_render_draw_model_shadow_rva",
             &gModelRenderDrawModelShadowRva) &&
         ReadExpectedBytes(
             iniPath,
             L"model_render_draw_model_shadow_expected",
             &gModelRenderDrawModelShadowExpected));
    // The lifetime pair is only required when an actor stream was asked for, so
    // a recipe that captures poses without actors stays configurable.
    const bool lifetimeProfile =
        !actorOutputPath[0] ||
        (ReadProfileDword(
             iniPath,
             L"base_entity_construct_rva",
             &gBaseEntityConstructRva) &&
         ReadExpectedBytes(
             iniPath,
             L"base_entity_construct_expected",
             &gBaseEntityConstructExpected) &&
         ReadProfileDword(
             iniPath,
             L"base_entity_destruct_rva",
             &gBaseEntityDestructRva) &&
         ReadExpectedBytes(
             iniPath,
             L"base_entity_destruct_expected",
             &gBaseEntityDestructExpected));
    // Likewise the five contribution frames: a recipe that wants poses without
    // source attribution stays configurable by leaving the stream out.
    const bool contributionProfile =
        !contributionOutputPath[0] ||
        (ReadProfileDword(
             iniPath,
             L"evaluate_sequence_pose_rva",
             &gEvaluateSequencePoseRva) &&
         ReadExpectedBytes(
             iniPath,
             L"evaluate_sequence_pose_expected",
             &gEvaluateSequencePoseExpected) &&
         ReadProfileDword(
             iniPath,
             L"decode_selected_bones_rva",
             &gDecodeSelectedBonesRva) &&
         ReadExpectedBytes(
             iniPath,
             L"decode_selected_bones_expected",
             &gDecodeSelectedBonesExpected) &&
         ReadProfileDword(
             iniPath,
             L"resolve_blend_axis_weight_rva",
             &gResolveBlendAxisWeightRva) &&
         ReadExpectedBytes(
             iniPath,
             L"resolve_blend_axis_weight_expected",
             &gResolveBlendAxisWeightExpected) &&
         ReadProfileDword(
             iniPath,
             L"decode_bone_quaternion_rva",
             &gDecodeBoneQuaternionRva) &&
         ReadExpectedBytes(
             iniPath,
             L"decode_bone_quaternion_expected",
             &gDecodeBoneQuaternionExpected) &&
         ReadProfileDword(
             iniPath,
             L"decode_bone_position_rva",
             &gDecodeBonePositionRva) &&
         ReadExpectedBytes(
             iniPath,
             L"decode_bone_position_expected",
             &gDecodeBonePositionExpected));
    return outputPath[0] && gReadyPath[0] && gStopPath[0] &&
        gDonePath[0] && studioProfile && clientProfile && lifetimeProfile &&
        contributionProfile;
}

DWORD WINAPI CaptureWorker(void*) {
    wchar_t iniPath[MAX_PATH * 4]{};
    wchar_t outputPath[MAX_PATH * 4]{};
    wchar_t animationOutputPath[MAX_PATH * 4]{};
    wchar_t censusOutputPath[MAX_PATH * 4]{};
    wchar_t actorOutputPath[MAX_PATH * 4]{};
    wchar_t contributionOutputPath[MAX_PATH * 4]{};
    wchar_t studioHash[80]{};
    wchar_t clientHash[80]{};
    if (!ReadConfiguration(
            iniPath, outputPath, animationOutputPath, censusOutputPath,
            actorOutputPath, contributionOutputPath, studioHash, clientHash)) {
        return 1;
    }

    HMODULE studioRender = nullptr;
    HMODULE client = nullptr;
    HMODULE engine = nullptr;
    for (int attempt = 0;
         attempt < 1200 && (!studioRender || !client || !engine);
         ++attempt) {
        studioRender = GetModuleHandleW(L"StudioRender.dll");
        client = GetModuleHandleW(L"client.dll");
        engine = GetModuleHandleW(L"engine.dll");
        if (!studioRender || !client || !engine) {
            Sleep(25);
        }
    }
    if (!studioRender) {
        WriteMarker(gDonePath, "error=StudioRender.dll not loaded\n");
        return 2;
    }
    if (animationOutputPath[0] && !client) {
        WriteMarker(gDonePath, "error=client.dll not loaded\n");
        return 2;
    }
    if (animationOutputPath[0] && !engine) {
        WriteMarker(gDonePath, "error=engine.dll not loaded\n");
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
    if (censusOutputPath[0]) {
        gCensusHeap = HeapCreate(0, 0, 0);
        gCensusOutput = CreateFileW(
            censusOutputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    // The actor census is emitted from the skeletal evaluation path, so it is
    // only populated when the animation stream is enabled.
    if (actorOutputPath[0] && animationOutputPath[0]) {
        gActorOutput = CreateFileW(
            actorOutputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    // Contributions nest inside the skeletal evaluation, so they share its
    // precondition.
    if (contributionOutputPath[0] && animationOutputPath[0]) {
        gContributionOutput = CreateFileW(
            contributionOutputPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    if (!gWake || gOutput == INVALID_HANDLE_VALUE ||
        (animationOutputPath[0] &&
         gAnimationOutput == INVALID_HANDLE_VALUE) ||
        (censusOutputPath[0] &&
         (gCensusOutput == INVALID_HANDLE_VALUE || !gCensusHeap)) ||
        (actorOutputPath[0] && animationOutputPath[0] &&
         gActorOutput == INVALID_HANDLE_VALUE) ||
        (contributionOutputPath[0] && animationOutputPath[0] &&
         gContributionOutput == INVALID_HANDLE_VALUE)) {
        WriteMarker(gDonePath, "error=cannot create capture output\n");
        return 3;
    }

    FileHeader fileHeader{};
    std::memcpy(fileHeader.magic, "ELPOSE4", 7);
    fileHeader.version = 4;
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
    fileHeader.studioObject = fileHeader.studioRenderBase + gStudioObjectRva;
    fileHeader.studioVtable = fileHeader.studioRenderBase + gStudioVtableRva;
    fileHeader.drawModelRva = gDrawModelRva;
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
        std::memcpy(animationHeader.magic, "ELANIM4", 7);
        animationHeader.version = 4;
        animationHeader.headerBytes = sizeof(animationHeader);
        animationHeader.qpcFrequency = frequency.QuadPart;
        animationHeader.startQpc = gStartQpc.QuadPart;
        animationHeader.pid = GetCurrentProcessId();
        animationHeader.clientBase = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(client));
        animationHeader.resolveVirtualModelPoseRva =
            gResolveVirtualModelPoseRva;
        animationHeader.buildTransformationsRva =
            gBuildTransformationsRva;
        animationHeader.targetChecksum = gTargetChecksum;
        animationHeader.setupBonesRva = gSetupBonesRva;
        animationHeader.modelRenderDrawModelRva = gModelRenderDrawModelRva;
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

    if (gCensusOutput != INVALID_HANDLE_VALUE) {
        CensusFileHeader censusHeader{};
        std::memcpy(censusHeader.magic, "ELMDL1", 6);
        censusHeader.version = 1;
        censusHeader.headerBytes = sizeof(censusHeader);
        censusHeader.qpcFrequency = frequency.QuadPart;
        censusHeader.startQpc = gStartQpc.QuadPart;
        censusHeader.pid = GetCurrentProcessId();
        censusHeader.clientBase = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(client));
        censusHeader.studioRenderBase = fileHeader.studioRenderBase;
        censusHeader.imageCap = kCensusImageCap;
        censusHeader.slotCount = kCensusSlots;
        if (!WriteAll(
                gCensusOutput, &censusHeader, sizeof(censusHeader))) {
            WriteMarker(
                gDonePath, "error=cannot write census capture header\n");
            return 4;
        }
    }

    if (gActorOutput != INVALID_HANDLE_VALUE) {
        ActorFileHeader actorHeader{};
        std::memcpy(actorHeader.magic, "ELACT2", 6);
        actorHeader.version = 2;
        actorHeader.headerBytes = sizeof(actorHeader);
        actorHeader.qpcFrequency = frequency.QuadPart;
        actorHeader.startQpc = gStartQpc.QuadPart;
        actorHeader.pid = GetCurrentProcessId();
        actorHeader.clientBase = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(client));
        actorHeader.slotCount = kActorSlots;
        actorHeader.setupBonesRva = gSetupBonesRva;
        if (!WriteAll(gActorOutput, &actorHeader, sizeof(actorHeader))) {
            WriteMarker(
                gDonePath, "error=cannot write actor capture header\n");
            return 4;
        }
    }

    if (gContributionOutput != INVALID_HANDLE_VALUE) {
        ContributionFileHeader contributionHeader{};
        std::memcpy(contributionHeader.magic, "ELCON2", 6);
        contributionHeader.version = 2;
        contributionHeader.headerBytes = sizeof(contributionHeader);
        contributionHeader.qpcFrequency = frequency.QuadPart;
        contributionHeader.startQpc = gStartQpc.QuadPart;
        contributionHeader.pid = GetCurrentProcessId();
        contributionHeader.clientBase = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(client));
        contributionHeader.evaluateSequencePoseRva = gEvaluateSequencePoseRva;
        contributionHeader.decodeSelectedBonesRva = gDecodeSelectedBonesRva;
        contributionHeader.resolveBlendAxisWeightRva =
            gResolveBlendAxisWeightRva;
        contributionHeader.decodeBoneQuaternionRva = gDecodeBoneQuaternionRva;
        contributionHeader.decodeBonePositionRva = gDecodeBonePositionRva;
        wcstombs_s(
            &converted, contributionHeader.clientSha256,
            sizeof(contributionHeader.clientSha256), clientHash, _TRUNCATE);
        if (!WriteAll(
                gContributionOutput, &contributionHeader,
                sizeof(contributionHeader))) {
            WriteMarker(
                gDonePath, "error=cannot write contribution capture header\n");
            return 4;
        }
    }

    if (!InstallHooks(studioRender, client, engine)) {
        RemoveHooks();
        char error[192]{};
        std::snprintf(
            error,
            sizeof(error),
            "error=capture hook validation failed: %s\n",
            gHookInstallError);
        WriteMarker(gDonePath, error);
        return 5;
    }
    InterlockedExchange(&gCapturing, 1);
    char ready[192]{};
    std::snprintf(
        ready, sizeof(ready), "ready=1\nformat=ELPOSE4%s%s%s%s\n",
        gAnimationOutput == INVALID_HANDLE_VALUE ? "" : "+ELANIM4",
        gCensusOutput == INVALID_HANDLE_VALUE ? "" : "+ELMDL1",
        gActorOutput == INVALID_HANDLE_VALUE ? "" : "+ELACT2",
        gContributionOutput == INVALID_HANDLE_VALUE ? "" : "+ELCON2");
    WriteMarker(gReadyPath, ready);

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

    // Before the hooks come out, while the game is still running and every
    // header the run recorded is still where it was recorded.
    SweepResidentHeaders();
    SweepResidentActors();
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
    if (gCensusOutput != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(gCensusOutput);
        CloseHandle(gCensusOutput);
        gCensusOutput = INVALID_HANDLE_VALUE;
    }
    if (gActorOutput != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(gActorOutput);
        CloseHandle(gActorOutput);
        gActorOutput = INVALID_HANDLE_VALUE;
    }
    if (gContributionOutput != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(gContributionOutput);
        CloseHandle(gContributionOutput);
        gContributionOutput = INVALID_HANDLE_VALUE;
    }

    char done[2048]{};
    std::snprintf(
        done, sizeof(done),
        "complete=1\nqueued=%ld\nwritten=%ld\ndropped=%ld\n"
        "queue_peak=%ld\nskipped=%ld\nfiltered=%ld\nbytes_written=%lld\n"
        "generations=%ld\nunbracketed=%ld\nbracket_overflow=%ld\n"
        "census_records=%ld\ncensus_images=%ld\ncensus_replacements=%ld\n"
        "census_resident=%ld\ncensus_vanished=%ld\ncensus_overflow=%ld\n"
        "census_faults=%ld\ncensus_capped=%ld\ncensus_bytes=%lld\n"
        "actor_records=%ld\nactor_identity_changes=%ld\nactor_resident=%ld\n"
        "actor_vanished=%ld\nactor_overflow=%ld\nactor_faults=%ld\n"
        "actor_constructions=%ld\nactor_destructions=%ld\n"
        "actor_bytes=%lld\n"
        "contribution_sequences=%ld\ncontribution_animations=%ld\n"
        "contribution_scopes=%ld\ncontribution_faults=%ld\n"
        "contribution_overflow=%ld\ncontribution_unscoped=%ld\n"
        "contribution_bytes=%lld\n"
        "channel_unwitnessed=%ld\nchannel_stride_faults=%ld\n"
        "channel_nested=%ld\n",
        gQueued, gWritten, gDropped, gQueuePeak, gSkipped, gFiltered,
        static_cast<long long>(gBytesWritten), gGeneration, gUnbracketed,
        gBracketOverflow, gCensusRecords, gCensusImages, gCensusReplacements,
        gCensusResident, gCensusVanished, gCensusOverflow, gCensusFaults,
        gCensusCapped, static_cast<long long>(gCensusBytes), gActorRecords,
        gActorIdentityChanges, gActorResident, gActorVanished, gActorOverflow,
        gActorFaults, gActorConstructions, gActorDestructions,
        static_cast<long long>(gActorBytes), gContributionSequences,
        gContributionAnimations, gContributionScope, gContributionFaults,
        gContributionOverflow, gContributionUnscoped,
        static_cast<long long>(gContributionBytes), gChannelUnwitnessed,
        gChannelStrideFaults, gChannelNested);
    WriteMarker(gDonePath, done);
    CloseHandle(gWake);
    if (gCensusHeap) {
        HeapDestroy(gCensusHeap);
        gCensusHeap = nullptr;
    }
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
