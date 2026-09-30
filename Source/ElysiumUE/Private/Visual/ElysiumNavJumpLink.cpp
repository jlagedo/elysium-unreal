#include "Visual/ElysiumNavJumpLink.h"

#include "AIController.h"
#include "NavLinkCustomComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Visual/ElysiumNpcBody.h"

AElysiumNavJumpLink::AElysiumNavJumpLink(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A parallel simple link would let Recast route over the gap without invoking the jump.
	PointLinks.Reset();
	SegmentLinks.Reset();
	bSmartLinkIsRelevant = true;
	GetSmartLinkComp()->SetNavigationRelevancy(true);
	// No agent from birth (a plain field write, safe during construction): whatever else happens
	// to an instance, no agent's mesh carries it. The enabled flag is written by
	// `ConfigureJumpLink`, where the component can be told about the change.
	GetSmartLinkComp()->SetSupportedAgents(FNavAgentSelector(0));
	// Reached only by the synthetic flight tests: with no agent and the link disabled, no path
	// follower ever arrives here in production (0018/7).
	GetSmartLinkComp()->SetMoveReachedLink(this, &AElysiumNavJumpLink::OnJumpLinkReached);
}

void AElysiumNavJumpLink::MakeNonTraversable()
{
	// Retail's reason is at the class: `0x102ff960` step 2 refuses every jump-only word.
	PointLinks.Reset();
	SegmentLinks.Reset();
	GetSmartLinkComp()->SetSupportedAgents(FNavAgentSelector(0));
	SetSmartLinkEnabled(false);
}

void AElysiumNavJumpLink::ConfigureJumpLink(FVector RelativeStart, FVector RelativeEnd,
	int32 InSourceLinkIndex, int32 InSourceNode, int32 InDestinationNode)
{
	SourceLinkIndex = InSourceLinkIndex;
	SourceNode = InSourceNode;
	DestinationNode = InDestinationNode;
	// Retail 0x102f626f/0x102f629f installs the same link at both endpoints; the direction is
	// recorded, and nothing routes on it.
	GetSmartLinkComp()->SetLinkData(RelativeStart, RelativeEnd, ENavLinkDirection::BothWays);
	MakeNonTraversable();
}

bool AElysiumNavJumpLink::SetHullVerdicts(const TArray<FElysiumNavJumpHullVerdict>& Verdicts)
{
	for (const FElysiumNavJumpHullVerdict& Verdict : Verdicts)
	{
		if (!Verdict.bJumpOnly && (Verdict.bLegalForward || Verdict.bLegalBack))
		{
			return false;
		}
	}
	HullVerdicts = Verdicts;
	return true;
}

bool AElysiumNavJumpLink::IsTraversable() const
{
	return !PointLinks.IsEmpty() || !SegmentLinks.IsEmpty() || IsSmartLinkEnabled()
		|| GetSmartLinkComp()->GetSupportedAgents().ContainsAnyAgent();
}

int32 AElysiumNavJumpLink::SupportedAgentBits() const
{
	return static_cast<int32>(GetSmartLinkComp()->GetSupportedAgents().GetAgentBits());
}

void AElysiumNavJumpLink::OnJumpLinkReached(UNavLinkCustomComponent* Link,
	UObject* PathingAgent, const FVector& Destination)
{
	UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathingAgent);
	AAIController* Controller = Following ? Cast<AAIController>(Following->GetOwner()) : nullptr;
	AElysiumNpcBody* Body = Controller ? Cast<AElysiumNpcBody>(Controller->GetPawn()) : nullptr;
	if (Body && Body->BeginNavigationJump(this, Destination)) return;

	UE_LOG(LogElysiumNpcEnt, Warning,
		TEXT("AIN jump link %d (%d <-> %d) cannot begin traversal for '%s'"),
		SourceLinkIndex, SourceNode, DestinationNode, *GetNameSafe(Body));
	if (Following)
	{
		Following->AbortMove(*this, FPathFollowingResultFlags::InvalidPath,
			FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Keep);
		Following->FinishUsingCustomLink(Link);
	}
}
