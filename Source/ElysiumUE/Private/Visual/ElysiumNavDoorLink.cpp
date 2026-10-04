#include "Visual/ElysiumNavDoorLink.h"

#include "AIController.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumNpcAccess.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Visual/ElysiumNpcBody.h"

namespace ElysiumNavDoorLinkDetail
{
	// The body a path query or a path follower belongs to: the querier is the controller (the
	// route searches `AAIController::BuildPathfindingQuery` builds) or its path-following
	// component (the follower's own calls).
	AElysiumNpcBody* BodyOf(const UObject* Agent)
	{
		if (Agent == nullptr)
		{
			return nullptr;
		}
		if (const UActorComponent* Component = Cast<UActorComponent>(Agent))
		{
			Agent = Component->GetOwner();
		}
		if (const AController* Controller = Cast<AController>(Agent))
		{
			return Cast<AElysiumNpcBody>(Controller->GetPawn());
		}
		return Cast<AElysiumNpcBody>(const_cast<UObject*>(Agent));
	}

	// The door entity at a lump ordinal in the map's entity world, or null.
	FElysiumEntity* DoorAt(AElysiumMapActor* Map, int32 DoorEntityIndex)
	{
		FElysiumEntityWorld* World = Map != nullptr ? Map->GetEntityWorld() : nullptr;
		if (World == nullptr || !World->Entities().IsValidIndex(DoorEntityIndex))
		{
			return nullptr;
		}
		FElysiumEntity* Entity = World->Entities()[DoorEntityIndex].Get();
		return Entity != nullptr && Entity->AsDoorBase() != nullptr ? Entity : nullptr;
	}
}

bool UElysiumNavDoorLinkComponent::IsLinkPathfindingAllowed(const UObject* Querier) const
{
	const AElysiumNavDoorLink* Link = Cast<AElysiumNavDoorLink>(GetOwner());
	return Link == nullptr || Link->IsUsableFor(Querier);
}

AElysiumNavDoorLink::AElysiumNavDoorLink(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UElysiumNavDoorLinkComponent>(TEXT("SmartLinkComp")))
{
	// Smart link only: a simple link beside it would route through the doorway with no predicate.
	PointLinks.Reset();
	SegmentLinks.Reset();
	bSmartLinkIsRelevant = true;
	GetSmartLinkComp()->SetNavigationRelevancy(true);
	// No agent until the bake states them (a plain field write, safe during construction).
	GetSmartLinkComp()->SetSupportedAgents(FNavAgentSelector(0));
	GetSmartLinkComp()->SetMoveReachedLink(this, &AElysiumNavDoorLink::OnDoorLinkReached);
}

void AElysiumNavDoorLink::Adopt(AElysiumMapActor* InMap)
{
	OwningMap = InMap;
}

void AElysiumNavDoorLink::ConfigureDoorLink(FVector RelativeStart, FVector RelativeEnd,
	const TArray<int32>& InDoorEntityIndices, int32 InWitnessLinkIndex)
{
	DoorEntityIndices = InDoorEntityIndices;
	WitnessLinkIndex = InWitnessLinkIndex;
	// Retail installs each AIN link at both of its nodes; the doorway is walked either way.
	GetSmartLinkComp()->SetLinkData(RelativeStart, RelativeEnd, ENavLinkDirection::BothWays);
	SetSmartLinkEnabled(true);
}

int32 AElysiumNavDoorLink::SetSupportedAgentNames(const TArray<FString>& AgentNames)
{
	const TArray<FNavDataConfig>& Agents = GetDefault<UNavigationSystemV1>()->GetSupportedAgents();
	FNavAgentSelector Selector(0);
	int32 Resolved = 0;
	for (const FString& Name : AgentNames)
	{
		const int32 Index = Agents.IndexOfByPredicate(
			[&Name](const FNavDataConfig& Agent) { return Agent.Name.ToString() == Name; });
		if (Index != INDEX_NONE)
		{
			Selector.Set(Index);
			++Resolved;
		}
	}
	GetSmartLinkComp()->SetSupportedAgents(Selector);
	return Resolved;
}

int32 AElysiumNavDoorLink::SupportedAgentBits() const
{
	return static_cast<int32>(GetSmartLinkComp()->GetSupportedAgents().GetAgentBits());
}

FElysiumEntity* AElysiumNavDoorLink::PrimaryDoor(AElysiumMapActor* Map) const
{
	return DoorEntityIndices.Num() > 0 ? ElysiumNavDoorLinkDetail::DoorAt(Map, DoorEntityIndices[0]) : nullptr;
}

FElysiumEntity* AElysiumNavDoorLink::FirstSolidDoor(AElysiumMapActor* Map) const
{
	for (const int32 Index : DoorEntityIndices)
	{
		FElysiumEntity* Door = ElysiumNavDoorLinkDetail::DoorAt(Map, Index);
		const FElysiumDoorBase* DoorBase = Door != nullptr ? Door->AsDoorBase() : nullptr;
		// A hidden or dead brush collides with nothing; a door at its top stands out of the way.
		if (DoorBase != nullptr && !DoorBase->IsInert()
			&& DoorBase->State() != FElysiumDoorBase::EToggleState::AtTop)
		{
			return Door;
		}
	}
	return nullptr;
}

