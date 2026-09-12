#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEventQueue.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumVariant.h"
#include "ElysiumWeatherState.h"

// The four blocks a save holds, as plain C++ structs we own.
// None of this is UPROPERTY-reflected and none of it should become so: the substrate is plain C++
// precisely so serialization, determinism and travel teardown stay in our hands.
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
		Feeding       = 10,  // an in-progress feed on the player record
		EventClock    = 11,  // the event queue's backward-clock guard state
		WireIdentity  = 12,  // the authored output row a pending queue record came from
		NpcMaker      = 13,  // maker ownership and once-only child termination notification
		NpcMind       = 14,  // resumable NPC state/body intent; session capabilities remain transient
		NpcSchedule   = 15,  // the running idle schedule; its task position is deliberately not saved
		NpcSocial     = 16,  // disposition level and the independent combat-relationship table
		Activation    = 17,  // whether Source Activate already ran for this entity
		NpcSenses     = 18,  // the NPC sensory memory: enemy, last-seen/heard/damage, LOS caches
		NpcCognition  = 19,  // the repeated-damage window and the committed enemy's eluded marker
		NpcCombat     = 20,  // the resolved NPC loadout latch and the detected-attack memory record
		Disciplines   = 21,  // the player's discipline block: selection, cast counter, active states
		Stealth       = 22,  // the player's stealth surface, as one generation, and its raw aggregate
		Law           = 23,  // the activity-channel deadlines/counts and the police-response block
		NpcWitness    = 24,  // the NPC's retained law witness block: processed counts, records, windows
		// The NPC's own discipline state: the targeted effects a cast tracked on it, their
		// expiry serials and the caster's per-record recovery deadlines. Appended to the END
		// of the NPC leaf behind its own version, so it is additive.
		NpcDisciplines = 25,

		// The weapon transaction's commit route: whether the staged swing waits on its clip's own
		// sequence event or on the `ContactEventCycle` estimate the accept already queued. Additive
		// inside the weapon leaf and read behind this version, so an older payload restores the
		// estimate route it was actually written with.
		WeaponAnimEvent = 26,

		// The bank that owns the staged swing's resolved clip. Appended to the END of the weapon
		// leaf's swing block behind its own version, so an older payload restores an empty stem.
		// Degraded only in the diagnostic: the blocked reaction is addressed by the attacking body's
		// stem and the swing's `ClipLabel`, which every supported version already writes, so a
		// pre-27 swing resolves the same reaction and loses only the bank name on its log line.
		WeaponSwingClipOwner = 27,

		// The active weapon's drawn/hidden bit — retail's `m_fEffects & EF_NODRAW` (+0x19c & 0x40).
		// An NPC saved while idle has holstered (hidden) its weapon, and a payload written before the
		// bit existed carries a drawn one, which is the default the field already has.
		WeaponHidden = 28,

		// The computer terminal's mail state: `m_EmailFlags[128]` on the terminal entity, and the
		// player's `m_GlobalEmailFlags` records behind it. The player slot that now carries those
		// records held an unwritten `TArray<FString>` placeholder before, so a pre-29 payload reads
		// that placeholder and discards it rather than mis-parsing the fields after it.
		TerminalEmail = 29,

		// CAI_Memory's per-NPC observed-actor records. This is an appended NPC-leaf block; old
		// payloads are intentionally refused by the current disposable-save policy rather than
		// replayed through a shifted leaf.
		NpcEnemyMemory = 30,
		// R6 appends the live sight/hearing records (including damage range override and unknown
		// attention state) inside the NPC senses leaf. Saves are disposable, so reject old layout.
		NpcSensesDetail = 31,
		NpcSenseTiming = 32, // saved delayed hearing, concealment seam, retained sound reduction
		NpcScheduleHost = 33, // TaskFail state and four think clocks
		DisciplineFlags = 34, // HitInfo cleanup masks, common misc word and ordered comfort targets
		StealthSampleValidity = 35, // measured light remains distinguishable from unavailable queries
		SoundSweep = 36, // the sound sweep's committed source and its two investigate clocks
		// The see-unknown sweep's "stopped seeing it" grace timer beside the other see-unknown fields,
		// and the memory's two dead latch copies dropped: a mid-record change, so the floor moves.
		SeeUnknownSweep = 37,
		// The comfort sweep's re-arm clock and the NPC's `m_hTargetEnt`, appended to the NPC leaf.
		ComfortSweep = 38,

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	// The oldest payload this build can read. A payload below it is rejected with a readable
	// reason rather than half-read.
	//
	// `Sheet` restructured the player's trait storage from a name -> value bag into VtMB's four
	// fixed containers. `Xp` added the award accumulators beside them, `Journal` the quest rows, and
	// `Identity` the name and the quest log's hub tab, and `History` the background trait. All five
	// are additions to the player block with no upgrade branch: an older payload is refused, not
	// half-read.
	//
	// `NpcEnemyMemory` inserts the CAI_Memory block ahead of later NPC leaf blocks. Saves are
	// disposable, so the build refuses every older payload rather than attempting a migration or
	// replaying a shifted leaf.
	static constexpr int32 MinSupported = SeeUnknownSweep;

	static const FGuid GUID;
};

