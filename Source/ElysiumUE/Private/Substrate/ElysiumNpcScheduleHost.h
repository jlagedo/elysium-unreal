#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
// The schedule ids the fail/interrupt/ideal words below name. `ElysiumSchedule.h` does not reach
// back here, so this is a one-way edge.
#include "Substrate/ElysiumSchedule.h"

class FElysiumEntityWorld;
struct FElysiumSaveArchive;

// Story 29c-1, family **Hints**. Five retail bodies — `0x102a13d0`, `0x102a1420`, `0x102a1470`,
// `0x102a14c0`, `0x102a1510` — are ONE lookup written five times: read the current hint node
// (`m_pHintNode +0x5ddc`), switch on its `m_nHintType` (`+0x5dc`) over exactly 100 / 101 / 10200,
// answer a retail `Activity` id, and answer -1 for every other type and for no hint at all. Three
// of the five subtract one from the 10200 answer when `m_bLeaningLeft` (`+0x63fd`) is set; two do
// not. They are named by address because the ids they answer have no retail symbol.
enum class EElysiumHintActivityQuery : uint8
{
	Query102a13d0,
	Query102a1420,
	Query102a1470,
	Query102a14c0,
	Query102a1510,
};

// Saved interpreter state read/written by TaskFail and the four Troika think clocks.
// Navigation projections remain body services; these fields are the authored goal/interrupt state.
struct FElysiumNpcScheduleHost
{
	double NextUpdate = 0.0, NextNormal = 0.0, NextMove = 0.0, NextAI = 0.0;
	double LastUpdate = 0.0, LastNormal = 0.0, LastMove = 0.0, LastAI = 0.0;
	// `m_flEnemyDist +0x6268`, `m_flEnemyHeightDiff +0x626c`, `m_flEnemyLastKnownDist +0x6270`,
	// in SOURCE UNITS as retail's combat selectors compare them; `5000.0` with no live enemy.
	// Session state: `NPCThink` rewrites all three on every normal-due think.
	float EnemyDistUnits = 5000.f, EnemyHeightDiffUnits = 5000.f, EnemyLastKnownDistUnits = 5000.f;
	int32 FailureReason = 0;
	int32 PendingFailureReason = 0; // consumed in the same maintenance pass; never saved
	uint32 MemoryBits = 0;
	int32 SquadDisconnected = 0; // +0x5bb0; shared-memory routing lands with the squad store
	float GoalToleranceCm = 0.f;
	float DesiredMoveYaw = 0.f;
	float InsideInterruptDistanceSqr = 0.f;
	float OutsideInterruptDistanceSqr = 0.f;
	double InterruptTime = 0.0; // +0x632c m_flInterruptTime
	// `m_flCacheInterruptTime` (`+0x1b24`), distinct from `InterruptTime` above.
	// `GetNewSchedule 0x102814d0` refreshes the cached masks only while this is before curtime.
	double CacheInterruptTime = 0.0;
	// `CAI_BaseNPCTroika::m_hMoveTargetEnt`: -1 at spawn (`0x1029a0b0`), released by `TaskFail`
	// (`0x1029adb0`) and `OnScheduleChange` (`0x102a0940`), read by Troika `StartTask` (`0x102a1910`).
	// NOT `CAI_BaseNPC::m_hTargetEnt` (+0x5ce4), which is `FElysiumNpc::TargetEnt` and is never
	// cleared by either.
	FElysiumEntityHandle MoveTarget;
	FElysiumEntityHandle KickProp;
	int32 HintNode = INDEX_NONE;
	double HintReusableAt = 0.0;
	bool bOwnsHint = false; // CAI_Hint +0x5e0; hint claim producer lands with the hint store
	int32 FailedCoverLosChecks = 0;
	FVector SavedSleepExtents = FVector(-1.0);
	FVector AttackExtentsCm = FVector::ZeroVector; // additive attack-partition margin, entity +0x50..58
	bool bPatrolPathUseHint = false;
	bool bSavePositionWalk = false; // m_fSavePositionWalk
	bool bMotorAnimationMovement = false; // CAI_Motor +0x28; task producer not built yet
	bool bWaitFinishedSet = false; // m_bWaitFinishedSet
	uint32 Unknown6300 = 0, Unknown659c = 0; // cleared by both Troika teardown virtuals
	double MoveWaitFinished = 0.0; // m_flMoveWaitFinished, base schedule-change stage

