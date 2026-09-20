#include "ElysiumNavBakeLibrary.h"

#include "AI/NavigationSystemBase.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "ElysiumWorldCollisionActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNavBake, Log, All);

namespace
{
	UNavigationSystemV1* NavSystem(UWorld* World)
	{
		return UNavigationSystemV1::GetCurrent(World);
	}

	/** The hull whose agent carries this name, or INDEX_NONE. */
	int32 HullForAgent(const FName AgentName)
	{
		for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
		{
			if (ElysiumRetailHulls::AgentName(Hull) == AgentName)
			{
				return Hull;
			}
		}
		return INDEX_NONE;
	}

	/** Retail's AI has no slope term at all -- its ground move clamps only on step height and its
	 *  stand test reads no surface normal. A rasterised mesh must answer what retail never asked,
	 *  so it takes the player movement layer's own standable normal, 0.7 at 0x104492d0. */
	float SlopeDegrees()
	{
		return FMath::RadiansToDegrees(FMath::Acos(0.7f));
	}
}

FBox UElysiumNavBakeLibrary::NavigationBoundsOf(UWorld* World)
{
	FBox Box(ForceInit);
	if (World == nullptr)
	{
		return Box;
	}
	// The WORLD COLLISION's own bodies, not every navigation-relevant primitive in the level.
	//
	// The union of all of them is what the map is solid against plus whatever else happens to
	// block a pawn, and on `sm_hub_1` one of its 387 relevant components carries world-sized
	// bounds: the union came out +/-500,000 cm, a 10 km cube, against a map that is 290 m across.
	// Recast was then asked for 3,084,588 tiles and clamped. The brushes the map is built from
	// ARE the playable volume -- every prop stands inside them -- so they are what the mesh is
	// cut over, and this is the same volume the run-time path takes from
	// `UElysiumMapCollision::GetWorldBounds`.
	for (TActorIterator<AElysiumWorldCollisionActor> It(World); It; ++It)
	{
		for (const UElysiumWorldCollisionComponent* Component : It->Bodies)
		{
			if (Component)
			{
				Box += Component->Bounds.GetBox();
			}
		}
	}
	if (Box.IsValid)
	{
		// The same margin the run-time path uses, so a baked mesh and a generated one cover the
		// same volume and are comparable.
		Box = Box.ExpandBy(FVector(500.0, 500.0, 300.0));
	}
	return Box;
}

int32 UElysiumNavBakeLibrary::CountNavigationRelevantComponents(UWorld* World)
{
	int32 Count = 0;
	if (World == nullptr)
	{
		return Count;
	}
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Component = *It;
		if (Component && Component->GetWorld() == World && Component->IsRegistered()
			&& Component->IsNavigationRelevant())
		{
			++Count;
		}
	}
	return Count;
}

TArray<FString> UElysiumNavBakeLibrary::SupportedAgentNames(UWorld* World)
{
	TArray<FString> Names;
	if (const UNavigationSystemV1* Nav = NavSystem(World))
	{
		for (const FNavDataConfig& Agent : Nav->GetSupportedAgents())
		{
			Names.Add(Agent.Name.ToString());
		}
	}
	return Names;
}

TArray<FString> UElysiumNavBakeLibrary::AgentNamesForHullBits(int32 HullBits)
{
	TArray<FString> Names;
	for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
	{
		if ((HullBits & (1 << Hull)) == 0)
		{
			continue;
		}
		const FName AgentName = ElysiumRetailHulls::AgentName(Hull);
		if (!AgentName.IsNone())
		{
			Names.Add(AgentName.ToString());
		}
	}
	return Names;
}

TArray<FString> UElysiumNavBakeLibrary::NavMeshTileCounts(UWorld* World)
{
	TArray<FString> Rows;
	// The world's own actors, not the navigation system's NavDataSet: this is asked of a level
	// that has just been loaded in the editor, where the set may not be populated yet.
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		const UEnum* Modes = StaticEnum<ENavDataGatheringModeConfig>();
		Rows.Add(FString::Printf(TEXT("%s=%d gen=%d"), *It->GetName(), It->GetNumActiveTiles(),
			static_cast<int32>(It->GetRuntimeGenerationMode())));
	}
	Rows.Sort();
	return Rows;
}

