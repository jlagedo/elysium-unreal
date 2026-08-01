#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

// Which stream a queued record belongs to. The writer routes on this and frees
// to the heap it names, so census payloads never touch the game's heap.
enum StreamIndex : std::uint32_t {
    kStreamPose = 0,
    kStreamAnimation = 1,
    kStreamCensus = 2,
};

// Why an observation was emitted. Reuse is a pointer that served one checksum
// and now serves another; residency is what the sweep at capture stop reports
// in place of the unload event no hooked target can currently see.
enum ObservationReason : std::uint32_t {
    kObservationFirst = 1,
    kObservationReplacement = 2,
    kObservationResidentAtStop = 3,
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
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 128, "capture file header changed");
static_assert(sizeof(PoseRecordHeader) == 144, "pose record header changed");
static_assert(
    sizeof(BracketRecordHeader) == 88,
    "pose-build bracket record header changed");
static_assert(
    sizeof(AnimationFileHeader) == 128,
    "animation capture file header changed");
static_assert(
    sizeof(AnimationRecordHeader) == 76,
    "animation record header changed");
static_assert(
    sizeof(CensusFileHeader) == 128, "census capture file header changed");
static_assert(
    sizeof(ModelObservationHeader) == 204,
    "model observation record header changed");
static_assert(
    sizeof(ModelImageHeader) == 52, "model image record header changed");

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

