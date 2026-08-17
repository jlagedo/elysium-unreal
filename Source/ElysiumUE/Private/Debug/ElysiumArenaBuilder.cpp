#include "Debug/ElysiumArenaBuilder.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumArena, Log, All);

namespace ElysiumArena
{

namespace
{
	// The anchors are addressable, and their names have to be stable across a restand: the ambient
	// selector holds a spot by INDEX into the entity list, so a rebuilt anchor under a new name
	// would leave a claiming NPC pointing at nothing. One prefix, the spec's own name after it.
	FString AnchorTargetName(const FName& SpecName)
	{
		return FString::Printf(TEXT("arena_%s"), *SpecName.ToString());
	}

	// The `type` an arena anchor wears. `interestingplacetypelist.txt` is the table
	// `FElysiumNpc::AmbientType` keys into, and a type it does not carry resolves to no row — which
	// makes the anchor unclaimable rather than broken. `Stand` is the plainest row the shipped table
	// authors, and it is what a body in cover should be doing.
	const TCHAR* AnchorType = TEXT("Stand");

	AActor* SpawnSolids(UWorld* World, const FSpec& Spec, const FVector& Origin, bool bWithMeshes)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}
#if WITH_EDITOR
		Actor->SetActorLabel(TEXT("ElysiumArena"));
#endif
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("ArenaRoot"));
		Actor->AddInstanceComponent(Root);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Origin);
		// Solids are Static so Recast will take them; a Movable root refuses that attach and
		// the room stands with no floor.
		Root->SetMobility(EComponentMobility::Static);

		UStaticMesh* Cube = bWithMeshes
			? LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")) : nullptr;
		if (bWithMeshes && Cube == nullptr)
		{
			UE_LOG(LogElysiumArena, Warning,
				TEXT("arena: the Engine cube would not load; standing collision-only"));
		}

		for (const FSolid& S : Spec.Solids)
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, S.Tag);
			Actor->AddInstanceComponent(Box);
			Box->SetupAttachment(Root);
			Box->SetBoxExtent(S.Extent, /*bUpdateOverlaps=*/false);
			Box->SetRelativeLocation(S.Center);
			// The same profile the map's own brush bodies use, so neither the mover nor an NPC
			// capsule can tell an arena solid from a `.hulls` collider.
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->SetGenerateOverlapEvents(false);
			// **The one line that separates this from the gym.** Recast reads the shapes that opt
			// in; without it the room has no navigable surface and the whole cast stands still.
			Box->SetCanEverAffectNavigation(true);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetHiddenInGame(!bWithMeshes);
			Box->RegisterComponent();

			if (Cube)
			{
				UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(
					Actor, FName(*FString::Printf(TEXT("%s_mesh"), *S.Tag.ToString())));
				Actor->AddInstanceComponent(Mesh);
				Mesh->SetupAttachment(Box);
				Mesh->SetStaticMesh(Cube);
				Mesh->SetMobility(EComponentMobility::Static);
				// The engine cube is 100 cm on a side and centred, so an extent maps to a scale by 50.
				Mesh->SetRelativeScale3D(S.Extent / 50.0f);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				// The decoration must not contribute a second, slightly different set of tiles.
				Mesh->SetCanEverAffectNavigation(false);
				Mesh->RegisterComponent();
			}
		}
		return Actor;
	}

	// `AElysiumMapActor::EnsureRuntimeNavigation`'s shape, over stated bounds instead of the
	// collision component's. It is not shared with that function on purpose: the map's path is
	// gated on `EElysiumCollisionBuildState::Ready`, a state a stage world never reaches, and
	// loosening that gate would let a map with no walkable surface request a build.
	AActor* BuildNavigation(UWorld* World, const FBox& WorldBounds, FString& OutError)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Navigation)
		{
			OutError = TEXT("this world has no navigation system");
			return nullptr;
		}
		if (!WorldBounds.IsValid)
		{
			OutError = TEXT("the arena reported no bounds to navigate");
			return nullptr;
		}

		const FVector Center = WorldBounds.GetCenter();
		FVector Extent = WorldBounds.GetExtent();
		// The same padding the map path uses: Recast needs room around the outermost collider, and
		// a volume flush with a wall drops the tile the wall stands on.
		Extent.X += 500.0f;
		Extent.Y += 500.0f;
		Extent.Z += 300.0f;

		const FTransform BoundsTransform(FRotator::ZeroRotator, Center);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(
			ANavMeshBoundsVolume::StaticClass(), BoundsTransform, Params);
		if (!Volume)
		{
			OutError = TEXT("could not create the arena's navigation bounds");
			return nullptr;
		}

		// A generated room carries no editor-authored brush, so a no-collision box contributes the
		// equivalent runtime bounds: the navigation system reads `GetComponentsBoundingBox` and
		// Recast projects the actual colliders inside it.
		UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Volume, TEXT("ElysiumArenaNavBounds"));
		BoundsBox->SetMobility(EComponentMobility::Static);
		BoundsBox->SetBoxExtent(Extent);
		BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoundsBox->SetCanEverAffectNavigation(false);
		BoundsBox->SetupAttachment(Volume->GetRootComponent());
		Volume->AddInstanceComponent(BoundsBox);
		Volume->FinishSpawning(BoundsTransform);
		if (!BoundsBox->IsRegistered())
		{
			BoundsBox->RegisterComponent();
		}

		Navigation->OnNavigationBoundsUpdated(Volume);
		Navigation->Build();
		UE_LOG(LogElysiumArena, Log, TEXT("arena: Recast build requested over %s"),
			*WorldBounds.ToString());
		return Volume;
	}

	// One `intersting_place`, spawned through the same runtime door `npc_maker` uses. Every value
	// it carries is an ordinary authored keyfield — nothing here reaches past the class registry.
	FElysiumEntityHandle SpawnAnchor(FElysiumEntityWorld& EntityWorld, const FAnchor& Anchor,
		const FVector& Origin)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("intersting_place");
		Def.TargetName = AnchorTargetName(Anchor.Name);
		Def.Origin = Origin + Anchor.FeetOrigin;
		Def.Keys.Add(TEXT("type"), AnchorType);
		Def.Keys.Add(TEXT("enabled"), TEXT("1"));
		Def.Keys.Add(TEXT("max_npcs"), TEXT("1"));
		// Group 0 is what an NPC with no `interesting_place_groups` allowlist accepts, which is
		// every character the arena spawns unless one is authored otherwise.
		Def.Keys.Add(TEXT("group_id"), TEXT("0"));
		Def.Keys.Add(TEXT("rating"), FString::FromInt(Anchor.Rating));
		Def.Keys.Add(TEXT("match_orientation"), TEXT("1"));
		Def.Keys.Add(TEXT("min_time"), TEXT("4"));
		Def.Keys.Add(TEXT("max_time"), TEXT("12"));
		Def.Keys.Add(TEXT("angles"), FString::Printf(TEXT("0 %.1f 0"), Anchor.Yaw));
		return EntityWorld.SpawnRuntimeEntity(MoveTemp(Def));
	}
}

