#include "Debug/ElysiumArenaBuilder.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumMoveSolve.h"   // ElysiumMove::U, the one cm-per-unit constant
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumRetailHullTable.h"   // the `Human` agent the arena's mesh is cut for

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "UObject/EnumProperty.h"   // ANavigationData::RuntimeGeneration, set through reflection

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

	// `ANavigationData::RuntimeGeneration` is a protected config UPROPERTY with a getter and no
	// setter; the reflected property is the one door to it that is not a subclass. False when the
	// property is not where 5.8 declares it, so the caller can fall back.
	bool SetRuntimeGeneration(ANavigationData& Data, ERuntimeGenerationType Type)
	{
		const FEnumProperty* Property =
			FindFProperty<FEnumProperty>(ANavigationData::StaticClass(), TEXT("RuntimeGeneration"));
		if (Property == nullptr || Property->GetUnderlyingProperty() == nullptr)
		{
			return false;
		}
		Property->GetUnderlyingProperty()->SetIntPropertyValue(
			Property->ContainerPtrToValuePtr<void>(&Data), static_cast<int64>(Type));
		return Data.GetRuntimeGenerationMode() == Type;
	}

	// The arena's Recast mesh: the `Human` agent's (hull 0, what every arena character walks on),
	// found among the registered nav data or created the way `SpawnMissingNavigationDataInLevel`
	// creates a missing agent's -- `CreateNavigationDataInstanceInLevel`, then
	// `RequestRegistrationDeferred` (registered by the `Build()` that follows, which is also what
	// populates the nav octree). Made `Dynamic` BEFORE it registers: `ConditionalPopulateNavOctree`
	// decides whether the octree stores geometry from `DoesAnyNavDataRequireGeometryStorage` at that
	// moment, and the ini's `DynamicModifiersOnly` would have it skip the arena's solids.
	ARecastNavMesh* EnsureArenaNavMesh(UNavigationSystemV1& Navigation, FString& OutError)
	{
		const FName Agent = ElysiumRetailHulls::AgentName(0);
		ARecastNavMesh* Mesh = nullptr;
		for (ANavigationData* Data : Navigation.NavDataSet)
		{
			ARecastNavMesh* Candidate = Cast<ARecastNavMesh>(Data);
			if (Candidate != nullptr && Candidate->GetConfig().Name == Agent)
			{
				Mesh = Candidate;
				break;
			}
		}
		const bool bCreated = Mesh == nullptr;
		if (bCreated)
		{
			const FNavDataConfig* Config = nullptr;
			for (const FNavDataConfig& Supported : Navigation.GetSupportedAgents())
			{
				if (Supported.Name == Agent)
				{
					Config = &Supported;
					break;
				}
			}
			if (Config == nullptr)
			{
				OutError = FString::Printf(
					TEXT("the navigation system supports no '%s' agent to build the arena for"),
					*Agent.ToString());
				return nullptr;
			}
			Mesh = Cast<ARecastNavMesh>(Navigation.CreateNavigationDataInstanceInLevel(*Config, nullptr));
			if (Mesh == nullptr)
			{
				OutError = FString::Printf(TEXT("could not create the '%s' Recast mesh"),
					*Agent.ToString());
				return nullptr;
			}
		}
		if (!SetRuntimeGeneration(*Mesh, ERuntimeGenerationType::Dynamic))
		{
			// The engine's own recovery flag for runtime-spawned nav data: it opens the same geometry
			// gates (`SupportsRuntimeGeneration`, `DoesAnyNavDataRequireGeometryStorage`) for one
			// build, then clears itself.
			Mesh->MarkRequiresInitialRebuild();
			UE_LOG(LogElysiumArena, Warning,
				TEXT("arena: RuntimeGeneration not settable on %s; one-shot initial rebuild instead"),
				*Mesh->GetName());
		}
		if (bCreated)
		{
			Navigation.RequestRegistrationDeferred(*Mesh);
		}
		UE_LOG(LogElysiumArena, Log, TEXT("arena: %s Recast mesh %s (agent %s, runtime generation %d)"),
			bCreated ? TEXT("created") : TEXT("reusing"), *Mesh->GetName(), *Agent.ToString(),
			static_cast<int32>(Mesh->GetRuntimeGenerationMode()));
		return Mesh;
	}

	// Over stated bounds instead of the collision component's, and not shared with
	// `AElysiumMapActor::EnsureRuntimeNavigation` on purpose: the map's path adopts a baked mesh and
	// is gated on `EElysiumCollisionBuildState::Ready`, a state a stage world never reaches. What it
	// does share is the lock release, verbatim.
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
		if (EnsureArenaNavMesh(*Navigation, OutError) == nullptr)
		{
			return nullptr;
		}
		// `bInitialBuildingLocked` holds `ENavigationBuildLock::InitialLock` in every game world, and
		// `Build()` refuses under it ("Navigation NOT building because navigation build is locked
		// (flags: 0x8)"). The map actor releases it only after adopting a baked mesh; a stage has none.
		// `NoRebuild`, as there: the default action would run `RebuildAll` at the removal itself.
		Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::InitialLock,
			UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);

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

		// `Build()` registers the mesh queued above and populates the octree (geometry stored, the mesh
		// being Dynamic), then runs `RebuildAll`. The bounds request is queued and lands on the
		// navigation system's next tick (`PerformNavigationBoundsUpdate`), whose dirty areas the
		// Dynamic generator rasterises -- so tiles arrive a few frames later, and `IsNavigationReady`
		// is the poll.
		Navigation->OnNavigationBoundsUpdated(Volume);
		Navigation->Build();
		UE_LOG(LogElysiumArena, Log,
			TEXT("arena: Recast build requested over %s (initial lock released, build locked: %d)"),
			*WorldBounds.ToString(), Navigation->IsNavigationBuildingLocked() ? 1 : 0);
		return Volume;
	}

	// One `intersting_place` (`AnchorRow`), spawned through the same runtime door `npc_maker` uses.
	FElysiumEntityHandle SpawnAnchor(FElysiumEntityWorld& EntityWorld, const FAnchor& Anchor,
		const FVector& Origin)
	{
		return EntityWorld.SpawnRuntimeEntity(AnchorRow(Anchor, Origin));
	}

	// The node classname a cover type is authored under. The class-forced type
	// (`ElysiumNodeEntity::ClassHintType`, `FUN_102d7d30`) is what `CNodeEnt::Spawn` decides on, and
	// plain `info_node` forces 0 -- it would make no hint. The cover classnames force exactly the
	// type the arena authors; `info_node_hint` forces nothing and keeps an authored type, for any
	// other.
	const TCHAR* NodeClassname(int32 HintType)
	{
		switch (HintType)
		{
		case 100:                 return TEXT("info_node_cover_med");
		case HintTypeCoverLow:    return TEXT("info_node_cover_low");
		case HintTypeCoverCorner: return TEXT("info_node_cover_corner");
		default:                  return TEXT("info_node_hint");
		}
	}

	// The class word `CAI_Hint::Spawn` (`0x102d0b60`) derives for a cover type: 1 for all three,
	// which is what the cover search's mask 1 admits.
	int32 ExpectedClassMask(int32 HintType)
	{
		return (HintType == 100 || HintType == HintTypeCoverLow || HintType == HintTypeCoverCorner)
			? 1 : 0;
	}

	// Kill every live entity whose targetname is a spec node's name.
	int32 KillNodesByName(FElysiumEntityWorld& EntityWorld, const FSpec& Spec)
	{
		int32 Killed = 0;
		for (const TUniquePtr<FElysiumEntity>& Entity : EntityWorld.Entities())
		{
			if (Entity && !Entity->IsDead() && Spec.FindNode(Entity->TargetName) != nullptr)
			{
				Entity->Kill();
				++Killed;
			}
		}
		return Killed;
	}

	// One node row exactly as a baked map carries it (`NodeRow`), then the runtime door.
	// `SpawnRuntimeEntity` runs `CNodeEnt::Spawn`'s loaded arm at creation
	// (`CreateRuntimeEntityNoSpawn` → `ElysiumNodeEntity::SpawnNodeRow`), the same arm `Load` runs
	// over a map's node rows.
	FElysiumEntityHandle SpawnNode(FElysiumEntityWorld& EntityWorld, const FNode& Node,
		int32 NodeIndex, const FVector& Origin)
	{
		return EntityWorld.SpawnRuntimeEntity(NodeRow(Node, NodeIndex, Origin));
	}
}