// 'ELYS' — the payload's first four bytes, so a truncated or foreign file fails loudly.
inline constexpr uint32 ElysiumSaveMagic = 0x53594C45u;

// The `Maps` block — one frozen map.

// One entity's saved state. Identity is the **def index** (stable across runs, never reused
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
	// not fit the field walk, written by FElysiumEntity::SaveState.
	TArray<uint8> LeafState;

	// Runtime-spawned entities (npc_maker.Spawn, CreateEntityNoSpawn) live past the def array, so
	// they serialize their synthesized def alongside their state and restore as themselves.
	bool bRuntime = false;
	FElysiumEntityDef Def;

	// In-memory only, never serialized: marks a slot of the world's post-Load omission baseline as
	// filled. A default-constructed row is "no baseline", which makes the freeze record everything.
	bool bCaptured = false;
};

// The `env_fade` screen fade — world state with the map epoch's lifetime, so it is the map's.
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
	int32   DefCount = 0;             // the def array's size when frozen; a mismatch refuses the snapshot
	double  FrozenAt = 0.0;           // game seconds at freeze, for the readable dump

	// **The schema every `FElysiumEntityState::LeafState` blob in this snapshot was written at.**
	//
	// A leaf blob is opaque to the payload: it is captured by writing the entity through its own
	// `Serialize` into a private memory archive and stored as bytes. That archive has no version of
	// its own, so a leaf's `Ar.Version()` gate is only meaningful if the version the blob was WRITTEN
	// at is carried alongside it. Replaying an old blob through a `Latest` archive reads fields the
	// writer never emitted and byte-shifts everything after them.
	//
	// Freezing a map in memory stamps `Latest`, because `CaptureState` writes the blobs at `Latest`.
	// A snapshot read from a file older than `WeaponAnimEvent` defaults to that FILE's version, which
	// is exact: every blob in it was written by the build that wrote the file.
	int32 SchemaVersion = FElysiumSaveVersion::Latest;

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
	TArray<FElysiumEntityHandle> ComfortTargets;

	bool IsValid() const { return !MapName.IsEmpty(); }
};

// The `Session`, `Player` and `World` blocks.

// `G`, the quest map, the clock and the RNG streams — everything that outlives any one map and is
// not the player. `G` serializes as a variant map with no pickling and no interpreter involvement,
// because it already lives in C++. The CPython VM's own namespace is
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

// The whole payload: the four blocks, in that order.
struct FElysiumSavePayload
{
	FElysiumSessionBlock Session;
	FElysiumPlayerRecord Player;
	TMap<FString, FElysiumMapSnapshot> Maps;
	FElysiumWorldBlock   World;
};
