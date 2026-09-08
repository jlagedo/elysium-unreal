#include "Substrate/ElysiumNpcScheduleHost.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

void FElysiumNpcScheduleHost::Serialize(FElysiumSaveArchive& Ar, const FElysiumEntityWorld* World)
{
	Ar << NextUpdate << NextNormal << NextMove << NextAI;
	Ar << LastUpdate << LastNormal << LastMove << LastAI;
	Ar << FailureReason << MemoryBits << GoalToleranceCm << DesiredMoveYaw;
	Ar << SquadDisconnected;
	Ar << InsideInterruptDistanceSqr << OutsideInterruptDistanceSqr << InterruptTime;
	Ar << MoveTarget << KickProp << HintNode << HintReusableAt << SavedSleepExtents << bPatrolPathUseHint;
	Ar << bOwnsHint << FailedCoverLosChecks << AttackExtentsCm;
	Ar << bSavePositionWalk << bMotorAnimationMovement << bWaitFinishedSet << Unknown6300 << Unknown659c << MoveWaitFinished;
	if (Ar.IsLoading()) PendingFailureReason = 0;
	if (Ar.IsLoading() && World)
	{
		MoveTarget = World->RebaseSavedHandle(MoveTarget);
		KickProp = World->RebaseSavedHandle(KickProp);
	}
}
