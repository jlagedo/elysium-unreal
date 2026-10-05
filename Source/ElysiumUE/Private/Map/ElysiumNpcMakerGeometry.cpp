#include "Map/ElysiumNpcMakerGeometry.h"

#include "ElysiumCollisionChannels.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumUseIcons.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/IConsoleManager.h"

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
	// A start inside a solid reports `bStartPenetrating` — Source's `startsolid`, whose `endpos` is the
	// start.
	Params.bFindInitialOverlaps = true;
	if (Ignore != nullptr)
	{
		Params.AddIgnoredActor(Ignore);
	}
	// `0x2400b` asks whether a BRUSH blocks an NPC; the port's answer to that is `ECC_Pawn`
	// (`ElysiumCollisionChannels.h`: the contents-signature profiles block it exactly where retail's
	// NPC-solid contents do). `CONTENTS_MONSTER` is not in the mask, so a hit on a character body
	// (object type Pawn, or the player's channel) is skipped and the ray re-cast past it.
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, GroundChannel, Params))
		{
			return End.Z;   // no floor within 2048 units: `endpos` is the ray's end
		}
		const UPrimitiveComponent* Component = Hit.GetComponent();
		const ECollisionChannel ObjectType =
			Component != nullptr ? Component->GetCollisionObjectType() : ECC_WorldStatic;
		if ((ObjectType == ECC_Pawn || ObjectType == ElysiumCollision::PlayerChannel)
			&& Hit.GetActor() != nullptr)
		{
			Params.AddIgnoredActor(Hit.GetActor());
			continue;
		}
		if (Hit.bStartPenetrating)
		{
			return MakerOriginCm.Z;   // `startsolid`: `endpos` is the start, the maker's own Z
		}
		// A hit between the lifted start and the origin is the surface the origin stands on:
		// Source answers the start, `origin.z`.
		return FMath::Min(Hit.ImpactPoint.Z, MakerOriginCm.Z);
	}
	return End.Z;
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

FString ElysiumNpcMakerGeometry::DescribeSpawnArea(const FElysiumEntityWorld* EntityWorld,
    const AActor* PlayerPawn, const FVector& CentreCm, float HalfExtentCm, float FloorZCm)
{
    const FBox Box(FVector(CentreCm.X - HalfExtentCm, CentreCm.Y - HalfExtentCm, FMath::Min(FloorZCm, CentreCm.Z)),
        FVector(CentreCm.X + HalfExtentCm, CentreCm.Y + HalfExtentCm, CentreCm.Z));
    TArray<FString> Rows;
    if (!EntityWorld) return TEXT("candidates=unavailable:no entity world");
    for (const auto& Entry : EntityWorld->Entities())
    {
        const FElysiumEntity* Candidate = Entry.Get();
        if (!Candidate) continue;
        const bool bPlayer = Candidate->Handle == EntityWorld->PlayerHandle();
        if (!bPlayer && (Candidate->Flags & SpawnAreaFlagMask) == 0) continue;
        if (bPlayer && !PlayerPawn)
        {
            Rows.Add(FString::Printf(TEXT("candidate=#%d flags=0x%x alive=%d life=%d bounds=unavailable:no player pawn"),
                Candidate->Handle.Index, Candidate->Flags, Candidate->LifeState == ElysiumLifeState::Alive ? 1 : 0, Candidate->LifeState));
            continue;
        }
        const FVector Half(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.f);
        const FBox Bounds = bPlayer ? PlayerPawn->GetComponentsBoundingBox(false)
            : FBox(Candidate->Origin - Half, Candidate->Origin + Half + FVector(0.f, 0.f, ElysiumMove::StandHeight));
        Rows.Add(FString::Printf(TEXT("candidate=#%d flags=0x%x alive=%d life=%d inert=%d origin=%s min=%s max=%s intersects=%d"),
            Candidate->Handle.Index, Candidate->Flags, Candidate->LifeState == ElysiumLifeState::Alive ? 1 : 0, Candidate->LifeState,
            Candidate->IsInert() ? 1 : 0, *Candidate->Origin.ToString(), *Bounds.Min.ToString(), *Bounds.Max.ToString(), Box.Intersect(Bounds) ? 1 : 0));
    }
    return FString::Printf(TEXT("candidates=%d %s"), Rows.Num(), *FString::Join(Rows, TEXT(";")));
}

#if !UE_BUILD_SHIPPING
// `elysium.npcmaker.groundprobe X Y Z [depthcm]` — the ground ray of `ResolveGroundZ` at a point, run
// against every channel the world's collision profiles distinguish, with and without initial
// overlaps, each hit named by its component's profile. Diagnostic only; it changes nothing.
DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpcMakerGeometry, Log, All);

static FAutoConsoleCommandWithWorldAndArgs GElysiumNpcMakerGroundProbeCmd(
	TEXT("elysium.npcmaker.groundprobe"),
	TEXT("elysium.npcmaker.groundprobe X Y Z [depthcm] — the maker ground ray on each channel"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 3)
		{
			UE_LOG(LogElysiumNpcMakerGeometry, Warning, TEXT("usage: elysium.npcmaker.groundprobe X Y Z [depthcm]"));
			return;
		}
		const FVector Origin(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		const double Depth = Args.Num() >= 4 ? FCString::Atod(*Args[3]) : 2048.0 * ElysiumMove::U;
		struct FChannelRow { const TCHAR* Name; ECollisionChannel Channel; };
		const FChannelRow Channels[] = {
			{ TEXT("ElysiumUse"), ELYSIUM_USE_CHANNEL },
			{ TEXT("Pawn"), ECC_Pawn },
			{ TEXT("WorldStatic"), ECC_WorldStatic },
			{ TEXT("Visibility"), ECC_Visibility },
			{ TEXT("ElysiumPlayer"), ECC_GameTraceChannel4 },
		};
		for (const FChannelRow& Row : Channels)
		{
			for (int32 Pass = 0; Pass < 4; ++Pass)
			{
				const bool bInitial = (Pass & 1) != 0;
				const bool bLifted = (Pass & 2) != 0;
				FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerGroundProbe")), false);
				Params.bFindInitialOverlaps = bInitial;
				const FVector Start = Origin + FVector::UpVector * (bLifted ? ElysiumMove::DistEpsilon : 0.0f);
				const FVector End = Origin - FVector::UpVector * Depth;
				FHitResult Hit;
				const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Row.Channel, Params);
				const UPrimitiveComponent* Comp = Hit.GetComponent();
				UE_LOG(LogElysiumNpcMakerGeometry, Display,
					TEXT("groundprobe %-13s initial=%d lifted=%d -> hit=%d startpen=%d z=%.3f comp=%s profile=%s"),
					Row.Name, bInitial ? 1 : 0, bLifted ? 1 : 0, bHit ? 1 : 0,
					Hit.bStartPenetrating ? 1 : 0, bHit ? Hit.ImpactPoint.Z : End.Z,
					Comp != nullptr ? *Comp->GetPathName() : TEXT("-"),
					Comp != nullptr ? *Comp->GetCollisionProfileName().ToString() : TEXT("-"));
			}
		}
	}));
#endif
