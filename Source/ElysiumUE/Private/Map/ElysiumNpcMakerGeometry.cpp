#include "Map/ElysiumNpcMakerGeometry.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumUseIcons.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

float ElysiumNpcMakerGeometry::ResolveGroundZ(const UWorld* World, const FVector& MakerOriginCm,
	float TraceDepthCm, const AActor* Ignore)
{
	if (World == nullptr)
	{
		return MakerOriginCm.Z;
	}
	const FVector End = MakerOriginCm - FVector::UpVector * TraceDepthCm;
	// The start lifted by the movement solver's own surface epsilon (see the header): a floor the
	// origin sits on is met at the ray's start, as Source meets it at fraction 0.
	const FVector Start = MakerOriginCm + FVector::UpVector * ElysiumMove::DistEpsilon;
	FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerGround")), /*bTraceComplex*/ false);
	if (Ignore != nullptr)
	{
		Params.AddIgnoredActor(Ignore);
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ELYSIUM_USE_CHANNEL, Params))
	{
		return End.Z;   // no floor within 2048 units: `endpos` is the ray's end
	}
	// A hit between the lifted start and the origin is the surface the origin stands on (or is
	// embedded a hair into): Source answers the start, `origin.z`.
	return FMath::Min(Hit.ImpactPoint.Z, MakerOriginCm.Z);
}

bool ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(const FElysiumEntityWorld* EntityWorld,
	const AActor* PlayerPawn, const FVector& CentreCm, float HalfExtentCm, float FloorZCm)
{
	// `mins = (x - 34, y - 34, floor)`, `maxs = (x + 34, y + 34, origin.z)`; flat when the floor is the
	// origin's own Z (`CNPCMaker::CanMakeNPC`).
	const FBox SpawnArea(
		FVector(CentreCm.X - HalfExtentCm, CentreCm.Y - HalfExtentCm, FMath::Min(FloorZCm, CentreCm.Z)),
		FVector(CentreCm.X + HalfExtentCm, CentreCm.Y + HalfExtentCm, CentreCm.Z));
	// `FL_CLIENT`: the player, by its pawn's collision bounds.
	if (PlayerPawn != nullptr
		&& SpawnArea.Intersect(PlayerPawn->GetComponentsBoundingBox(/*bNonColliding*/ false)))
	{
		return true;
	}
	if (EntityWorld == nullptr)
	{
		return false;
	}
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		const FElysiumEntity* Ent = EntPtr.Get();
		if (Ent == nullptr || Ent->IsInert() || Ent->Handle == EntityWorld->PlayerHandle()
			|| (Ent->Flags & FlagNpc) == 0)
		{
			continue;
		}
		// `FL_NPC`: the NPC's collision hull, feet-anchored (`ElysiumMove`'s standing hull).
		const FVector Half(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.0f);
		const FBox Hull(Ent->Origin - Half,
			Ent->Origin + Half + FVector(0.0f, 0.0f, ElysiumMove::StandHeight));
		if (SpawnArea.Intersect(Hull))
		{
			return true;
		}
	}
	return false;
}
