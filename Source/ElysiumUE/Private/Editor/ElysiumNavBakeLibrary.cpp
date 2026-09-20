#include "ElysiumNavBakeLibrary.h"

#include "AI/NavigationSystemBase.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Substrate/ElysiumRetailHullTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNavBake, Log, All);

namespace
{
	UNavigationSystemV1* NavSystem(UWorld* World)
	{
		return UNavigationSystemV1::GetCurrent(World);
	}
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
	return Report;
}
