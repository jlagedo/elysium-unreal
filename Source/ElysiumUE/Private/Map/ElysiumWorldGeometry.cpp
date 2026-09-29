#include "Map/ElysiumWorldGeometry.h"

// 0018 story 6, wave 1: the contract's stubs. Each answers today's default -- no collision world, no
// route, no mesh -- so every caller keeps its current behaviour until lane A defines the bodies.

namespace ElysiumWorldGeometry
{
	bool Trace(UWorld& World, const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		Out.EndPosCm = Request.EndCm;
		return false;
	}

	bool Route(const ANavigationData& NavData, const FNavAgentProperties& Agent,
		FSharedConstNavQueryFilter Filter, const FVector& FromCm, const FElysiumNpcRouteQuery& Query,
		FElysiumNpcRouteAnswer& Out)
	{
		return false;
	}

	bool Raycast(const ANavigationData& NavData, FSharedConstNavQueryFilter Filter,
		const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out)
	{
		return false;
	}
}

namespace
{
	FElysiumGeometryBudget GElysiumGeometryCurrent;
	FElysiumGeometryBudget GElysiumGeometryPeak;
}

void FElysiumGeometryBudget::Count(EKind Kind, double InMs)
{
	check(IsInGameThread());
	if (GElysiumGeometryCurrent.Frame != GFrameCounter)
	{
		GElysiumGeometryCurrent = FElysiumGeometryBudget();
		GElysiumGeometryCurrent.Frame = GFrameCounter;
	}
	switch (Kind)
	{
	case EKind::PathTest: ++GElysiumGeometryCurrent.PathTests; break;
	case EKind::Raycast: ++GElysiumGeometryCurrent.Raycasts; break;
	case EKind::Trace: ++GElysiumGeometryCurrent.Traces; break;
	}
	GElysiumGeometryCurrent.Ms += InMs;
	if (GElysiumGeometryCurrent.Frame == GElysiumGeometryPeak.Frame
		|| GElysiumGeometryCurrent.Ms > GElysiumGeometryPeak.Ms)
	{
		GElysiumGeometryPeak = GElysiumGeometryCurrent;
	}
}

const FElysiumGeometryBudget& FElysiumGeometryBudget::Peak()
{
	return GElysiumGeometryPeak;
}
