#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumBrushComponent.generated.h"

struct FElysiumConvexHull;
class UBodySetup;
class UStaticMeshComponent;

// The runtime solidity of a brush entity's body. In Source (and VtMB) solidity is a per-class
// property, not a brush-contents flag — the `.ents` `contents`/`blocks_player` fields do not
// discriminate triggers from solids — so it is decided by classname (ElysiumBrushSolidityForClass).
enum class EElysiumBrushSolidity : uint8
{
	Solid,    // blocks the player + world (func_door/button/brush/elevator/…): BlockAll
	Trigger,  // non-solid overlap volume (trigger_*): QueryOnly, overlaps, raises begin/end touch
	Passable, // query-only: ignores actors/physics, blocks only ElysiumUse/ElysiumPick
	None,     // non-solid, no touch (func_illusionary): the body carries the handle only
};

// Classify a brush entity's runtime solidity from its classname. Leaf classes can override
// the result on their own body in Spawn(); this is the default the world builds with.
EElysiumBrushSolidity ElysiumBrushSolidityForClass(const FString& Classname);

// The collision/overlap embodiment of one brush entity. A live entity is a plain-C++
// FElysiumEntity; this component is a disposable body the map actor owns, positioned at the
// def origin (brush hulls are entity-local, origin = hinge for the rotating doors). It
// renders nothing (no scene proxy); its whole job is a convex UBodySetup cooked from the
// def's hulls plus the overlap tap that routes begin/end touch back to the entity world.
// It carries its owning handle so a crosshair trace resolves to an entity in one step, and
// its collision is gated by the entity's dormancy switch.
UCLASS()
class UElysiumBrushComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UElysiumBrushComponent();

	// Build the convex body from the entity-local hulls and apply the classname's solidity.
	// Called once at spawn, before RegisterComponent — the base UPrimitiveComponent creates the
	// physics body from GetBodySetup() at registration, so the setup must be cooked first.
	void InitBrush(const FElysiumEntityHandle& InOwner, const TArray<FElysiumConvexHull>& Hulls,
		EElysiumBrushSolidity Solidity);

	// The same body, adopted from the map's cooked collision payload instead of cooked here:
	// `Cooked` is one entity's `UBodySetup`, authored offline from these same hulls.
	// The component keeps the setup but does not
	// own it — the payload asset does, and it outlives the map load.
	void InitBrushFromPayload(const FElysiumEntityHandle& InOwner, UBodySetup* Cooked,
		EElysiumBrushSolidity Solidity);

	// Dormancy: dormant → collision off (cannot be touched/traced) and visual hidden;
	// active → restore the built solidity and attached visual.
	void SetDormant(bool bDormant);
	void SetVisual(UStaticMeshComponent* InVisual);
	UStaticMeshComponent* GetVisual() const { return Visual; }

	const FElysiumEntityHandle& GetOwningEntity() const { return OwningEntity; }
	EElysiumBrushSolidity GetSolidity() const { return BuiltSolidity; }

	// UPrimitiveComponent — collision-only: hand out our convex setup, bound the body from it.
	virtual UBodySetup* GetBodySetup() override { return BrushBodySetup; }
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	UPROPERTY() TObjectPtr<UBodySetup> BrushBodySetup;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;

	FElysiumEntityHandle OwningEntity;
	EElysiumBrushSolidity BuiltSolidity = EElysiumBrushSolidity::Solid;
	FBox LocalBounds = FBox(ForceInit);

	void ApplySolidity(EElysiumBrushSolidity Solidity);
	// The tail both InitBrush paths share: solidity, and the one overlap tap.
	void FinishInit(const FElysiumEntityHandle& InOwner, EElysiumBrushSolidity Solidity);

	// Overlap taps — forward to the entity world (resolved through the owning map actor), so a
	// stale world pointer is impossible: teardown drops the actor's world before the actor's
	// components.
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void RouteTouch(const AActor* Toucher, bool bBegin) const;
};