HMODULE gSelf = nullptr;
DrawModelFn gOriginalDrawModel = nullptr;
ResolveVirtualModelPoseFn gOriginalResolveVirtualModelPose = nullptr;
BuildTransformationsFn gOriginalBuildTransformations = nullptr;
GetStudioHdrFn gGetStudioHdr = nullptr;
SetupBonesFn gOriginalSetupBones = nullptr;
ModelRenderDrawModelFn gOriginalModelRenderDrawModel = nullptr;
ModelRenderDrawModelShadowFn gOriginalModelRenderDrawModelShadow = nullptr;
elysium::capture::HookHandle gDrawModelHook;
elysium::capture::HookHandle gResolveVirtualModelPoseHook;
elysium::capture::HookHandle gBuildTransformationsHook;
elysium::capture::HookHandle gSetupBonesHook;
elysium::capture::HookHandle gModelRenderDrawModelHook;
elysium::capture::HookHandle gModelRenderDrawModelShadowHook;
__declspec(thread) ThreadPoseState gThread{};
HANDLE gOutput = INVALID_HANDLE_VALUE;
HANDLE gAnimationOutput = INVALID_HANDLE_VALUE;
HANDLE gCensusOutput = INVALID_HANDLE_VALUE;
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
// Headers keyed by address, images keyed by checksum.
CensusSlot gCensusHeaders[kCensusSlots]{};
CensusSlot gCensusImageSlots[kCensusImageSlots]{};
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
ConfiguredSignature gResolveExpected{};
ConfiguredSignature gBuildExpected{};
ConfiguredSignature gSetupBonesExpected{};
ConfiguredSignature gModelRenderDrawModelExpected{};
ConfiguredSignature gModelRenderDrawModelShadowExpected{};
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
    bool animationEnabled) {
    const bool drawMatches = drawModel.Rva == gDrawModelRva &&
        drawModel.ObjectRva == gStudioObjectRva &&
        drawModel.ExpectedVtableRva == gStudioVtableRva &&
        drawModel.VtableSlot == gDrawModelSlotIndex;
    if (!drawMatches || !animationEnabled) {
        return drawMatches;
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
    if (drawModel == nullptr || resolvePose == nullptr ||
        buildTransformations == nullptr || getStudioHdr == nullptr ||
        setupBones == nullptr || modelRenderDrawModel == nullptr ||
        modelRenderDrawModelShadow == nullptr ||
        !MatchesConfiguredProfile(
            *drawModel,
            *resolvePose,
            *buildTransformations,
            *getStudioHdr,
            *setupBones,
            *modelRenderDrawModel,
            *modelRenderDrawModelShadow,
            gAnimationOutput != INVALID_HANDLE_VALUE)) {
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
    return true;
}

void RemoveHooks() {
    using elysium::capture::HookBackends;

    InterlockedExchange(&gCapturing, 0);
    HookBackends::Disable(&gModelRenderDrawModelShadowHook);
    HookBackends::Disable(&gModelRenderDrawModelHook);
    HookBackends::Disable(&gSetupBonesHook);
    HookBackends::Disable(&gBuildTransformationsHook);
    HookBackends::Disable(&gResolveVirtualModelPoseHook);
    HookBackends::Disable(&gDrawModelHook);
    while (InterlockedCompareExchange(&gActiveHooks, 0, 0)) {
        Sleep(1);
    }
    HookBackends::Release(&gModelRenderDrawModelShadowHook);
    HookBackends::Release(&gModelRenderDrawModelHook);
    HookBackends::Release(&gSetupBonesHook);
    HookBackends::Release(&gBuildTransformationsHook);
    HookBackends::Release(&gResolveVirtualModelPoseHook);
    HookBackends::Release(&gDrawModelHook);
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
    wchar_t* censusOutputPath, wchar_t* studioHash, wchar_t* clientHash) {
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
    return outputPath[0] && gReadyPath[0] && gStopPath[0] &&
        gDonePath[0] && studioProfile && clientProfile;
}

DWORD WINAPI CaptureWorker(void*) {
    wchar_t iniPath[MAX_PATH * 4]{};
    wchar_t outputPath[MAX_PATH * 4]{};
    wchar_t animationOutputPath[MAX_PATH * 4]{};
    wchar_t censusOutputPath[MAX_PATH * 4]{};
    wchar_t studioHash[80]{};
    wchar_t clientHash[80]{};
    if (!ReadConfiguration(
            iniPath, outputPath, animationOutputPath, censusOutputPath,
            studioHash, clientHash)) {
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
    if (!gWake || gOutput == INVALID_HANDLE_VALUE ||
        (animationOutputPath[0] &&
         gAnimationOutput == INVALID_HANDLE_VALUE) ||
        (censusOutputPath[0] &&
         (gCensusOutput == INVALID_HANDLE_VALUE || !gCensusHeap))) {
        WriteMarker(gDonePath, "error=cannot create capture output\n");
        return 3;
    }

    FileHeader fileHeader{};
    std::memcpy(fileHeader.magic, "ELPOSE3", 7);
    fileHeader.version = 3;
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
        std::memcpy(animationHeader.magic, "ELANIM3", 7);
        animationHeader.version = 3;
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
    char ready[160]{};
    std::snprintf(
        ready, sizeof(ready), "ready=1\nformat=ELPOSE3%s%s\n",
        gAnimationOutput == INVALID_HANDLE_VALUE ? "" : "+ELANIM3",
        gCensusOutput == INVALID_HANDLE_VALUE ? "" : "+ELMDL1");
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

    char done[1024]{};
    std::snprintf(
        done, sizeof(done),
        "complete=1\nqueued=%ld\nwritten=%ld\ndropped=%ld\n"
        "queue_peak=%ld\nskipped=%ld\nfiltered=%ld\nbytes_written=%lld\n"
        "generations=%ld\nunbracketed=%ld\nbracket_overflow=%ld\n"
        "census_records=%ld\ncensus_images=%ld\ncensus_replacements=%ld\n"
        "census_resident=%ld\ncensus_vanished=%ld\ncensus_overflow=%ld\n"
        "census_faults=%ld\ncensus_capped=%ld\ncensus_bytes=%lld\n",
        gQueued, gWritten, gDropped, gQueuePeak, gSkipped, gFiltered,
        static_cast<long long>(gBytesWritten), gGeneration, gUnbracketed,
        gBracketOverflow, gCensusRecords, gCensusImages, gCensusReplacements,
        gCensusResident, gCensusVanished, gCensusOverflow, gCensusFaults,
        gCensusCapped, static_cast<long long>(gCensusBytes));
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
