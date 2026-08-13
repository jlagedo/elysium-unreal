#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEventQueue.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumVariant.h"
#include "ElysiumWeatherState.h"

// 11.9 — the four blocks a save holds (`docs/architecture/save-architecture.md` §3), as plain C++ structs we own.
// None of this is UPROPERTY-reflected and none of it should become so: the substrate is plain C++
// precisely so serialization, determinism and travel teardown stay in our hands (engine-core.md R1).
// `UElysiumSaveGame` is the only reflected part, and it carries these as an opaque byte payload.

// The payload's schema id. It is also registered as an engine custom version
// (`FCustomVersionRegistration`), so a nested engine serializer sees it on the archive; the number
// is written into the payload prologue as well, because a raw memory archive carries no custom-
// version container of its own.
struct FElysiumSaveVersion
{
	enum Type : int32
	{
		BeforeFirst   = 0,
		Initial       = 1,   // the four blocks, the field walk, the snapshot lifecycle
		Sheet         = 2,   // the character sheet as VtMB's four trait containers, base + current
		Xp            = 3,   // AddExperience's two accumulators — the residue and the lifetime total
		Journal       = 4,   // m_QuestList — the ASSIGNED_QUEST rows a quest state change writes
		Identity      = 5,   // the PC's name, and m_iCurrQuestLogArea — the quest log's hub tab
		History       = 6,   // m_iVHistoryID — the background trait chargen writes
		BodyIdentity  = 7,   // the authored M_BodyN/F_BodyN armor-slot appearance
		Weather       = 8,   // map wetness transition and env_particle ramps
		ScriptedBody  = 9,   // the cutscene body state: a scene's frozen cast, a beat's NPC claim
		Feeding       = 10,  // an in-progress feed on the player record (B6)
		EventClock    = 11,  // the event queue's backward-clock guard state
		WireIdentity  = 12,  // the authored output row a pending queue record came from
		NpcMaker      = 13,  // maker ownership and once-only child termination notification
		NpcMind       = 14,  // resumable NPC state/body intent; session capabilities remain transient
		NpcSchedule   = 15,  // the running idle schedule; its task position is deliberately not saved
		NpcSocial     = 16,  // disposition level and the independent combat-relationship table
		Activation    = 17,  // whether Source Activate already ran for this entity

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	// The oldest payload this build can read. A payload below it is rejected with a readable
	// reason rather than half-read (`docs/architecture/save-architecture.md` §2).
	//
	// `Sheet` restructured the player's trait storage from a name -> value bag into VtMB's four
	// fixed containers. `Xp` added the award accumulators beside them, `Journal` the quest rows, and
	// `Identity` the name and the quest log's hub tab, and `History` the background trait. All five
	// are additions to the player block with no upgrade branch: an older payload is refused, not
	// half-read.
	//
	// `ScriptedBody` is the same kind of break. A choreographed scene now records which of its cast
	// it immobilised and a `scripted_sequence` records which NPC it has claimed, both mid-record in
	// their leaf blocks; a payload written before that reads those bytes as the fields that followed
	// them, so it is refused rather than mis-restored.
	static constexpr int32 MinSupported = ScriptedBody;

	static const FGuid GUID;
};

// 'ELYS' — the payload's first four bytes, so a truncated or foreign file fails loudly.
inline constexpr uint32 ElysiumSaveMagic = 0x53594C45u;

// ------------------------------------------------------------------------------------------------
// The `Maps` block — one frozen map (`docs/architecture/save-architecture.md` §5)
// ------------------------------------------------------------------------------------------------

// One entity's saved state. Identity is the **def index** (R3: stable across runs, never reused
// within a map load), so it is also the save id with no extra id space.
//
// `Fields` holds only what differs from what a fresh build of the same def would produce — the
// generalisation of VtMB's zero-value-omission rule, and most of why these payloads are small.
// Restoring matches **by name**, so a field added to a base class does not invalidate a payload.
struct FElysiumEntityState
{
	int32 Index = INDEX_NONE;
	FName ClassName;                  // guards a def-array shift: a mismatch skips the record
	FString TargetName;               // Entity.SetName re-keys the name index, so it is restored explicitly

	// The live origin. Not a registered field and cannot be one — the def's `origin` key is still
	// the raw Source-space string and Construct applies every key that has a field — so it rides
	// here, beside the flags.
	FVector Origin = FVector::ZeroVector;

	bool  bDead = false;
	bool  bHidden = false;
	bool  bSpawnCalled = true;
	// Saves are taken from an active map. Default true preserves that fact for older supported
	// payloads which predate this explicit latch; a freshly captured dormant entity overwrites it.
	bool  bActivateCalled = true;
	float NextThink = 0.0f;           // absolute game seconds, rebased onto the restored clock
	float SavedNextThink = 0.0f;      // what ScriptUnhide restores

	TArray<int32> OutputTimesRemaining;
	TArray<TPair<FName, FElysiumVariant>> Fields;   // sorted by name; only the differing ones

