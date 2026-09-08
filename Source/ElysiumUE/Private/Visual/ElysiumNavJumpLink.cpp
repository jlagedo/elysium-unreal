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
	GetSmartLinkComp()->SetMoveReachedLink(this, &AElysiumNavJumpLink::OnJumpLinkReached);
}

void AElysiumNavJumpLink::ConfigureJumpLink(FVector RelativeStart, FVector RelativeEnd,
	int32 InSourceLinkIndex, int32 InSourceNode, int32 InDestinationNode)
{
	SourceLinkIndex = InSourceLinkIndex;
	SourceNode = InSourceNode;
	DestinationNode = InDestinationNode;
	// Retail 0x102f626f/0x102f629f installs the same link at both endpoints. The AIN
	// records an accepted connection, with directional collision checked by the mover.
	GetSmartLinkComp()->SetLinkData(RelativeStart, RelativeEnd, ENavLinkDirection::BothWays);
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