FElysiumEntityDef AuthoredRow(const TCHAR* Classname, const FString& TargetName,
	const FVector& OriginCm, float YawDeg)
{
	// `UElysiumMapEntities::Deserialize`'s shape: `Origin` in Unreal centimetres beside the raw
	// keyvalues, which still hold the authored `origin` in Source inches with Y negated back
	// (`formats/bsp.py` `source_to_unreal`, inverted — the same inversion
	// `AElysiumInfraActor` writes) and `angles` in Source degrees.
	FElysiumEntityDef Def;
	Def.Classname = Classname;
	Def.TargetName = TargetName;
	Def.Origin = OriginCm;
	const double Inches = 1.0 / ElysiumMove::U;
	Def.Keys.Add(TEXT("origin"), FString::Printf(TEXT("%.9g %.9g %.9g"),
		OriginCm.X * Inches, -OriginCm.Y * Inches, OriginCm.Z * Inches));
	Def.Keys.Add(TEXT("angles"), FString::Printf(TEXT("0 %.9g 0"), -YawDeg));
	return Def;
}

FElysiumEntityDef AnchorRow(const FAnchor& Anchor, const FVector& Origin)
{
	// Every value it carries is an ordinary authored keyfield — nothing here reaches past the class
	// registry.
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
	return Def;
}

