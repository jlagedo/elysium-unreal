#include "Visual/ElysiumNpcBody.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumMapActor.h"
#include "ElysiumPlayerBody.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Map/ElysiumNavQueryFilter_Pedestrian.h"
#include "Map/ElysiumWorldGeometry.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

// 0018 story 6: the NPC body's half of the geometry seam -- the path-length service and the navmesh
// raycast, on this body's own nav agent. The body resolves the nav data, the agent and the filter;
// `ElysiumWorldGeometry` asks the mesh.

namespace ElysiumNpcBodyGeometry
{
	// The nav data this body walks: its own agent (the pathing hull's, stated by `ApplyRetailHull`)
	// at its feet. Null = no NavMesh behind this body.
	const ANavigationData* ResolveNavData(const AElysiumNpcBody& Body, const FNavAgentProperties*& OutAgent)
	{
		OutAgent = nullptr;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Body.GetWorld());
		const UCharacterMovementComponent* Movement = Body.GetCharacterMovement();
		if (NavSys == nullptr || Movement == nullptr)
		{
			return nullptr;
		}
		OutAgent = &Movement->NavAgentProps;
		return NavSys->GetNavDataForProps(*OutAgent, Body.GetNavAgentLocation());
	}
}

bool AElysiumNpcBody::QueryRoute(const FElysiumNpcRouteQuery& Query, FElysiumNpcRouteAnswer& Out) const
{
	// A route on this body's own mesh, asked synchronously and measured, under the filter `MoveTo`
	// builds for the same multiplier (`ElysiumNavQueryFilterPedestrian::MakeFilter`: 0 = the nav
	// data's default, 5..10 = the priced pedestrian copy), so the route measured is the route walked.
	const FNavAgentProperties* Agent = nullptr;
	const ANavigationData* NavData = ElysiumNpcBodyGeometry::ResolveNavData(*this, Agent);
	if (NavData == nullptr || Agent == nullptr)
	{
		return false;
	}
	const FSharedConstNavQueryFilter Filter = ElysiumNavQueryFilterPedestrian::MakeFilter(
		*NavData, this, FMath::Max(0, Query.PedestrianCostMultiplier));
	// An explicit start is retail's two-point ask (`0x102ee380` -> `0x102fdcc0`); unset = from here.
	const FVector FromCm = Query.StartCm.IsSet() ? Query.StartCm.GetValue() : GetNavAgentLocation();
	// The controller is the search's owner, as `MoveTo`'s own queries name it: the door smart link's
	// per-query predicate reads the asking NPC through it (0018/7).
	return ElysiumWorldGeometry::Route(*NavData, *Agent, Filter, FromCm, Query, Out, GetController());
}

bool AElysiumNpcBody::NavRaycast(const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out) const
{
	// The Detour raycast on the same nav data, under the same filter a route would use.
	const FNavAgentProperties* Agent = nullptr;
	const ANavigationData* NavData = ElysiumNpcBodyGeometry::ResolveNavData(*this, Agent);
	if (NavData == nullptr)
	{
		return false;
	}
	const FSharedConstNavQueryFilter Filter = ElysiumNavQueryFilterPedestrian::MakeFilter(
		*NavData, this, FMath::Max(0, Query.PedestrianCostMultiplier));
	return ElysiumWorldGeometry::Raycast(*NavData, Filter, Query, Out);
}

// -------------------------------------------------------------------------------------------------
// 0019 story 6: the floor, the move-ignore filter and the route-held fact. Facts only; the rules
// that read them (`CheckOnGround` `0x1026e5e0`, the Troika ignore triple, the moving-goal trackers)
// stay in the kernel.
// -------------------------------------------------------------------------------------------------

namespace ElysiumNpcBodyGeometry
{
	// The entity behind the surface a floor probe hit, classified the way the map actor's
	// `HandleForActor` classifies a trace hit: a brush / prop entity's component carries its own
	// handle, an NPC body and the player's pawn carry theirs, everything else is the static world.
	FElysiumEntityHandle HandleOfFloor(const FHitResult& Hit)
	{
		if (const UElysiumBrushComponent* Brush = Cast<UElysiumBrushComponent>(Hit.GetComponent()))
		{
			return Brush->GetOwningEntity();
		}
		const AActor* Actor = Hit.GetActor();
		if (const AElysiumNpcBody* Body = Cast<AElysiumNpcBody>(Actor))
		{
			return Body->GetOwningEntity();
		}
		if (const IElysiumPlayerBody* Player = Cast<IElysiumPlayerBody>(Actor))
		{
			return Player->GetPlayerEntity();
		}
		return FElysiumEntityHandle::Invalid();
	}
}

