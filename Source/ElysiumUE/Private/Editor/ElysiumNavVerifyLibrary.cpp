#include "ElysiumNavVerifyLibrary.h"

#include "AI/NavigationSystemBase.h"
#include "EngineUtils.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNavVerify, Log, All);

namespace
{
	//: A mesh is found by the agent NAME its config carries, not by index: a map builds only the
	//: agents its own graph's `UsedHullBits` names, so the project's ordering says nothing about
	//: which of them this level holds.
	ARecastNavMesh* MeshFor(UWorld* World, const FString& AgentName)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
		{
			if (It->GetConfig().Name.ToString() == AgentName)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

TArray<double> UElysiumNavVerifyLibrary::PathLengths(UWorld* World, const FString& AgentName,
	const TArray<FVector>& StartsCm, const TArray<FVector>& EndsCm,
	const FVector& ProjectExtentCm)
{
	TArray<double> Lengths;
	Lengths.Init(-2.0, FMath::Min(StartsCm.Num(), EndsCm.Num()));

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ARecastNavMesh* Mesh = MeshFor(World, AgentName);
	if (Nav == nullptr || Mesh == nullptr)
	{
		UE_LOG(LogElysiumNavVerify, Error, TEXT("no mesh for agent '%s' in this level"),
			*AgentName);
		return Lengths;
	}

	// The mesh's own default filter: the port declares no query filter of its own yet -- the
	// pedestrian one that prices story 6's roadway is story 5's -- so a verify path is the path an
	// ordinary body would take.
	const FSharedConstNavQueryFilter Filter = Mesh->GetDefaultQueryFilter();
	for (int32 Index = 0; Index < Lengths.Num(); ++Index)
	{
		// Project first, and say so separately. "The mesh does not reach this endpoint" and "the
		// mesh reaches both ends but cannot join them" are different findings: the first is a
		// hole, the second is a wall, and a single failure value would conflate them.
		FNavLocation Start;
		FNavLocation End;
		if (!Mesh->ProjectPoint(StartsCm[Index], Start, ProjectExtentCm)
			|| !Mesh->ProjectPoint(EndsCm[Index], End, ProjectExtentCm))
		{
			Lengths[Index] = -2.0;
			continue;
		}

		FPathFindingQuery Query(nullptr, *Mesh, Start.Location, End.Location, Filter);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Nav->FindPathSync(Mesh->GetConfig(), Query);
		Lengths[Index] = (Result.IsSuccessful() && Result.Path.IsValid()
			&& !Result.Path->IsPartial())
			? Result.Path->GetLength()
			: -1.0;
	}
	return Lengths;
}

TArray<bool> UElysiumNavVerifyLibrary::ProjectPoints(UWorld* World, const FString& AgentName,
	const TArray<FVector>& PointsCm, const FVector& ProjectExtentCm)
{
	TArray<bool> Landed;
	Landed.Init(false, PointsCm.Num());
	ARecastNavMesh* Mesh = MeshFor(World, AgentName);
	if (Mesh == nullptr)
	{
		return Landed;
	}
	for (int32 Index = 0; Index < PointsCm.Num(); ++Index)
	{
		FNavLocation Out;
		Landed[Index] = Mesh->ProjectPoint(PointsCm[Index], Out, ProjectExtentCm);
	}
	return Landed;
}

TArray<FString> UElysiumNavVerifyLibrary::AgentMeshes(UWorld* World)
{
	TArray<FString> Rows;
	if (World == nullptr)
	{
		return Rows;
	}
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		Rows.Add(FString::Printf(TEXT("%s=%d"), *It->GetConfig().Name.ToString(),
			It->GetNumActiveTiles()));
	}
	Rows.Sort();
	return Rows;
}