FElysiumEntityDef NodeRow(const FNode& Node, int32 NodeIndex, const FVector& Origin)
{
	FElysiumEntityDef Def = AuthoredRow(NodeClassname(Node.HintType), Node.Name,
		Origin + Node.FeetCm, Node.YawDeg);
	// The hint parses the row's own keys (`ApplyHintReplacement` keeps them), so the AUTHORED
	// type lands on `m_nHintType` beside the class-forced one that decided the spawn.
	Def.Keys.Add(TEXT("hinttype"), FString::FromInt(Node.HintType));
	Def.Keys.Add(TEXT("group_id"), FString::FromInt(Node.GroupId));
	// `m_nWCNodeID` (`CNodeEnt +0x454`), provenance only: the counter, not this key, gives the
	// hint its node. Authored because a map row carries it.
	Def.Keys.Add(TEXT("nodeid"), FString::FromInt(NodeIndex));
	return Def;
}

TArray<FElysiumPlaceRow> NodePlaceRows(const FSpec& Spec, const FVector& Origin)
{
	TArray<FElysiumPlaceRow> Rows;
	Rows.Reserve(Spec.Nodes.Num());
	for (int32 Index = 0; Index < Spec.Nodes.Num(); ++Index)
	{
		const FNode& Node = Spec.Nodes[Index];
		FElysiumPlaceRow Row;
		Row.NetworkIndex = Index;
		Row.Type = 2;                                // ground: `GetPosition` adds `zoffset[hull]`, all 0
		Row.OriginCm = Origin + Node.FeetCm;
		Row.YawDeg = Node.YawDeg;                    // Unreal-native, as the row carries it
		Row.HintBspIndex = INDEX_NONE;
		Rows.Add(Row);
	}
	return Rows;
}

