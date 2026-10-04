#include "ElysiumNavAreaActor.h"

#include "AI/NavigationModifier.h"
#include "AI/NavigationSystemBase.h"
#include "ElysiumNavAreas.h"
#include "NavigationSystem.h"   // FNavigationRelevantData

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNavArea, Log, All);

//: A convex needs at least a tetrahedron. The collision payload drops anything smaller rather
//: than staging a row the cook would silently discard, and a mark follows the same rule so the
//: two describe the same set of brushes.
static constexpr int32 GMinConvexPoints = 4;

UElysiumNavAreaComponent::UElysiumNavAreaComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Static);
	// It marks an area; it stops nothing. A mark that answered a pawn channel would also cut the
	// mesh, which is the opposite of what a priced roadway is for.
	bCanEverAffectNavigation = true;
}

void UElysiumNavAreaComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	if (AreaClass == nullptr)
	{
		return;
	}
	// The roadway's vertical frame -- under 0018/6's NavMesh divergence (Source's node graph and
	// link stream become a Recast mesh), the frame difference with retail's `0x2000` test
	// `102fbbaa`. Retail asks the brush's contents between two NODE positions, about 21 cm above
	// the ground (`sm_hub_1`: slabs z -118..-16 u, nodes z ~-111 u; `navigation-jump-links.md`
	// § "The `0x2000`-only brushes"), so a slab whose floor floats over the road still meets every
	// link across it. Recast asks the GROUND surface, the walkable span's top, and lowers a
	// convex's floor by one cell height only (`FRecastTileGenerator::MarkDynamicArea`,
	// `OffsetZMin = ch + (ShouldIncludeAgentHeight() ? AgentHeight : 0)`); the hub's slabs stand at
	// z -298.5 cm over a road at -304.8 cm. Including the agent height reaches the surface the agent
	// stands on under the volume -- what the engine's own convex marks do (`NavCollision.cpp`
	// `SetIncludeAgentHeight(true)`) -- and adds no margin of ours: the staged hull stays the brush.
	// The door cut is not this rule (a door is solid to every probe at its own height, 0018/7) and
	// keeps the brush's own floor.
	const bool bReachGroundUnder = AreaClass->IsChildOf(UElysiumNavArea_Pedestrian::StaticClass());
	// The points are already world centimetres -- the pipeline stages them in the same frame the
	// collision hulls are staged in -- so the transform handed to the modifier is identity rather
	// than this component's. Passing the component transform would apply the actor's placement a
	// second time.
	for (const FElysiumNavAreaConvex& Convex : Convexes)
	{
		if (Convex.Points.Num() < GMinConvexPoints)
		{
			continue;
		}
		FAreaNavModifier Area(Convex.Points, ENavigationCoordSystem::Unreal, FTransform::Identity,
			AreaClass);
		// Set before `Add`: the composite reads the flag as the area is added (`bAdjustHeight`).
		Area.SetIncludeAgentHeight(bReachGroundUnder);
		Data.Modifiers.Add(Area);
	}
}

FBox UElysiumNavAreaComponent::GetNavigationBounds() const
{
	return AreaBounds;
}

bool UElysiumNavAreaComponent::IsNavigationRelevant() const
{
	return AreaClass != nullptr && Convexes.Num() > 0 && AreaBounds.IsValid;
}

AElysiumNavAreaActor::AElysiumNavAreaActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	GetRootComponent()->SetMobility(EComponentMobility::Static);
}

void AElysiumNavAreaActor::Author(const FString& InMapName)
{
	MapName = InMapName;
}

UElysiumNavAreaComponent* AElysiumNavAreaActor::AddArea(const FString& Label,
	TSubclassOf<UNavAreaBase> AreaClass, const TArray<FVector>& Points,
	const TArray<int32>& ConvexSizes)
{
	if (AreaClass == nullptr || ConvexSizes.IsEmpty())
	{
		return nullptr;
	}

	UElysiumNavAreaComponent* Component = NewObject<UElysiumNavAreaComponent>(
		this, FName(*FString::Printf(TEXT("NavArea_%s"), *Label)));
	Component->AreaClass = AreaClass;
	Component->SetupAttachment(GetRootComponent());

	FBox Bounds(ForceInit);
	int32 Cursor = 0;
	int32 Dropped = 0;
	for (const int32 Size : ConvexSizes)
	{
		if (Size <= 0 || Cursor + Size > Points.Num())
		{
			// A truncated run is a staging defect, not something to guess at -- and silence here
			// would leave the level marked with fewer convexes than the report claims.
			UE_LOG(LogElysiumNavArea, Error,
				TEXT("%s: %s names a convex of %d point(s) with %d left; the run is truncated"),
				*MapName, *Label, Size, Points.Num() - Cursor);
			return nullptr;
		}
		FElysiumNavAreaConvex Convex;
		Convex.Points.Reserve(Size);
		for (int32 Index = 0; Index < Size; ++Index)
		{
			Convex.Points.Add(Points[Cursor + Index]);
		}
		Cursor += Size;
		if (Convex.Points.Num() < GMinConvexPoints)
		{
			++Dropped;
			continue;   // and its points stay OUT of the bounds below: the octree keys on what is
						// marked, and a dropped convex marks nothing
		}
		for (const FVector& Point : Convex.Points)
		{
			Bounds += Point;
		}
		Component->Convexes.Add(MoveTemp(Convex));
	}

	if (Component->Convexes.IsEmpty())
	{
		return nullptr;
	}
	Component->AreaBounds = Bounds;
	Component->RegisterComponent();
	AddInstanceComponent(Component);

	UE_LOG(LogElysiumNavArea, Log, TEXT("%s: %s marks %d convex(es)%s"),
		*MapName, *Label, Component->Convexes.Num(),
		Dropped > 0 ? *FString::Printf(TEXT(", %d too small to be one"), Dropped) : TEXT(""));
	Areas.Add(Component);
	return Component;
}