TArray<FString> UElysiumNavBakeLibrary::SetMapNavAgents(UWorld* World, int32 HullBits)
{
	TArray<FString> Kept;
	UNavigationSystemV1* Nav = NavSystem(World);
	if (Nav == nullptr)
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("no navigation system in this world"));
		return Kept;
	}

	// `UsedHullBits` names hulls; the project's SupportedAgents name agents. The hull table is what
	// ties the two together, and it is generated from the same recovered rows the ini is, so a
	// disagreement here is a generator that was not re-run.
	const TArray<FNavDataConfig>& Agents = Nav->GetSupportedAgents();
	TMap<FName, int32> IndexByName;
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		IndexByName.Add(Agents[Index].Name, Index);
	}

	FNavAgentSelector Mask;
	Mask.Empty();
	for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
	{
		if ((HullBits & (1 << Hull)) == 0)
		{
			continue;
		}
		const FName AgentName = ElysiumRetailHulls::AgentName(Hull);
		if (AgentName.IsNone())
		{
			// A hull the graph declares but no shipped link uses -- nothing can path on a mesh cut
			// for it, so no agent is declared for it either.
			UE_LOG(LogElysiumNavBake, Warning,
				TEXT("hull %d (%s) is declared by this map's graph but carries no links anywhere; "
					"no agent is built for it"),
				Hull, ElysiumRetailHulls::Find(Hull) ? ElysiumRetailHulls::Find(Hull)->Name
					: TEXT("?"));
			continue;
		}
		const int32* Index = IndexByName.Find(AgentName);
		if (Index == nullptr)
		{
			UE_LOG(LogElysiumNavBake, Error,
				TEXT("hull %d wants agent '%s', which DefaultEngine.ini does not declare; "
					"re-run `elysium research gen_hull_table`"), Hull, *AgentName.ToString());
			continue;
		}
		Mask.Set(*Index);
		Kept.Add(AgentName.ToString());
	}

	if (Kept.IsEmpty())
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("hull bits %#x name no supported agent"), HullBits);
		return Kept;
	}

	// The mask is only honoured once it says it has been set at all.
	Mask.MarkInitialized();
	Nav->SetSupportedAgentsMask(Mask);
	UE_LOG(LogElysiumNavBake, Log, TEXT("map nav agents from UsedHullBits %#x: %s"),
		HullBits, *FString::Join(Kept, TEXT(", ")));
	return Kept;
}

TArray<FString> UElysiumNavBakeLibrary::CreateNavigationForAgents(UWorld* World, int32 HullBits)
{
	TArray<FString> Kept;
	if (World == nullptr)
	{
		return Kept;
	}

	// The mask is resolved against the PROJECT's supported agents, which are readable without a
	// navigation system: they are this class's own config.
	const UNavigationSystemV1* Defaults = GetDefault<UNavigationSystemV1>();
	const TArray<FNavDataConfig>& Agents = Defaults->GetSupportedAgents();
	FNavAgentSelector Mask;
	Mask.Empty();
	for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
	{
		if ((HullBits & (1 << Hull)) == 0)
		{
			continue;
		}
		const FName AgentName = ElysiumRetailHulls::AgentName(Hull);
		for (int32 Index = 0; Index < Agents.Num(); ++Index)
		{
			if (Agents[Index].Name == AgentName)
			{
				Mask.Set(Index);
				Kept.Add(AgentName.ToString());
				break;
			}
		}
	}
	if (Kept.IsEmpty())
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("hull bits %#x name no supported agent"), HullBits);
		return Kept;
	}
	Mask.MarkInitialized();

	// Built FROM the mask, not masked after the fact.
	UNavigationSystemModuleConfig* Config = NewObject<UNavigationSystemModuleConfig>(World);
	Config->SupportedAgentsMask = Mask;
	FNavigationSystem::AddNavigationSystemToWorld(
		*World, FNavigationSystemRunMode::EditorMode, Config,
		/*bInitializeForWorld*/ true, /*bOverridePreviousNavSys*/ true);

	UNavigationSystemV1* Nav = NavSystem(World);
	if (Nav == nullptr)
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("could not create a navigation system"));
		return TArray<FString>();
	}
	Nav->SetSupportedAgentsMask(Mask);
	UE_LOG(LogElysiumNavBake, Log, TEXT("navigation for UsedHullBits %#x: %s"),
		HullBits, *FString::Join(Kept, TEXT(", ")));
	return Kept;
}