	// --- The retail words, declared and unwritten ------------------------------------------------
	//
	// Every word of `CAI_BaseNPCTroika` this struct owns that no port system writes yet
	// (`docs/vtmb/npc-kernel/layout.md`), default-initialised, each carrying its offset, its
	// retail name and the tier that typed it. They are the shape 29b landed so a later story
	// fills a member instead of inventing one; `ElysiumNpcKernelShapeMap.cpp` binds every one of
	// them to its offset and the shape test fails if one goes missing.
	bool bShouldMove = false;  // +0x1a40 m_bShouldMove (datamap)
	bool bRanAi = false;  // +0x1b4c m_bRanAI (walked)
	// +0x5c3c m_IdealSchedule (datamap). Raw int32: retail preserves local ids, global ids >= 1e9
	// and -1 here before TranslateSchedule/GetScheduleOfType resolves the installed pointer.
	int32 IdealScheduleRetail = 0;
	bool bDoPostRestoreRefindPath = false;  // +0x5c58 m_bDoPostRestoreRefindPath (doc)
	FString HintGroup;  // +0x5db0 m_strHintGroup (datamap)
	// +0x5db4 m_flWaitFinished (datamap) — an absolute curtime deadline, beside its existing
	// bWaitFinishedSet
	double WaitFinished = 0.0;
	FElysiumEntityHandle GoalEnt;  // +0x5de8 m_pGoalEnt (datamap)
	FElysiumEntityHandle StoredPathTarget;  // +0x5df4 m_hStoredPathTarget (sdk-order)
	FVector StoredPathGoal = FVector::ZeroVector;  // +0x5df8 m_vecStoredPathGoal (sdk-order)
	// +0x5e04 m_nStoredPathType (sdk-order) — GoalType_t has no port enum
	int32 StoredPathType = 0;
	int32 StoredPathFlags = 0;  // +0x5e08 m_fStoredPathFlags (sdk-order)
	FString FailText;  // +0x5f30 m_failText (sdk-order)
	FString InterruptText;  // +0x5f34 m_interruptText (sdk-order)
	// +0x5f38 m_failedSchedule (sdk-order)
	EElysiumScheduleId FailedSchedule = EElysiumScheduleId::None;
	// +0x5f3c m_interuptSchedule (sdk-order)
	EElysiumScheduleId InterruptSchedule = EElysiumScheduleId::None;
	FString HintGroups;  // +0x62e0 m_sHintGroups (datamap) — the authored hint-group allowlist, KEY
	                     // key=hint_groups
	// +0x62e4 m_iHintGroups (datamap) — the parsed allowlist, as retail carries it: a 32-bit SET,
	// one bit per group id, written by `0x102989e0` and read by `FValidateHintType 0x10295c20`,
	// which admits a hint node when `node->m_iHintGroup & m_iHintGroups` is non-zero. An unset
	// or empty `hint_groups` is `0xffffffff` — every group — so the AND always passes; that is
	// why the default here is all-ones and not zero.
	uint32 HintGroupMask = 0xffffffffu;
	// +0x6330 m_flWaitFinishedDelta (datamap) — FIELD_FLOAT in retail: a delta added to the wait
	// deadline, not a stamp
	float WaitFinishedDelta = 0.f;
	// +0x6400 m_flNextCoverLOSCheck (datamap) — FIELD_TIME; declared beside the existing
	// FailedCoverLosChecks
	double NextCoverLosCheck = 0.0;
	bool bForceCoverLosCheck = false;  // +0x6408 m_bForceCoverLOSCheck (datamap)
	bool bAllowKickHintUse = false;  // +0x6436 m_bAllowKickHintUse (datamap)
	// +0x6438 m_flKickPhysicsPropSearchTimer (datamap) — FIELD_TIME; an absolute stamp
	double KickPropSearchTimer = 0.0;
	// +0x6440 m_flNextShootAtHintSearchTime (doc) — an absolute curtime deadline
	double NextShootAtHintSearchTime = 0.0;
	// +0x6444 m_pShootAtHint (walked) — hints are node indices here, as HintNode is; NOT the
	// shoot-target override at +0x5ba8
	int32 ShootAtHintNode = 0;
	FElysiumEntityHandle HintCoverObject;  // +0x6448 m_hHintCoverObject (datamap)
	// +0x65c8 m_iForcedSchedule (datamap) — a registered schedule id, as every other schedule word
	// here is
	EElysiumScheduleId ForcedSchedule = EElysiumScheduleId::None;