int32 StandCoverNetwork(FElysiumEntityWorld& EntityWorld, const FSpec& Spec,
	const FVector& Origin, TArray<FElysiumEntityHandle>& OutNodes)
{
	OutNodes.Reset();
	const int32 Replaced = KillNodesByName(EntityWorld, Spec);

	// The network first, and whole, before any node row spawns: the row index is the node id.
	FElysiumPlaceSet& Places = EntityWorld.Places();
	Places.AdoptRows(NodePlaceRows(Spec, Origin));
	Places.BeginMapSpawn();

	for (int32 Index = 0; Index < Spec.Nodes.Num(); ++Index)
	{
		const FNode& Node = Spec.Nodes[Index];
		const FElysiumEntityHandle Handle = SpawnNode(EntityWorld, Node, Index, Origin);
		const FElysiumHint* Hint = FElysiumHint::Cast(EntityWorld.Resolve(Handle));
		if (Hint == nullptr)
		{
			UE_LOG(LogElysiumArena, Warning,
				TEXT("arena: cover node %s (type %d) made no ai_hint"), *Node.Name, Node.HintType);
			continue;
		}
		OutNodes.Add(Handle);
		const bool bBound = Hint->NodeId == Index && Places.AttachedHint(Index) == Handle;
		const bool bMask = Hint->ClassMask == ExpectedClassMask(Node.HintType);
		UE_LOG(LogElysiumArena, Log,
			TEXT("arena: cover node %-16s row %d  entity %d  node id %d  class mask %d  type %d%s"),
			*Node.Name, Index, Handle.Index, Hint->NodeId, Hint->ClassMask, Hint->HintType,
			bBound && bMask ? TEXT("") : TEXT("  <-- MISBOUND"));
		if (!bBound || !bMask)
		{
			UE_LOG(LogElysiumArena, Warning,
				TEXT("arena: cover node %s expected node id %d / class mask %d"),
				*Node.Name, Index, ExpectedClassMask(Node.HintType));
		}
	}
	UE_LOG(LogElysiumArena, Log,
		TEXT("arena: AI network of %d node(s) adopted, %d cover hint(s) standing (%d replaced)"),
		Places.NumNodes(), OutNodes.Num(), Replaced);
	return OutNodes.Num();
}

int32 ClearCoverNetwork(FElysiumEntityWorld& EntityWorld, const FSpec& Spec)
{
	const int32 Killed = KillNodesByName(EntityWorld, Spec);
	// Back to what a stage world starts with: a network of zero nodes.
	EntityWorld.Places().AdoptRows(TArray<FElysiumPlaceRow>());
	EntityWorld.Places().BeginMapSpawn();
	return Killed;
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
		// After the anchors and independent of them: the cover nodes are the AI network's, the
		// anchors the ambient selector's, and neither reads the other.
		StandCoverNetwork(*EntityWorld, Spec, Origin, Out.Nodes);
	}
	else
	{
		UE_LOG(LogElysiumArena, Warning,
			TEXT("arena: no entity world — standing the room with no anchors and no cover nodes"));
	}

	UE_LOG(LogElysiumArena, Log,
		TEXT("arena: %d solid(s), %d anchor(s), %d cover node(s), %d pad(s) at %s — navigation building"),
		Spec.Solids.Num(), Out.Anchors.Num(), Out.Nodes.Num(), Spec.Pads.Num(),
		*Origin.ToCompactString());
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
		for (const FElysiumEntityHandle& Handle : Standing.Nodes)
		{
			if (FElysiumEntity* Node = EntityWorld->Resolve(Handle))
			{
				Node->Kill();
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
	if (Navigation->IsNavigationBuildInProgress())
	{
		return false;
	}
	for (const ANavigationData* Data : Navigation->NavDataSet)
	{
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
		if (Recast != nullptr && Recast->GetNumActiveTiles() > 0)
		{
			return true;
		}
	}
	return false;
}

} // namespace ElysiumArena

#endif // !UE_BUILD_SHIPPING
