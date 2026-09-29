#include "Map/ElysiumNavQueryFilter_Pedestrian.h"

#include "ElysiumNavAreas.h"
#include "NavigationData.h"

UElysiumNavQueryFilter_Pedestrian::UElysiumNavQueryFilter_Pedestrian(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The area is listed and its travel cost left alone (`bOverrideTravelCost` false): the price is
	// the per-search `RandomInt(5, 10)`, which only `MakeFilter` knows.
	FNavigationFilterArea Pedestrian;
	Pedestrian.AreaClass = UElysiumNavArea_Pedestrian::StaticClass();
	Areas.Add(Pedestrian);
}

FSharedConstNavQueryFilter ElysiumNavQueryFilterPedestrian::MakeFilter(
	const ANavigationData& NavData, const UObject* Querier, int32 CostMultiplier)
{
	if (CostMultiplier <= 0)
	{
		return NavData.GetDefaultQueryFilter();
	}

	const FSharedConstNavQueryFilter ClassFilter = UNavigationQueryFilter::GetQueryFilter(
		NavData, Querier, UElysiumNavQueryFilter_Pedestrian::StaticClass());
	if (!ClassFilter.IsValid())
	{
		return NavData.GetDefaultQueryFilter();
	}

	// A copy per request: the class filter is cached on the nav data and shared.
	const FSharedNavQueryFilter Copy = ClassFilter->GetCopy();
	const int32 AreaId = NavData.GetAreaID(UElysiumNavArea_Pedestrian::StaticClass());
	if (AreaId != INDEX_NONE)
	{
		Copy->SetAreaCost(IntCastChecked<uint8>(AreaId), static_cast<float>(CostMultiplier));
	}
	return Copy;
}