	/**
	 * The five hint-node activity lookups, as one table (story 29c-1, family Hints).
	 *
	 * `HintType` is the resolved `m_nHintType` of `HintNode`; pass -1 (or any type outside the three
	 * retail cases) for "no hint node", which is retail's `m_pHintNode == NULL` arm. Pure, because
	 * every one of the five retail bodies is pure once the hint's type is in hand — and `HintNode`
	 * here is a bare index with no store behind it, so the NPC-side seam is what resolves it.
	 */
	static int32 HintNodeActivity(EElysiumHintActivityQuery Query, int32 HintType,
		bool bLeaningLeft);

	// --- Story 29c-1, family Schedule -------------------------------------------------------------

	/**
	 * `0x10280db0`: `m_iScheduleIndex (+0x5c40) == m_pSchedule->NumTasks (CAI_Schedule +0x24)`.
	 *
	 * The recovered name reads as "is the task index current"; what the body TESTS is whether the
	 * index has reached the program's task count, which is "the program is exhausted". Its one
	 * caller, `NextScheduledTask` (`0x10280f40`), uses it that way — raising `COND_SCHEDULE_DONE`
	 * when it answers true.
	 *
	 * Static, and takes the schedule state rather than reading a member, because both words it
	 * reads live on `FElysiumScheduleState` (`+0x5c38` → `Current`, `+0x5c40` → `TaskIndex`) and the
	 * task count is the registered program's. With no installed program retail dereferences a null
	 * `m_pSchedule`; this answers false, which is the divergence a null would otherwise crash on.
	 */
	static bool IsTaskIndexCurrent(const FElysiumScheduleState& State);

	/**
	 * `0x102a18a0` — `TASK_WAIT`'s deadline stamp.
	 *
	 * `if (0.0 < task->flTaskData) m_flWaitFinished = curtime + flTaskData;` and otherwise
	 * `curtime + _DAT_10447ee0`, a retail default duration. So a task whose operand is zero or
	 * negative waits the default rather than not at all.
	 *
	 * **NAMED `SetWaitFinished`, not 29c's `WaitFinished`**: that name is already the `+0x5db4` data
	 * member 29b declared, which this writes.
	 */
	void SetWaitFinished(float TaskSeconds, double Now);

	/**
	 * Slot 619 `SetSchedule(int)`'s five species overrides — `CNPC_VAndreiBlood` (`0x1035dba0`),
	 * `CNPC_VAsianVampire` (`0x10361530`), `CNPC_VChangBros` and its two leaves (`0x1036c760`),
	 * `CNPC_VSabbatLeader` (`0x103a9fd0`) and `CNPC_VSheriffMan` (`0x103af8d0`).
	 *
	 * All five are 100-byte scope-trace wrappers around the shared `CAI_BaseNPC::SetSchedule(int)`
	 * (`0x10280de0`, ported as `FElysiumNpc::ChangeSchedule`): push a literal name onto retail's
	 * `g_ScopeTraceStack`, forward, pop. They carry NO class-specific logic, so the whole of what
	 * they add over the shared body is the name they push — which is what this answers. Empty for a
	 * class with no slot-619 override.
	 */
	static const TCHAR* SetScheduleTraceName(const TCHAR* RetailClass);

	void ResetThinkTimers(double Now) { NextUpdate = NextNormal = NextMove = NextAI = Now; }
	void Serialize(FElysiumSaveArchive& Ar, const FElysiumEntityWorld* World);
};
