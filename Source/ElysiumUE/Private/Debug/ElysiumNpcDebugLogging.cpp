#include "Debug/ElysiumNpcDebugLogging.h"

#if ELYSIUM_NPC_VLOG

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "VisualLogger/VisualLogger.h"

namespace
{
	AActor* BodyActor(const FElysiumNpc& Npc)
	{
		const USkeletalMeshComponent* Visual = Npc.GetSkeletalBody();
		return Visual ? Visual->GetAttachParentActor() : nullptr;
	}

	const FColor OuterBandColour(255, 180, 60);
	const FColor InBandColour(90, 220, 120);
	const FColor ConeColour(90, 170, 255);
}

namespace ElysiumNpcDebugLogging
{
	void Sighting(const FElysiumNpc& Npc, const FElysiumEntity& Candidate,
		const FElysiumNpcSightingDebug& Sighting)
	{
		AActor* Owner = BodyActor(Npc);
		if (Owner == nullptr)
		{
			return;
		}
		// Senses re-confirms the committed enemy every tick it is in view. Drawing three shapes
		// for each of those buries the timeline in combat, so an in-band reconfirmation of the
		// current enemy logs nothing; admissions, band changes and bypasses still draw.
		const bool bReconfirmation = Candidate.Handle == Npc.Senses.Memory.Enemy
			&& !Sighting.bOuterBand && !Sighting.bRangeBypass && !Sighting.bDamageOverride;
		if (bReconfirmation)
		{
			return;
		}

		const TCHAR* Arm = Sighting.bRangeBypass ? TEXT(" range-bypass")
			: Sighting.bDamageOverride ? TEXT(" damage-override")
			: Sighting.bOuterBand ? TEXT(" outer-band") : TEXT("");
		const FColor BandColour = Sighting.bOuterBand ? OuterBandColour : InBandColour;

		// The cone Senses tested: the observer's own forward, with the apex shifted back by the
		// body offset, and the FOV widened by the target's cone scalar.
		const float ConeDot = FMath::Clamp(ElysiumNpcSense::DefaultViewConeDot
			/ FMath::Max(0.01f, Sighting.TargetConeScalar), -1.0f, 1.0f);
		const float HalfAngle = FMath::Acos(ConeDot);
		const FVector Forward = FElysiumNpcSenses::ViewForward(Npc);
		const FVector Eye = Npc.EyePosition();
		const FVector Apex = Eye - Forward * Npc.Senses.ViewConeBodyOffsetCm;

		UE_VLOG_CIRCLE(Owner, LogElysiumNpcEnt, Log, Eye, FVector::UpVector,
			Sighting.EffectiveRadiusCm, BandColour, TEXT("vision radius %.0fcm%s"),
			Sighting.EffectiveRadiusCm, Arm);
		UE_VLOG_CONE(Owner, LogElysiumNpcEnt, Log, Apex, Forward, Sighting.EffectiveRadiusCm,
			HalfAngle, ConeColour, TEXT("view cone %.1fdeg"),
			FMath::RadiansToDegrees(HalfAngle * 2.0f));
		UE_VLOG_SEGMENT(Owner, LogElysiumNpcEnt, Log, Eye, Candidate.EyePosition(), BandColour,
			TEXT("sighted %s%s"), *Candidate.DebugString(), Arm);
	}

	void EnemyChoice(const FElysiumNpc& Npc, const FElysiumEntityWorld& World,
		const FElysiumEntityHandle& OldEnemy, const FElysiumEntityHandle& NewEnemy)
	{
		AActor* Owner = BodyActor(Npc);
		if (Owner == nullptr)
		{
			return;
		}
		const FElysiumEntity* NewEntity = ElysiumNpcCond::ResolveEnemyHandle(World, NewEnemy);
		UE_VLOG(Owner, LogElysiumNpcEnt, Log, TEXT("enemy choice %s -> %s"),
			*OldEnemy.ToString(), *NewEnemy.ToString());
		if (NewEntity != nullptr)
		{
			UE_VLOG_ARROW(Owner, LogElysiumNpcEnt, Log, Npc.EyePosition(),
				NewEntity->EyePosition(), FColor(240, 80, 80), TEXT("committed enemy"));
		}
	}

	void ScheduleInstalled(const FElysiumNpc& Npc, EElysiumScheduleId Schedule)
	{
		AActor* Owner = BodyActor(Npc);
		if (Owner == nullptr)
		{
			return;
		}
		UE_VLOG_LOCATION(Owner, LogElysiumNpcEnt, Log, Npc.Origin, 14.0f,
			FColor(255, 220, 80), TEXT("schedule installed: %s (%d)"),
			ElysiumScheduleName(Schedule), ElysiumScheduleNumber(Schedule));
		UE_VLOG(Owner, LogElysiumNpcEnt, Log, TEXT("schedule installed: %s (%d)"),
			ElysiumScheduleName(Schedule), ElysiumScheduleNumber(Schedule));
	}
}

#endif // ELYSIUM_NPC_VLOG
