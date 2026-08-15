#include "Substrate/ElysiumNpcMaker.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcLog.h"

const TCHAR* FElysiumNpcMaker::AttemptName(EAttempt Attempt)
{
	switch (Attempt)
	{
	case EAttempt::Spawned:      return TEXT("spawned");
	case EAttempt::LiveLimit:    return TEXT("live-limit");
	case EAttempt::Scene:        return TEXT("scene");
	case EAttempt::Visible:      return TEXT("visible");
	case EAttempt::ViewCone:     return TEXT("view-cone");
	case EAttempt::Distance:     return TEXT("distance");
	case EAttempt::Occupied:     return TEXT("occupied");
	case EAttempt::InvalidChild: return TEXT("invalid-child");
	}
	return TEXT("unknown");
}

void FElysiumNpcMaker::Spawn()
{
	LiveChildren = 0;
	CachedGroundZ = 0.0f;
	if (bInfinite)
	{
		bFade = true;
	}
	NextThink = bDisabled ? ELYSIUM_NEVER_THINK
		: static_cast<float>((World ? World->NowSeconds() : 0.0) + SpawnFrequency);
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::CanMakeNpc(bool bBypass) const
{
	if (bBypass)
	{
		return EAttempt::Spawned;
	}
	if (MaxLiveChildren > 0 && LiveChildren >= MaxLiveChildren)
	{
		return EAttempt::LiveLimit;
	}
	if (World && World->IsNpcMakerSceneBlocked())
	{
		return EAttempt::Scene;
	}
	const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Player)
	{
		if (bNpcClip && Embodiment && Embodiment->IsNpcMakerVisibleFromPlayer(Origin))
		{
			return EAttempt::Visible;
		}
		if (bViewCone && Embodiment && Embodiment->IsNpcMakerInPlayerViewCone(Origin))
		{
			return EAttempt::ViewCone;
		}
		if (MinPcDistance > 0)
		{
			const int32 DistanceUnits = FMath::TruncToInt(
				FVector::Dist(Player->Origin, Origin) / ElysiumMove::U);
			if (DistanceUnits < MinPcDistance)
			{
				return EAttempt::Distance;
			}
		}
	}
	if (Embodiment && Embodiment->IsNpcMakerSpawnAreaOccupied(
		FVector(Origin.X, Origin.Y, CachedGroundZ), 34.0f * ElysiumMove::U))
	{
		return EAttempt::Occupied;
	}
	return EAttempt::Spawned;
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::TrySpawn(bool bBypass)
{
	if (!World || !Def)
	{
		return LastAttempt = EAttempt::InvalidChild;
	}
	if (CachedGroundZ == 0.0f)
	{
		CachedGroundZ = World->Embodiment()
			? World->Embodiment()->ResolveNpcMakerGroundZ(Origin, 2048.0f * ElysiumMove::U)
			: Origin.Z;
	}
	const EAttempt Admission = CanMakeNpc(bBypass);
	if (Admission != EAttempt::Spawned)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Spawn rejected: %s (live %d/%d)"),
			*DebugString(), AttemptName(Admission), LiveChildren, MaxLiveChildren);
		return LastAttempt = Admission;
	}
	if (NpcType.IsEmpty())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: no NPCType"), *DebugString());
		return LastAttempt = EAttempt::InvalidChild;
	}

	static const TSet<FName> MakerOnlyKeys = {
		FName(TEXT("classname")), FName(TEXT("targetname")), FName(TEXT("origin")),
		FName(TEXT("angles")), FName(TEXT("spawnflags")), FName(TEXT("NPCType")),
		FName(TEXT("MaxNPCCount")), FName(TEXT("SpawnFrequency")),
		FName(TEXT("MaxLiveChildren")), FName(TEXT("NPCTargetname")),
		FName(TEXT("Flag_StartDisabled")), FName(TEXT("Flag_NPCClip")),
		FName(TEXT("Flag_Fade")), FName(TEXT("Flag_InfChild")),
		FName(TEXT("Flag_NoDrop")), FName(TEXT("Flag_ViewCone")),
		FName(TEXT("MinPCDistance"))
	};
	FElysiumEntityDef Child;
	Child.Classname = NpcType;
	Child.Origin = Origin;
	for (const TPair<FString, FString>& KV : Def->Keys)
	{
		if (!MakerOnlyKeys.Contains(FName(*KV.Key)))
		{
			Child.Keys.Add(KV.Key, KV.Value);
		}
	}
	Child.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"), Angles.X, Angles.Y, Angles.Z));
	Child.Outputs = Def->Outputs; // each Construct seeds fresh per-row times counters

	const FElysiumEntityHandle ChildHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(Child));
	FElysiumEntity* ChildEntity = World->Resolve(ChildHandle);
	if (!ChildEntity || ChildEntity->IsRecordOnly() || !ChildEntity->AsCombatCharacter())
	{
		if (ChildEntity)
		{
			ChildEntity->Kill();
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' is not a live NPC class"),
			*DebugString(), *NpcType);
		return LastAttempt = EAttempt::InvalidChild;
	}

	static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
	FireOutput(OnSpawnNpc, Handle);
	ChildEntity->SpawnFlags = bFade ? 0x204 : 4;
	World->CallEntitySpawn(*ChildEntity);
	if (ChildEntity->IsDead())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
			*DebugString(), *NpcType);
		return LastAttempt = EAttempt::InvalidChild;
	}
	ChildEntity->SetOwnerEntity(Handle);
	World->RenameEntity(*ChildEntity, ChildTargetName);
	++LiveChildren;
	if (!bInfinite)
	{
		--RemainingTotal;
		if (IsDepleted())
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s (live %d/%d, remaining %d%s)"),
		*DebugString(), *World->DescribeHandle(ChildHandle), LiveChildren, MaxLiveChildren,
		RemainingTotal, bInfinite ? TEXT(" infinite") : TEXT(""));
	return LastAttempt = EAttempt::Spawned;
}

