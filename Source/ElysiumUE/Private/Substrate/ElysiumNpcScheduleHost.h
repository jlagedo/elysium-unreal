#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntityWorld;
struct FElysiumSaveArchive;

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
	double InterruptTime = 0.0;
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

	void ResetThinkTimers(double Now) { NextUpdate = NextNormal = NextMove = NextAI = Now; }
	void Serialize(FElysiumSaveArchive& Ar, const FElysiumEntityWorld* World);
};
