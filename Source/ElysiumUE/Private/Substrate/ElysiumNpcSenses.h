#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"

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
// The seam rule: the engine
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

	// Troika's field-of-view member is 0.2 (`FInViewCone` 0x103264d0). The target's stealth cone
	// scalar multiplies this cosine threshold inside the strict apex/cone test.
	inline constexpr float DefaultViewConeDot = 0.2f;

	// `DAT_10924fb9` / `DAT_10924fba`. ConVars `npc_ignore_player` / `npc_ignore_senses`, default
	// off, not saved. Non-zero is on. The recovered constructors are ConCommands that toggle
	// those bytes (`0x10088c70` / `0x10088d70`); the spec's job is the live bytes as ConVars.
	bool IgnorePlayer();
	bool IgnoreSenses();

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

	// --- Enemy selection (cognition owns the writer) ---
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
	// `CAI_BaseNPCTroika::OnListened` retains one record per raw CSound family.  These are
	// deliberately records, rather than seven booleans: the later investigate programs need the
	// source, origin and type that won `CommitBestSound`.
	FElysiumGameSoundEvent LastSoundCombat;
	FElysiumGameSoundEvent LastSoundBulletImpact;
	FElysiumGameSoundEvent LastSoundFlinch;
	FElysiumGameSoundEvent LastSoundPlayer;
	FElysiumGameSoundEvent LastSoundDanger;
	FElysiumGameSoundEvent LastSoundPhysicsDanger;
	FElysiumGameSoundEvent LastSoundWorld;
	FElysiumGameSoundEvent BestSound;
	// `m_hBestSoundSource` (+0x5b78), written by `CommitBestSound` alongside the record copy and
	// read only by the sound sweep's `SEE_SOUND_SOURCE` tail. It is deliberately the COMMITTED
	// sound's owner, not the sound the current sweep just picked: the tail answers about the
	// sound the NPC decided to act on, which the selector commits (10d), one pass earlier.
	FElysiumEntityHandle BestSoundSource;

	// --- The sound sweep's two clocks ----------------------------------------------------------
	// `m_flNextInvestigateSoundTime` (+0x623c), `FIELD_TIME`, saved, zeroed at Spawn. Gates the
	// WHOLE six-arm body of `FUN_102b1cd0`. Its six writers all live in `SelectSchedule` and the
	// sound selectors (`curtime + 2.0`, or `curtime + 20.0` from the third-party tail
	// `FUN_102b8d20`), which are story 10d -- so until 10d lands nothing re-arms it and the sweep
	// runs every pass. That is an unfinished chain, not a divergence: the reader is exact, and a
	// sweep-local re-arm would be a rule retail does not have.
	double NextInvestigateSoundTime = 0.0;
	// `+0x6418`, the `SEE_SOUND_SOURCE` stranger arm's own rate limit, `curtime + 0.5`. Unlike the
	// gate above, this field's single writer IS the sweep, so it is complete here.
	double NextSeeSoundSourceTime = 0.0;

	// --- Last damage ---------------------------------------------------------------------------
	// Retail's NPC override saves the complete incoming packet before composing the base
	// transaction, and a surviving positive hit remembers its attacker. The typed commit
	// (`FElysiumNpc::OnDamageCommitted`) is the one writer.
	FElysiumEntityHandle LastDamageAttacker;
	double LastDamageTime = -1.0;
	int32 LastDamageAmount = 0;

	// --- The incoming-attack notice ------------------------------------------------------------
	// Retail's melee acquisition sends the aimed target an incoming-melee notice, and the NPC notice
	// path "remembers the attacker for five seconds and lets the concrete combatant schedule its
	// response" (`docs/vtmb/combat-and-damage.md`). This is that record. It is memory, not a
	// condition, which is why it saves and the `DETECTED_ATTACK` bit derived from it would not.
	//
	// The writer is `ElysiumNpcCond::NoticeMeleeAttack`, reached from `TASK_ANNOUNCE_ATTACK`. Its
	// four consumers — the `SHOULD_*` responses — are a policy the survey does not decode, so the
	// record is written and read by the diagnostics only (that seam is stated at the four
	// conditions in `ElysiumNpcConditions.h`).
	FElysiumEntityHandle DetectedAttackAttacker;
	double DetectedAttackTime = -1.0;

	// The repeated-damage window (`+0x5d94` accumulated, `+0x5d98` window root). Damage sums for one
	// second; a sum over 15 percent of Source max health raises `REPEATED_DAMAGE`, and an expired
	// window is RESET rather than decayed. The rule lives on `ElysiumNpcCond::AccumulateDamage`;
	// these are the two bytes it keeps. Negative start means "no window open".
	double RepeatedDamageWindowStart = -1.0;
	int32 RepeatedDamageAccumulated = 0;

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
	// `m_flStealthVisionOverrideTime` (+0x6604), max-written by the surviving-damage tail.
	// It bypasses only the normal visual range gate; cone, concealment and trace remain live.
	double StealthVisionOverrideUntil = -1.0;

	// Slot-472 unknown-vision state. Requirement 10 consumes the flags/timers; R6 owns the
	// observation records so that later programs do not reconstruct a second seen system.
	FElysiumEntityHandle BestSeeUnknown;
	FElysiumEntityHandle LastSeeUnknown;
	FVector LastSeeUnknownPosition = FVector::ZeroVector;
	int32 SeeUnknownRepeatSightings = 0;
	double SeeUnknownRunTimer = -1.0;
	double SeeUnknownStartTimer = -1.0;
	bool bIgnoreUnknown = false;
	bool bMadeInitialUnknownResponse = false;

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
	// 0x10937a8c's unnamed ConVar supplies the pulled-back apex distance in retail.
	// Its default/writer remains unrecovered; the substrate seam answers zero until recovered.
	float ViewConeBodyOffsetCm = 0.f;
	FElysiumNpcConditions HeardConditions;
	bool bSeeUnknownThisPass = false;

	// Resolve `vision`/`hearing`/`npc_perception` into the effective pair. Called from Activate,
	// and idempotent: the resolution is authored data, not runtime state.
	void ResolveTuning(FElysiumNpc& Npc);

	// One condition-gathering pass. `Look` scans actual candidates on their 0.15/0.25/0.45 s
	// cadences; the separate closest-player LOS cache remains 2 s. Hearing and the committed-enemy
	// debounce run every think. Safe with no motor, no body and no services.
	void Tick(FElysiumNpc& Npc, double Now);

	// The three halves, exposed so a test can drive one without the others.
	void TickSight(FElysiumNpc& Npc, double Now);
	void GatherEnemyLos(FElysiumNpc& Npc, double Now);
	void TickHearing(FElysiumNpc& Npc, double Now);
	const TArray<FElysiumEntityHandle>& Sighted() const { return SeenThisPass; }
	void CommitBestSound(const FElysiumNpcConditions& Conditions);
	void ExtendVisionOverride(FElysiumNpc& Npc, FElysiumEntityHandle Source, double Now, double Duration);

	// Where this NPC's hearing has consumed the game-sound bus up to. Serial 0 is "nothing seen
	// yet"; a restored NPC deliberately starts at the live head rather than replaying a window it
	// was never present for.
	uint64 SoundCursor() const { return Cursor; }
	void StartSoundCursorAtHead(const FElysiumNpc& Npc);

	// Base `CBaseCombatCharacter::FInViewCone` / `FinViewCone3dNew` (`0x10326750` /
	// `0x103264d0`). Strict 3-D apex test: Source pitch and inverse-Unreal yaw, reject a
	// negative front-plane dot, then cosine × target cone scalar vs 0.2. Point-only callers
	// (the closest-player cache, witness origins, geometry tests) use this body because they
	// are not slot-363 dispatches.
	//
	// `TargetConeScalar` is the TARGET's own `m_flStealthVisionCone`, which retail applies inside
	// this test alongside the observer's own threshold. 1.0 is a target carrying no stealth
	// surface, which is every character except the player.
	static bool IsInViewCone(const FElysiumNpc& Npc, const FVector& TargetCm,
		float TargetConeScalar = 1.0f);
	// Troika slot 363 (`0x102b4540`): the two sense-off ConVars, then the follower any-angle
	// seam, then the base body at the target's eye. Look and the `SEE_SOUND_SOURCE` stranger
	// arm dispatch this.
	static bool IsInViewCone(const FElysiumNpc& Npc, const FElysiumEntity& Target,
		float TargetConeScalar = 1.0f);
	// Troika FVisible, also BestEnemy's fallback. No Look-only cone or 3072-unit prefilter.
	static bool IsVisible(const FElysiumNpc& Npc, const FElysiumEntity& Candidate, double Now);

	void Serialize(FElysiumSaveArchive& Ar, FElysiumNpc& Npc);

private:
	struct FPendingSound
	{
		EElysiumNpcCond Condition = EElysiumNpcCond::None;
		double PromoteAt = -1.0;
	};
	uint64 Cursor = 0;
	TArray<FPendingSound> PendingSounds;
	// `CAI_Senses::Look` keeps per-candidate throttles separate from closest-player/PVS cache.
	double NextLookTime[3] = { -1.0, -1.0, -1.0 };
	TArray<FElysiumEntityHandle> SeenByChannel[3];
	double LastListenTime = -1.0;
	// One report per NPC for an unreachable perception table. It is a K9 rulebook fact, not a
	// per-think event, and a map full of NPCs must not turn it into a wall.
	bool bWarnedPerception = false;
	TArray<FElysiumEntityHandle> SeenThisPass;
};