void FElysiumNpcMaker::InputEnable(const FElysiumInputArgs&)
{
	if (IsDepleted())
	{
		return;
	}
	bDisabled = false;
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumNpcMaker::InputDisable(const FElysiumInputArgs&)
{
	bDisabled = true;
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumNpcMaker::InputToggle(const FElysiumInputArgs& Args)
{
	if (bDisabled) { InputEnable(Args); }
	else { InputDisable(Args); }
}

void FElysiumNpcMaker::Think()
{
	const EAttempt Result = TrySpawn(/*bBypass=*/false);
	if (Result == EAttempt::Spawned && IsDepleted())
	{
		return;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	if (Result == EAttempt::Spawned || Result == EAttempt::LiveLimit)
	{
		NextThink = static_cast<float>(Now + SpawnFrequency);
	}
	else
	{
		NextThink = static_cast<float>(Now
			+ ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f));
	}
}

void FElysiumNpcMaker::OnOwnedEntityTerminated(FElysiumEntity& Child,
	EElysiumOwnedEntityTermination Reason)
{
	if (Reason == EElysiumOwnedEntityTermination::RemovedAlive)
	{
		if (!bInfinite)
		{
			++RemainingTotal;
		}
	}
	else
	{
		static const FName OnNpcDied(TEXT("OnNPCDied"));
		FireOutput(OnNpcDied, Child.Handle);
	}
	if (IsDepleted())
	{
		static const FName OnLastNpcDied(TEXT("OnLastNPCDied"));
		FireOutput(OnLastNpcDied, Child.Handle);
	}
	LiveChildren = FMath::Max(0, LiveChildren - 1);
}

void FElysiumNpcMaker::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
	Out.Emplace(TEXT("NPCType"), NpcType.IsEmpty() ? TEXT("(none)") : NpcType);
	Out.Emplace(TEXT("NPCTargetname"), ChildTargetName.IsEmpty() ? TEXT("(none)") : ChildTargetName);
	Out.Emplace(TEXT("Live children"), FString::Printf(TEXT("%d / %d"), LiveChildren, MaxLiveChildren));
	Out.Emplace(TEXT("Remaining total"), bInfinite ? TEXT("infinite") : FString::FromInt(RemainingTotal));
	Out.Emplace(TEXT("Spawn frequency"), FString::Printf(TEXT("%.3f s"), SpawnFrequency));
	Out.Emplace(TEXT("Cached ground Z"), FString::SanitizeFloat(CachedGroundZ));
	Out.Emplace(TEXT("Last attempt"), AttemptName(LastAttempt));
}
