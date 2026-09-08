#include "Substrate/ElysiumStealthKillRules.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Substrate/ElysiumVdataLoad.h"

bool FElysiumStealthKillRules::Load(FString& OutError)
{
	TSharedPtr<ElysiumKeyValues::FKvNode> Root;
	return ElysiumVdata::ReadVdata(TEXT("system/StealthKillRules.txt"), Root, OutError)
		&& Parse(*Root, OutError);
}

bool FElysiumStealthKillRules::Parse(const ElysiumKeyValues::FKvNode& Root, FString& OutError)
{
	const ElysiumKeyValues::FKvNode* Data = Root.Child(TEXT("StealthKillRules"));
	if (!Data) Data = &Root;
	const auto* Arc = Data->Child(TEXT("DeafZoneArc"));
	const auto* Stats = Data->Child(TEXT("StatInfo"));
	if (!Arc || !Stats)
	{
		OutError = TEXT("StealthKillRules requires DeafZoneArc and StatInfo");
		return false;
	}
	float Previous = 0.f;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Previous = Arc->Flt(*FString::FromInt(Index + 1), Previous);
		DeafArcDegrees[Index] = Previous;
	}
	FeatMin = Stats->Int(TEXT("StealthFeatMin"), 1);
	FeatMax = Stats->Int(TEXT("StealthFeatMax"), 10);
	HearingMin = Stats->Flt(TEXT("HearingScalarMin"), 0.f);
	HearingMax = Stats->Flt(TEXT("HearingScalarMax"), 3.f);
	DistanceMaxUnits = Stats->Flt(TEXT("StealthKillDistMax"), 70.f);
	return true;
}

float FElysiumStealthKillRules::ArcDot(int32 CombatFeat) const
{
	return CombatFeat < 1 || CombatFeat > 20 ? 1.f
		: FMath::Cos(FMath::DegreesToRadians(DeafArcDegrees[CombatFeat - 1] * 0.5f));
}

float FElysiumStealthKillRules::MinDepthUnits(int32 Sneaking, float Hearing) const
{
	const float Skill = FeatMax != FeatMin
		? FMath::Clamp(float(Sneaking - FeatMin) / float(FeatMax - FeatMin), 0.f, 1.f) : 0.f;
	const float Sense = HearingMax != HearingMin
		? FMath::Clamp((Hearing - HearingMin) / (HearingMax - HearingMin), 0.f, 1.f) : 0.f;
	return FMath::Max(0.f, DistanceMaxUnits * (1.f - Skill + Sense));
}

bool FElysiumStealthKillRules::InDeafArc(const FElysiumPlayer& Player, const FElysiumNpc& Victim) const
{
	const float Yaw = FMath::DegreesToRadians(-float(Victim.Angles.Y));
	const FVector Rear(-FMath::Cos(Yaw), -FMath::Sin(Yaw), 0.0);
	FElysiumItem* Item = Player.Inventory.Active(Player);
	const FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
	if (!Weapon) return false;
	const FString& AttackFeat = Weapon->DamageForMode(Weapon->PrimaryModeIndex).AttackFeat;
	const int32 Rating = Player.CalcFeat(AttackFeat.Equals(TEXT("Close_Combat_Melee"), ESearchCase::IgnoreCase)
		? TEXT("Close_Combat_Melee") : TEXT("Close_Combat_Brawl"));
	return FVector::DotProduct((Player.Origin - Victim.Origin).GetSafeNormal(), Rear)
		> ArcDot(Rating);
}

bool FElysiumStealthKillRules::InDeafZone(const FElysiumPlayer& Player, const FElysiumNpc& Victim) const
{
	// 0x101be710: unlike FindVictim, hearing is suppressed OUTSIDE the minimum depth.
	const bool Eligible = Player.IsInStealthPosture();
	return Eligible && InDeafArc(Player, Victim)
		&& FVector::Dist(Player.Origin, Victim.Origin) > MinDepthUnits(
			Player.CalcFeat(TEXT("Sneaking")), Victim.Senses.Perception.HearingScalar) * ElysiumMove::U;
}

namespace
{
	FBox StealthKillStandHull(const FVector& FeetOriginCm)
	{
		const FVector Half(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.0f);
		return FBox(FeetOriginCm - Half,
			FeetOriginCm + Half + FVector(0.0f, 0.0f, ElysiumMove::StandHeight));
	}

	FElysiumNpc* TraceNpcHulls(FElysiumEntityWorld& World, const FVector& FromCm, const FVector& ToCm,
		const FElysiumEntityHandle& Ignore)
	{
		const FVector Delta = ToCm - FromCm;
		if (Delta.IsNearlyZero())
		{
			return nullptr;
		}
		FElysiumNpc* Best = nullptr;
		float BestT = 1.0f;
		for (const TUniquePtr<FElysiumEntity>& EntPtr : World.Entities())
		{
			FElysiumEntity* Ent = EntPtr.Get();
			FElysiumNpc* Npc = Ent ? Ent->AsNpc() : nullptr;
			if (!Npc || Npc->IsInert() || Npc->Handle == Ignore)
			{
				continue;
			}
			const FBox Hull = StealthKillStandHull(Npc->Origin);
			if (!FMath::LineBoxIntersection(Hull, FromCm, ToCm, Delta))
			{
				continue;
			}
			const FVector Closest = Hull.GetClosestPointTo(FromCm);
			const float T = FVector::DotProduct(Closest - FromCm, Delta) / Delta.SizeSquared();
			if (T < 0.0f || T > BestT)
			{
				continue;
			}
			BestT = T;
			Best = Npc;
		}
		return Best;
	}
}

FElysiumNpc* FElysiumStealthKillRules::FindVictim(FElysiumPlayer& Player) const
{
	FElysiumEntityWorld* World = Player.World;
	const double Now = World ? World->NowSeconds() : 0.0;
	if (CachedAt == Now)
	{
		FElysiumEntity* Cached = World ? World->Resolve(CachedVictim) : nullptr;
		return Cached ? Cached->AsNpc() : nullptr;
	}
	CachedVictim = FElysiumEntityHandle::Invalid();
	CachedAt = Now;

	if (!World || DistanceMaxUnits <= 0.f || !Player.CanAttemptStealthKill())
	{
		return nullptr;
	}

	const FVector From = Player.EyePosition();
	const FVector Dir = Player.BodyDirection2D();
	const FVector To = From + Dir * (DistanceMaxUnits * ElysiumMove::U);

	FElysiumNpc* Victim = nullptr;
	FElysiumEntityHandle Hit;
	const IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (Embodiment && Embodiment->TracePlayerSolid(From, To, Player.Handle, Hit))
	{
		FElysiumEntity* Ent = World->Resolve(Hit);
		Victim = Ent ? Ent->AsNpc() : nullptr;
	}
	else
	{
		Victim = TraceNpcHulls(*World, From, To, Player.Handle);
	}
	if (!Victim || !Victim->IsValidStealthKillTarget(Player))
	{
		return nullptr;
	}
	if (!InDeafArc(Player, *Victim) && !Victim->IsOblivious())
	{
		return nullptr;
	}
	// FindVictim passes literal position hint 1, before StartGrappleAttack derives its own hint.
	if (!Player.CanStartStealthKill(*Victim, DistanceMaxUnits, 1))
	{
		return nullptr;
	}

	CachedVictim = Victim->Handle;
	return Victim;
}