bool UElysiumNavBakeLibrary::PlaceNavBounds(UWorld* World, const FBox& BoundsCm)
{
	UNavigationSystemV1* Nav = NavSystem(World);
	if (Nav == nullptr || !BoundsCm.IsValid || BoundsCm.GetSize().IsNearlyZero())
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("no navigation system, or empty nav bounds"));
		return false;
	}

	// A bounds volume is a BRUSH actor: without a brush built into it the volume has no shape and
	// the navigation system takes no bounds from it at all.
	ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(
		BoundsCm.GetCenter(), FRotator::ZeroRotator);
	if (Volume == nullptr)
	{
		return false;
	}
	const FVector Extent = BoundsCm.GetExtent();
	UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume);
	Builder->X = Extent.X * 2.0;
	Builder->Y = Extent.Y * 2.0;
	Builder->Z = Extent.Z * 2.0;
	Volume->PreEditChange(nullptr);
	Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
	Volume->Brush->Initialize(nullptr, true);
	Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, RF_Transactional);
	Volume->GetBrushComponent()->Brush = Volume->Brush;
	Builder->Build(World, Volume);
	Volume->PostEditChange();

	Nav->OnNavigationBoundsUpdated(Volume);
	UE_LOG(LogElysiumNavBake, Log, TEXT("nav bounds %s"), *BoundsCm.ToString());
	return true;
}

