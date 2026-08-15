#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

class FElysiumEntity;
class FElysiumEntityWorld;
class FElysiumNpc;
class UElysiumRulebookSubsystem;
struct FElysiumRuleTable;
struct FElysiumSaveArchive;

// The NPC sensory transaction: resolved perception tuning, the two stimulus channels, and the
// memory they produce. Plain C++ owned by `FElysiumNpc` and driven from its think — the
// `FElysiumNpcMind` posture, with no actor, no UObject and no clock of its own.
//
// The seam rule (`docs/architecture/gameplay-systems-architecture.md` §5.5.3, K13): the engine
// answers one world term — is the segment between two points clear — and nothing else. Cone,
// range, the 2 s cadence, the 8 s grace, the ten-failure debounce, the `vision`/`hearing`/
// `npc_perception` tuning and the hearing radii are substrate rules and stay here.
//
// The VtMB facts: `docs/vtmb/stealth.md` -> "Visual observer transaction" and "Auditory stealth",
// and `docs/vtmb/npc-ai-reverse-engineering.md` -> "Perception, sound, memory, and hostility".

namespace ElysiumNpcSense
{
	// `SetPlayerLOS` refreshes its cached player sight on this cadence, not per think.
	inline constexpr double PlayerLosCadenceSeconds = 2.0;
	// Blocked while the player is still in cone: LOS is preserved this long past the last clear
	// far trace.
	inline constexpr double BlockedInConeGraceSeconds = 8.0;
	// At or below this distance an in-cone target is seen with no trace at all.
	inline constexpr float NearBypassUnits = 512.0f;
	// A target between this fraction of the effective radius and the whole of it sets the outer
	// band. The band is carried, not consumed: its attention/investigation consumer (`0x102b3e00`)
	// has no recovered authored name, so nothing reads it yet.
	inline constexpr float OuterBandFraction = 0.7f;
	// `GatherEnemyConditions` increments a failure counter up to ten. Below ten the committed
	// enemy retains `HAVE_ENEMY_LOS`; at ten it becomes `ENEMY_OCCLUDED`.
	inline constexpr int32 EnemyLosFailureLimit = 10;
	// The `-1.0` sentinel `InitPerceptionDistances` reads on `vision` and `hearing`.
	inline constexpr float DerivedSentinel = -1.0f;

	// CHOSEN, NOT RECOVERED: the observer's own cone threshold. `FInViewCone` (`0x10326750`)
	// compares a dot product against the observer's field-of-view member, and neither VtMB's
	// authored default nor the member's spawn value is recovered. This is Source's stock NPC
	// value (0.5 = a 120-degree cone), which is also what the stealth cone-scalar table's neutral
	// 1.0 multiplies to nothing. Replace it, not the multiply, when the value is recovered.
	inline constexpr float DefaultViewConeDot = 0.5f;

	// The `Inspection` feat's two nested tables, by their authored `InternalName`.
	inline const TCHAR* VisionTableName = TEXT("Inspection_Vision_Distances");
	inline const TCHAR* HearingTableName = TEXT("Inspection_Hearing_Scalars");

	// `sound_volume_table.txt` authors (commented out) `AVERAGE_HUMAN_INSPECTION 3`, and the
	// hearing table's own row 3 carries the comment "An average human has inspection of 3". Those
	// two rows are the warn-and-default answer when the tables are unreachable.
	inline constexpr int32 AverageHumanInspection = 3;
	inline constexpr float FallbackVisionUnits = 440.0f;
	inline constexpr float FallbackHearingScalar = 1.0f;
}

// `InitPerceptionDistances` (`0x1028fb70`) resolved once, at Activate: the authored value copied
// through, or the `npc_perception` row of the matching `Inspection` table when the author wrote the
// `-1.0` sentinel. Retail keeps the pair at `+0x63b8` / `+0x63c0`.
struct FElysiumNpcPerception
{
	// The effective vision distance, in CENTIMETRES. The table and the keyvalue are both in Source
	// game units; the conversion happens once, here, so nothing downstream carries a unit question.
	float VisionDistanceCm = 0.f;
	// The effective hearing scalar, unitless. It multiplies an emitted sound's own radius.
	float HearingScalar = 1.f;
	bool bResolved = false;
	// True when either channel fell back because the table was unreachable. Diagnostic only.
	bool bUsedFallback = false;

	// The rule itself, over the two tables already in hand. Either may be null — a headless
	// substrate world has no rulebook behind it — and a null table takes the average-human
	// fallback and reports it through `OutWarning`. `OutWarning` is empty when nothing needed
	// reporting; a sentinel resolved from a real table is not a failure.
	void Resolve(int32 AuthoredPerception, float AuthoredVision, float AuthoredHearing,
		const FElysiumRuleTable* VisionTable, const FElysiumRuleTable* HearingTable,
		FString& OutWarning);

	// The K9 join: find the `Inspection` feat's two nested tables and apply the rule above.
	// `Rules` may be null.
	void ResolveFromRulebook(int32 AuthoredPerception, float AuthoredVision, float AuthoredHearing,
		UElysiumRulebookSubsystem* Rules, FString& OutWarning);
};