bool AElysiumNpcBody::SampleFloor(FElysiumNpcFloorFacts& Out) const
{
	// The movement component's own capsule floor probe, asked fresh. `CurrentFloor` is what the
	// component measured on its last WALKING tick; a body that stands asleep, was teleported, or is
	// falling holds a stale or cleared one, and retail's `CheckOnGround` traces on its own clock.
	// `FindFloor` is the same query `CurrentFloor` is filled from, with this capsule's collision
	// params (so a `SetMoveIgnore` entity is not the floor either).
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr || GetCapsuleComponent() == nullptr)
	{
		return false;
	}
	Out = FElysiumNpcFloorFacts();
	Out.MaxStepHeightCm = Movement->MaxStepHeight;
	Out.WalkableFloorZ = Movement->GetWalkableFloorZ();
	Out.WalkableFloorAngleDegrees = Movement->GetWalkableFloorAngle();

	FFindFloorResult Floor;
	Movement->FindFloor(GetActorLocation(), Floor, /*bCanUseCachedLocation=*/false);
	if (!Floor.bBlockingHit)
	{
		return true;   // nothing under the hull within the probe's reach (retail: fraction 1.0)
	}
	Out.bOnGround = true;
	Out.bWalkable = Floor.bWalkableFloor;
	Out.FloorNormal = Floor.HitResult.ImpactNormal;
	Out.FloorDistanceCm = Floor.FloorDist;
	Out.GroundEntityHandle = ElysiumNpcBodyGeometry::HandleOfFloor(Floor.HitResult);
	return true;
}

void AElysiumNpcBody::ResolveEntityCollision(const FElysiumEntityHandle& Entity, AActor*& OutActor,
	UPrimitiveComponent*& OutComponent) const
{
	OutActor = nullptr;
	OutComponent = nullptr;
	UWorld* World = GetWorld();
	if (!Entity.IsSet() || World == nullptr)
	{
		return;
	}
	// A character: an NPC's body or the player's pawn. Both are pawns; the walk is over pawns only.
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (const AElysiumNpcBody* Body = Cast<AElysiumNpcBody>(Pawn))
		{
			if (Body != this && Body->GetOwningEntity() == Entity)
			{
				OutActor = Pawn;
				return;
			}
			continue;
		}
		if (const IElysiumPlayerBody* Player = Cast<IElysiumPlayerBody>(Pawn))
		{
			if (Player->GetPlayerEntity() == Entity)
			{
				OutActor = Pawn;
				return;
			}
		}
	}
	// A brush or runtime prop entity: its body is a component of the map actor that owns this one,
	// and the component carries its own handle (the map actor itself stands for every one of them).
	if (AElysiumMapActor* Map = OwningMap.Get())
	{
		Map->ForEachComponent<UElysiumBrushComponent>(/*bIncludeFromChildActors=*/false,
			[&OutComponent, &Entity](UElysiumBrushComponent* Brush)
			{
				if (OutComponent == nullptr && Brush->GetOwningEntity() == Entity)
				{
					OutComponent = Brush;
				}
			});
	}
}

void AElysiumNpcBody::SetMoveIgnore(const FElysiumEntityHandle& Entity, bool bIgnore)
{
	// `UPrimitiveComponent::IgnoreActorWhenMoving` / `IgnoreComponentWhenMoving` on the capsule, the
	// component the movement component sweeps: every move and floor probe of this body skips it.
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (Capsule == nullptr)
	{
		return;
	}
	AActor* Actor = nullptr;
	UPrimitiveComponent* Component = nullptr;
	ResolveEntityCollision(Entity, Actor, Component);
	if (Actor != nullptr)
	{
		Capsule->IgnoreActorWhenMoving(Actor, bIgnore);
	}
	else if (Component != nullptr)
	{
		Capsule->IgnoreComponentWhenMoving(Component, bIgnore);
	}
}

bool AElysiumNpcBody::HasPath() const
{
	// The follower holds a request (Moving, Waiting or Paused: a held body's route stands, as a
	// paused retail route does) and the path it carries is valid.
	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following != nullptr && Following->GetStatus() != EPathFollowingStatus::Idle
		&& Following->HasValidPath();
}
