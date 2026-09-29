#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "NavFilters/NavigationQueryFilter.h"

#include "ElysiumNavQueryFilter_Pedestrian.generated.h"

class ANavigationData;
class UObject;

/**
 * The pedestrian search's filter: the class that names `UElysiumNavArea_Pedestrian`.
 *
 * Retail's A* `FUN_102fe9f0` multiplies a flagged (`0x2000`) link's cost by one
 * `RandomInt(5, 10)` drawn per search, so a pedestrian pays 5-10x to step into the roadway; every
 * other search (`FUN_103055b0`, `param_3 == 0`) pays the plain cost. The draw is the substrate's and
 * arrives per request (`FElysiumNpcMoveRequest::PedestrianCostMultiplier`), so this class lists
 * the area and leaves its travel cost at the area's own default of 1. `MakeFilter` below prices it.
 *
 * Never mutated: `UNavigationQueryFilter::GetQueryFilter` caches one filter per class on the nav
 * data, shared by every caller, and the multiplier differs per search.
 */
UCLASS()
class ELYSIUMUE_API UElysiumNavQueryFilter_Pedestrian : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UElysiumNavQueryFilter_Pedestrian(const FObjectInitializer& ObjectInitializer);
};

namespace ElysiumNavQueryFilterPedestrian
{
	/**
	 * The filter for one pedestrian search. `CostMultiplier` is the search's one `RandomInt(5, 10)`
	 * draw; `<= 0` is a non-pedestrian search and answers the nav data's default filter. Otherwise a
	 * fresh copy of the class filter whose pedestrian-area travel cost is `CostMultiplier`, so two
	 * calls never share a cost table. A nav data that does not carry the pedestrian area answers
	 * the unpriced copy (nothing on its mesh is flagged).
	 */
	ELYSIUMUE_API FSharedConstNavQueryFilter MakeFilter(
		const ANavigationData& NavData, const UObject* Querier, int32 CostMultiplier);
}