// What the native object retains across the loss of current sight: enemy, last enemy, the
// last-seen target per relation category, the last heard stimulus, the last damage packet, the
// committed-enemy occlusion state and the closest-player cache. "Not currently visible" is not
// "forgotten and neutral", which is exactly what this struct exists to keep separate.
struct FElysiumNpcMemory
{
	// The four relation categories the recovered `SEE_*` conditions name.
	enum class ESeen : uint8 { Hate, Fear, Dislike, Nemesis, Count };

	// --- Enemy selection (cycle 5 owns the writer; the fields are plumbed now) ----------------
	FElysiumEntityHandle Enemy;
	FElysiumEntityHandle LastEnemy;

	// --- Last seen, by relation category -------------------------------------------------------
	FElysiumEntityHandle LastSeen[static_cast<int32>(ESeen::Count)];
	double LastSeenTime[static_cast<int32>(ESeen::Count)] = { -1.0, -1.0, -1.0, -1.0 };

	// --- Last heard ----------------------------------------------------------------------------
	FElysiumEntityHandle LastHeardSource;      // invalid means "the world made it" (a door)
	FVector LastHeardPosition = FVector::ZeroVector;
	FString LastHeardCategory;                 // the `SoundTypes` name, as emitted
	double LastHeardTime = -1.0;

	// --- Last damage ---------------------------------------------------------------------------
	// Retail's NPC override saves the complete incoming packet before composing the base
	// transaction, and a surviving positive hit remembers its attacker. The typed commit
	// (`FElysiumNpc::OnDamageCommitted`) is the one writer.
	FElysiumEntityHandle LastDamageAttacker;
	double LastDamageTime = -1.0;
	int32 LastDamageAmount = 0;

	// --- Committed-enemy LOS (`GatherEnemyConditions`) -----------------------------------------
	int32 EnemyLosFailures = 0;                // consecutive failed checks, capped at the limit
	double EnemyLastLosTime = -1.0;
	bool bEnemyOccluded = false;               // the debounce has flipped to ENEMY_OCCLUDED
	// The retained memory bit that makes the found/lost outputs edge-triggered rather than
	// per-think. One acquisition episode fires `OnFoundEnemy` once and `OnLostEnemyLOS` once.
	bool bEnemyLosLatched = false;

	// --- Closest player + its LOS cache (`SetClosestPlayer` / `SetPlayerLOS`) -------------------
	// The nearest-player cache is NOT hostility admission and fires no output on its own.
	FElysiumEntityHandle ClosestPlayer;
	float ClosestPlayerDistanceCm = 0.f;
	bool bPlayerInRange = false;               // within the effective visual radius
	bool bPlayerInOuterBand = false;           // between 0.7x and 1.0x of it
	bool bPlayerInCone = false;
	bool bPlayerLos = false;
	double PlayerLosLastClearTime = -1.0;
	double PlayerLosNextUpdateTime = -1.0;     // negative means "due now"

	void Reset();
	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const FElysiumEntityWorld& World);

	const FElysiumEntityHandle& Seen(ESeen Category) const
	{
		return LastSeen[static_cast<int32>(Category)];
	}
};

// The per-NPC senses runner. Owned by value on `FElysiumNpc`; every entry point takes the owning
// NPC so the object holds no back pointer to rebind across a save.
class FElysiumNpcSenses
{
public:
	FElysiumNpcPerception Perception;
	FElysiumNpcMemory Memory;

	// Resolve `vision`/`hearing`/`npc_perception` into the effective pair. Called from Activate,
	// and idempotent: the resolution is authored data, not runtime state.
	void ResolveTuning(FElysiumNpc& Npc);

	// One condition-gathering pass. Sight runs on its own 2 s cadence, hearing and the
	// committed-enemy debounce run every think. Safe with no motor, no body and no services.
	void Tick(FElysiumNpc& Npc, double Now);

	// The three halves, exposed so a test can drive one without the others.
	void TickSight(FElysiumNpc& Npc, double Now);
	void GatherEnemyLos(FElysiumNpc& Npc, double Now);
	void TickHearing(FElysiumNpc& Npc, double Now);

	// Where this NPC's hearing has consumed the game-sound bus up to. Serial 0 is "nothing seen
	// yet"; a restored NPC deliberately starts at the live head rather than replaying a window it
	// was never present for.
	uint64 SoundCursor() const { return Cursor; }
	void StartSoundCursorAtHead(const FElysiumNpc& Npc);

	// The observer's cone test, `FInViewCone` in this runtime's frame. Horizontal only: an NPC
	// body carries a yaw, and retail's own observer test is a yaw-plane comparison.
	static bool IsInViewCone(const FElysiumNpc& Npc, const FVector& TargetCm);

	void Serialize(FElysiumSaveArchive& Ar, FElysiumNpc& Npc);

private:
	uint64 Cursor = 0;
	// One report per NPC for an unreachable perception table. It is a K9 rulebook fact, not a
	// per-think event, and a map full of NPCs must not turn it into a wall.
	bool bWarnedPerception = false;
};
