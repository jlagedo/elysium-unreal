#pragma once

#include "CoreMinimal.h"
#include "NavLinkCustomComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "ElysiumNavDoorLink.generated.h"

class AElysiumMapActor;
class AElysiumNpcBody;

/**
 * The door smart link's component: `UNavLinkCustomComponent` with retail's per-query link predicate.
 *
 * `IsLinkPathfindingAllowed` is Detour's special-link filter (`FRecastSpeciaLinkFilter::
 * isLinkAllowed`), asked for this link every time a route search expands it, with the searching
 * controller as the querier. It forwards to the querier's NPC (`FElysiumNpcBase::
 * DoorLinkPathfindingAllowed`, `0x102ff960` / `0x102fce80`), which reads the link's stale words on
 * the door and re-probes at most once per `curtime` per pathfinder.
 */
UCLASS()
class UElysiumNavDoorLinkComponent final : public UNavLinkCustomComponent
{
	GENERATED_BODY()

public:
	virtual bool IsLinkPathfindingAllowed(const UObject* Querier) const override;
};

// The map's smart link along one AIN link retail's graph runs through doors (0018/7).
//
// Retail builds AIN links straight through a standing door (the graph-build mask `0x2000b` has no
// `MOVEABLE`) while every run-time probe finds the door solid; the NPC meets the closed leaf, the
// movement sink hands it to NPC slot 531 `OnObstructingDoor`, and the door opens. The port cuts
// EVERY door out of every mesh (`UElysiumNavArea_DoorCut`) and lays this link across the doorways
// retail's graph crosses, for exactly the agents whose hulls cross it (`SupportedAgents`, the
// per-agent half the area cut cannot carry). One link per witness AIN link, over EVERY door entity
// that link crosses (retail's one link: the hub's smoke-shop pair is link 958), keyed by the doors'
// lump ordinals (`DoorEntityIndices`, each `FElysiumEntityHandle::Index`).
//
// Reached (the follower arrives at the link's start): the link's doors are asked in order, and the
// first that is solid -- not open (at its top), not hidden or dead -- HOLDS the body at the doorway
// (the custom-link wait) and is reported in `FElysiumNpcMoveFacts::DoorLinkEntity`, which the
// kernel's move step reads as its blocker and hands to slot 531. With none solid the body walks
// straight through. A held body is re-asked when one of the link's doors reaches its top
// (`DoorHitTop` -> `IElysiumEmbodiment::ReleaseDoorLink`): released when none is solid any more,
// else held on the next solid one. The request ending also ends the hold.
//
// Always enabled: a disabled smart link takes the null area, and Detour's area filter refuses it
// BEFORE the special-link filter is asked, which would erase `0x102fce80`'s re-probe arm. The stale
// mark therefore lives in the per-query predicate, not in the enabled flag (named, 0018/7).
UCLASS(NotBlueprintable)
class AElysiumNavDoorLink final : public ANavLinkProxy
{
	GENERATED_BODY()

public:
	AElysiumNavDoorLink(const FObjectInitializer& ObjectInitializer);

	/** The endpoints (relative to the actor), the doors the link crosses and its AIN link. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	void ConfigureDoorLink(FVector RelativeStart, FVector RelativeEnd,
		const TArray<int32>& InDoorEntityIndices, int32 InWitnessLinkIndex);

	/** The agents this link carries, by the project's agent names (`UElysiumNavBakeLibrary::
	 *  AgentNamesForHullBits`). Answers how many names resolved to a supported agent. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	int32 SetSupportedAgentNames(const TArray<FString>& AgentNames);

	/** The smart link's agent bits (without the initialised bit). */
	UFUNCTION(BlueprintCallable, Category="Elysium|Navigation")
	int32 SupportedAgentBits() const;

	/** The doors' lump ordinals (`FElysiumEntityHandle::Index`), in the order the hold asks them. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	TArray<int32> DoorEntityIndices;

	/** The AIN link the bake's crossing test found through this door (data only). */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 WitnessLinkIndex = INDEX_NONE;

	/** Adopted by the map actor that owns the entity world this link's door lives in. */
	void Adopt(AElysiumMapActor* InMap);

	/** The per-query predicate (`UElysiumNavDoorLinkComponent::IsLinkPathfindingAllowed`). */
	bool IsUsableFor(const UObject* Querier) const;

	/** The door reached its top: every body this link holds walks on. */
	void ReleaseHeldBodies();

	/** A body let go of its hold for its own reason (a new request, a stop). */
	void ForgetHeldBody(AElysiumNpcBody* Body);

	/** The first of the link's doors that stands solid in the doorway (not at its top, not hidden or
	 *  dead) in `Map`'s entity world, or null. */
	class FElysiumEntity* FirstSolidDoor(AElysiumMapActor* Map) const;

	/** The link's first door in `Map`'s entity world (the one the path facts name), or null. */
	class FElysiumEntity* PrimaryDoor(AElysiumMapActor* Map) const;

private:
	void OnDoorLinkReached(UNavLinkCustomComponent* Link, UObject* PathingAgent,
		const FVector& Destination);

	TWeakObjectPtr<AElysiumMapActor> OwningMap;
	TArray<TWeakObjectPtr<AElysiumNpcBody>> HeldBodies;
};