	// A leaf's derived runtime state (a mover's phase, a sequence cursor) — the one thing that does
	// not fit the field walk, written by FElysiumEntity::SaveState (`docs/architecture/save-architecture.md` §4).
	TArray<uint8> LeafState;

	// Runtime-spawned entities (npc_maker.Spawn, CreateEntityNoSpawn) live past the def array, so
	// they serialize their synthesized def alongside their state and restore as themselves.
	bool bRuntime = false;
	FElysiumEntityDef Def;

	// In-memory only, never serialized: marks a slot of the world's post-Load omission baseline as
	// filled. A default-constructed row is "no baseline", which makes the freeze record everything.
	bool bCaptured = false;
};

// The `env_fade` screen fade — world state with the map epoch's lifetime (11.8), so it is the map's.
struct FElysiumSavedFade
{
	bool         bActive = false;
	FLinearColor Color = FLinearColor::Black;
	float        MaxAlpha = 1.0f;
	float        Duration = 0.0f;
	float        HoldTime = 0.0f;
	bool         bFadeIn = false;
	bool         bAutoReverse = false;
	double       StartTime = 0.0;
};

// A save holds the current map plus a frozen snapshot of every other map visited this run, so
// walking back into Santa Monica finds it as you left it. It is also exactly what the OpenLevel
// hard-travel lifecycle produces at every travel boundary — the same code path, which is what keeps
// travel and save from drifting apart.
struct FElysiumMapSnapshot
{
	FString MapName;
	int32   DefCount = 0;             // the def array's size when frozen; a mismatch is logged
	double  FrozenAt = 0.0;           // game seconds at freeze, for the readable dump

	TArray<FElysiumEntityState> Entities;

	// The `.HL3` equivalent: def indices that left the map with the player (the player plus
	// everything carried). On restore the build skips them, because they now live wherever the
	// player is — without it, walking back re-materialises the whole inventory.
	TArray<int32> AbsentEntities;

	// The one time-sorted queue, absolute times kept as-is (the restored clock is the saved clock).
	TArray<FElysiumIOEvent> Queue;
	uint64 QueueNextSerial = 1;
	// The queue's backward-clock guard state, saved with the queue it guards. The restored clock is
	// the saved clock, so the guard has to measure the first enqueue after a load against the time
	// the save was written at rather than against whatever the live session had reached.
	double QueueLastEnqueue = 0.0;

	FElysiumSavedFade Fade;
	FElysiumWeatherState Weather;

	bool IsValid() const { return !MapName.IsEmpty(); }
};

// ------------------------------------------------------------------------------------------------
// The `Session`, `Player` and `World` blocks
// ------------------------------------------------------------------------------------------------

// `G`, the quest map, the clock and the RNG streams — everything that outlives any one map and is
// not the player. `G` serializes as a variant map with no pickling and no interpreter involvement,
// because it already lives in C++ (`docs/architecture/save-architecture.md` §7). The CPython VM's own namespace is
// **not** saved and does not need to be: level-script names are re-imported per map at load, and
// every durable value a script writes goes to `G`, the quest map or an entity field.
struct FElysiumSessionBlock
{
	double ClockNow = 0.0;

	// Sorted by key on the way out, so two saves of the same state are byte-identical (§8).
	TArray<TPair<FString, FElysiumVariant>> Globals;
	TArray<TPair<FString, int32>>           Quests;

	int32 RngSessionSeed = 0;
	TArray<ElysiumRng::FState> Rng;
};

// The visited-map graph and where the player is standing in it.
struct FElysiumWorldBlock
{
	FString CurrentMap;
	FVector PlayerOrigin = FVector::ZeroVector; // Unreal capsule centre; preserved for save compatibility
	float   PlayerYaw = 0.0f;          // Unreal yaw (the pawn's), not the entity's Source-space angle
	bool    bHasPlacement = false;     // false = place at info_player_start (a save with no live pawn)

	TArray<FString> VisitedMaps;       // first-visit order
};

// Slot metadata, written **outside** the compressed payload so the load menu can list slots without
// inflating or loading anything — the reason `userName`, `comment` and `mapName` sit in VtMB's own
// global stream too.
struct FElysiumSaveHeaderData
{
	int32   PayloadVersion = FElysiumSaveVersion::Latest;
	FString Map;
	FString Label;
	FString ClanName;
	int32   Clan = 0;
	double  PlaytimeSeconds = 0.0;
	FDateTime Timestamp;
	FString Kind;                      // "manual" | "quick" | "auto"
};

// The whole payload: the four blocks, in the order `docs/architecture/save-architecture.md` §3 lists them.
struct FElysiumSavePayload
{
	FElysiumSessionBlock Session;
	FElysiumPlayerRecord Player;
	TMap<FString, FElysiumMapSnapshot> Maps;
	FElysiumWorldBlock   World;
};