TArray<FElysiumNavAgentBuild> UElysiumNavBakeLibrary::BuildAgentNavMeshes(UWorld* World)
{
	TArray<FElysiumNavAgentBuild> Report;
	UNavigationSystemV1* Nav = NavSystem(World);
	if (Nav == nullptr)
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("no navigation system in this world"));
		return Report;
	}

	// A level opened through the editor's loader leaves an `AsyncLoadLock` on the navigation
	// system, and `Build` declines SILENTLY while any lock is held -- it logs "Navigation NOT
	// building because navigation build is locked" and returns, so the bake would save an empty
	// mesh and report success. Release what a loaded-but-not-interactive world has no use for,
	// then refuse to continue if anything still holds it.
	Nav->RemoveNavigationBuildLock(
		ENavigationBuildLock::AsyncLoadLock | ENavigationBuildLock::InitialLock
			| ENavigationBuildLock::NoUpdateInEditor,
		UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	// NOT `ReleaseInitialBuildingLock`: it spawns missing navigation data for every supported
	// agent on its way out, which puts thirteen empty meshes in a level that named two. Removing
	// the lock flags directly, with NoRebuild, leaves the spawn to the mask.
	if (Nav->IsNavigationBuildingLocked())
	{
		UE_LOG(LogElysiumNavBake, Error,
			TEXT("navigation is still locked; a build now would silently do nothing"));
		return Report;
	}

	// Cell and tile size are RecastNavMesh properties, not FNavDataConfig ones, so the ini's
	// SupportedAgents cannot carry them: without this every mesh takes the engine default cell
	// and a map the size of the hub asks Recast for more tiles than it will allocate.
	for (ANavigationData* Data : Nav->NavDataSet)
	{
		ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Data);
		if (Mesh == nullptr)
		{
			continue;
		}
		const int32 Hull = HullForAgent(Mesh->GetConfig().Name);
		const float Cell = Hull >= 0 ? ElysiumRetailHulls::AgentCellSize(Hull) : 0.0f;
		if (Cell <= 0.0f)
		{
			continue;
		}
		for (uint8 Index = 0; Index < static_cast<uint8>(ENavigationDataResolution::MAX); ++Index)
		{
			const ENavigationDataResolution Resolution =
				static_cast<ENavigationDataResolution>(Index);
			Mesh->SetCellSize(Resolution, Cell);
			Mesh->SetCellHeight(Resolution, Cell * 0.5f);
			Mesh->SetAgentMaxStepHeight(Resolution, ElysiumRetailHulls::StepHeightUnits * 2.54f);
		}
		Mesh->TileSizeUU = ElysiumRetailHulls::AgentTileSize(Hull);
		Mesh->AgentMaxSlope = SlopeDegrees();
		// The grid Recast will be asked for, stated before it asks: this is the number that
		// exceeded its own limit when the tile size was left at the engine default.
		const FBox Bounds = NavigationBoundsOf(World);
		if (Bounds.IsValid && Mesh->TileSizeUU > 0.0f)
		{
			const FVector Size = Bounds.GetSize();
			const double Tiles = FMath::CeilToDouble(Size.X / Mesh->TileSizeUU)
				* FMath::CeilToDouble(Size.Y / Mesh->TileSizeUU);
			UE_LOG(LogElysiumNavBake, Log,
				TEXT("agent '%s': cell %.1f, tile %.0f, bounds %.0f x %.0f x %.0f -> %.0f tiles"),
				*Mesh->GetConfig().Name.ToString(), Cell, Mesh->TileSizeUU,
				Size.X, Size.Y, Size.Z, Tiles);
		}
	}

	const double Started = FPlatformTime::Seconds();
	// Synchronous: `Build` ends in `EnsureBuildCompletion`, which is what makes saving the level
	// straight afterwards mean anything.
	Nav->Build();
	const double Elapsed = FPlatformTime::Seconds() - Started;

	for (ANavigationData* Data : Nav->NavDataSet)
	{
		ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Data);
		if (Mesh == nullptr)
		{
			continue;
		}
		FElysiumNavAgentBuild Row;
		Row.Agent = Mesh->GetConfig().Name.ToString();
		Row.Tiles = Mesh->GetNumActiveTiles();
		Row.Bytes = Mesh->GetCompressedTileCacheSize();
		Row.Seconds = static_cast<float>(Elapsed);
		Row.AgentRadius = Mesh->GetConfig().AgentRadius;
		Row.AgentHeight = Mesh->GetConfig().AgentHeight;
		Report.Add(Row);
		UE_LOG(LogElysiumNavBake, Log,
			TEXT("agent '%s' (r %.2f h %.2f): %d tile(s), %d byte(s)"),
			*Row.Agent, Row.AgentRadius, Row.AgentHeight, Row.Tiles, Row.Bytes);
	}
	if (Report.IsEmpty())
	{
		UE_LOG(LogElysiumNavBake, Error, TEXT("the build produced no Recast mesh"));
	}

	// `Build` spawns missing navigation data for every SUPPORTED agent, mask or no mask, so a
	// level that named two agents ends up holding fourteen -- twelve of them empty. An empty mesh
	// saved in a level is not harmless: the runtime reads any saved mesh as "already built", and
	// each one asks Recast for a tile grid it then clamps, an error per agent per load. Drop them
	// here, where the set that was actually wanted is still in hand.
	// Over the WORLD's actors, not `NavDataSet`: the spawned-but-empty meshes are in the level
	// without being in the registered set, so a pass over the set alone finds none of them and
	// they reach the save anyway.
	TArray<ARecastNavMesh*> Unwanted;
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		if (It->GetNumActiveTiles() == 0)
		{
			Unwanted.Add(*It);
		}
	}
	for (ARecastNavMesh* Mesh : Unwanted)
	{
		UE_LOG(LogElysiumNavBake, Log, TEXT("dropping empty mesh for agent '%s'"),
			*Mesh->GetConfig().Name.ToString());
		Nav->UnregisterNavData(Mesh);
		Mesh->Destroy();
	}

	return Report;
}