bool Stand(UWorld* World, FElysiumEntityWorld* EntityWorld, const FSpec& Spec,
	const FVector& Origin, bool bWithMeshes, FStanding& Out, FString& OutError)
{
	Out = FStanding();
	if (!World)
	{
		OutError = TEXT("no world to stand an arena in");
		return false;
	}
	if (Spec.Solids.IsEmpty())
	{
		OutError = TEXT("the arena spec carries no solids");
		return false;
	}

	AActor* Solids = SpawnSolids(World, Spec, Origin, bWithMeshes);
	if (!Solids)
	{
		OutError = TEXT("could not spawn the arena's solids");
		return false;
	}
	Out.Solids = Solids;

	// Navigation is not optional and its failure is not recoverable here: an arena with no graph is
	// a room full of characters that cannot path, which reads as broken AI rather than as a missing
	// navmesh. Tear the solids back down so the caller is refused rather than half-served.
	const FBox WorldBounds = Spec.Bounds().ShiftBy(Origin);
	AActor* Volume = BuildNavigation(World, WorldBounds, OutError);
	if (!Volume)
	{
		Solids->Destroy();
		Out = FStanding();
		return false;
	}
	Out.NavigationBounds = Volume;

	if (EntityWorld != nullptr)
	{
		for (const FAnchor& Anchor : Spec.Anchors)
		{
			const FElysiumEntityHandle Handle = SpawnAnchor(*EntityWorld, Anchor, Origin);
			if (Handle.IsSet())
			{
				Out.Anchors.Add(Handle);
			}
			else
			{
				UE_LOG(LogElysiumArena, Warning, TEXT("arena: anchor %s would not spawn"),
					*Anchor.Name.ToString());
			}
		}
	}
	else
	{
		UE_LOG(LogElysiumArena, Warning,
			TEXT("arena: no entity world — standing the room with no interesting-place anchors"));
	}

	UE_LOG(LogElysiumArena, Log,
		TEXT("arena: %d solid(s), %d anchor(s), %d pad(s) at %s — navigation building"),
		Spec.Solids.Num(), Out.Anchors.Num(), Spec.Pads.Num(), *Origin.ToCompactString());
	return true;
}

void Teardown(FElysiumEntityWorld* EntityWorld, FStanding& Standing)
{
	if (EntityWorld != nullptr)
	{
		for (const FElysiumEntityHandle& Handle : Standing.Anchors)
		{
			if (FElysiumEntity* Anchor = EntityWorld->Resolve(Handle))
			{
				Anchor->Kill();
			}
		}
	}
	if (AActor* Volume = Standing.NavigationBounds.Get())
	{
		Volume->Destroy();
	}
	if (AActor* Solids = Standing.Solids.Get())
	{
		Solids->Destroy();
	}
	Standing = FStanding();
}

bool IsNavigationReady(const UWorld* World)
{
	// Non-const, and the const_cast is the reason this whole query is not a const operation:
	// `UNavigationSystemV1::IsNavigationBuildInProgress` is declared non-const even though it only
	// reads. The cast is confined here rather than pushed onto every caller.
	UNavigationSystemV1* Navigation = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World)) : nullptr;
	if (Navigation == nullptr)
	{
		return false;
	}
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Navigation->GetMainNavData());
	return Recast && Recast->GetNumActiveTiles() > 0 && !Navigation->IsNavigationBuildInProgress();
}

} // namespace ElysiumArena

#endif // !UE_BUILD_SHIPPING