bool AElysiumNavDoorLink::IsUsableFor(const UObject* Querier) const
{
	AElysiumNpcBody* Body = ElysiumNavDoorLinkDetail::BodyOf(Querier);
	AElysiumMapActor* Map = OwningMap.Get();
	if (Map == nullptr && Body != nullptr)
	{
		Map = Body->GetOwningMap();
	}
	FElysiumEntityWorld* World = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	if (World == nullptr)
	{
		return true;   // no entity world behind the link (an editor or a bake world): no mark to read
	}
	// The predicate writes kernel state (the probe stamp, the mark, the notice). Off the game thread
	// -- an async re-path -- only its two passive arms are asked.
	FElysiumEntity* Npc = Body != nullptr && IsInGameThread() ? World->Resolve(Body->GetOwningEntity()) : nullptr;
	FElysiumNpcBase* NpcBase = Npc != nullptr ? Npc->AsNpcBase() : nullptr;
	// The link's stale words ride its doors (`FElysiumDoorLinkWords`): the link is usable when every
	// door's words let it be. A link over one door (every shipped link but the hub pair's) is exactly
	// `0x102ff960` / `0x102fce80`; over two, a second stale door asked after the first's re-probe
	// stamped this `curtime` answers stale unprobed (named).
	for (const int32 Index : DoorEntityIndices)
	{
		FElysiumEntity* Door = ElysiumNavDoorLinkDetail::DoorAt(Map, Index);
		if (Door == nullptr)
		{
			continue;
		}
		if (NpcBase != nullptr)
		{
			// `0x102ff960` / `0x102fce80` on the querier's own pathfinder.
			if (!ElysiumNpcAccess::DoorLinkPathfindingAllowed(*NpcBase, *Door,
				GetSmartLinkComp()->GetStartPoint(), GetSmartLinkComp()->GetEndPoint()))
			{
				return false;
			}
			continue;
		}
		// No NPC behind the query (no querier, or off the game thread): the passive arms only -- no
		// pathfinder to stamp a re-probe on, no NPC to notify, nothing written.
		const FElysiumDoorLinkWords& Words = Door->AsDoorBase()->LinkWords;
		if (Words.bStale && !(World->NowSeconds() > Words.StaleUntil))
		{
			return false;
		}
	}
	return true;
}

void AElysiumNavDoorLink::OnDoorLinkReached(UNavLinkCustomComponent* Link, UObject* PathingAgent,
	const FVector& Destination)
{
	(void)Destination;
	UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathingAgent);
	AElysiumNpcBody* Body = ElysiumNavDoorLinkDetail::BodyOf(PathingAgent);
	AElysiumMapActor* Map = OwningMap.Get();
	if (Map == nullptr && Body != nullptr)
	{
		Map = Body->GetOwningMap();
	}
	// Retail's move probe meets the first shut leaf in the doorway; with none, no leaf at all. A
	// door at its top, or a hidden / dead one (the hub's inert `plus_smoke_door`), is not solid.
	FElysiumEntity* Door = FirstSolidDoor(Map);
	if (Body == nullptr || Door == nullptr)
	{
		if (Body != nullptr)
		{
			Body->NoteDoorLinkCrossed(this);
		}
		if (Following != nullptr)
		{
			Following->FinishUsingCustomLink(Link);
		}
		return;
	}
	Body->HoldAtDoorLink(this, Door->Handle);
	HeldBodies.AddUnique(Body);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("door link %d: '%s' held at the doorway (door %s)"),
		WitnessLinkIndex, *GetNameSafe(Body), *Door->DebugString());
}

void AElysiumNavDoorLink::ReleaseHeldBodies()
{
	// One of the link's doors reached its top. Re-ask the doorway: another shut leaf on the same link
	// keeps the bodies held (now on that door), none lets them walk on.
	FElysiumEntity* StillSolid = FirstSolidDoor(OwningMap.Get());
	// Copy: a released body forgets this link, which edits the list.
	const TArray<TWeakObjectPtr<AElysiumNpcBody>> Held = HeldBodies;
	if (StillSolid == nullptr)
	{
		HeldBodies.Reset();
	}
	for (const TWeakObjectPtr<AElysiumNpcBody>& Weak : Held)
	{
		if (AElysiumNpcBody* Body = Weak.Get())
		{
			if (StillSolid != nullptr)
			{
				Body->HoldAtDoorLink(this, StillSolid->Handle);
			}
			else
			{
				Body->ReleaseDoorLinkHold(this);
			}
		}
	}
}

void AElysiumNavDoorLink::ForgetHeldBody(AElysiumNpcBody* Body)
{
	HeldBodies.RemoveAll([Body](const TWeakObjectPtr<AElysiumNpcBody>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == Body;
	});
}
