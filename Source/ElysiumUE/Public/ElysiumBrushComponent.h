#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumBrushComponent.generated.h"

struct FElysiumConvexHull;
class UBodySetup;

// The runtime solidity of a brush entity's body. In Source (and VtMB) solidity is a per-class
// property, not a brush-contents flag — the `.ents` `contents`/`blocks_player` fields do not
// discriminate triggers from solids — so it is decided by classname (ElysiumBrushSolidityForClass).
enum class EElysiumBrushSolidity : uint8
{
	Solid,    // blocks the player + world (func_door/button/brush/elevator/…): BlockAll
	Trigger,  // non-solid overlap volume (trigger_*): QueryOnly, overlaps, raises begin/end touch
	None,     // non-solid, no touch (func_illusionary): the body carries the handle only
};

// Classify a brush entity's runtime solidity from its classname. P1.6 leaf classes can override
// the result on their own body in Spawn(); this is the P1.5 default the world builds with.
EElysiumBrushSolidity ElysiumBrushSolidityForClass(const FString& Classname);

// R1 body (P1.5) — the collision/overlap embodiment of one brush entity. A live entity is a
// plain-C++ FElysiumEntity; this component is a disposable body the map actor owns, positioned
// at the def origin (brush hulls are entity-local, origin = hinge for the rotating doors P4
// animates). It renders nothing (no scene proxy); its whole job is a convex UBodySetup cooked
// from the def's hulls plus the overlap tap that routes begin/end touch back to the entity
// world. It carries its owning handle so a crosshair trace (P2) resolves to an entity in one
// step, and its collision is gated by the entity's dormancy switch (R6).
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

	// Dormancy (R6): dormant → collision off (cannot be touched/traced); active → restore the
	// built solidity. Nothing to gate for rendering — the component draws nothing.
	void SetDormant(bool bDormant);

	const FElysiumEntityHandle& GetOwningEntity() const { return OwningEntity; }
	EElysiumBrushSolidity GetSolidity() const { return BuiltSolidity; }

	// UPrimitiveComponent — collision-only: hand out our convex setup, bound the body from it.
	virtual UBodySetup* GetBodySetup() override { return BrushBodySetup; }
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	UPROPERTY() TObjectPtr<UBodySetup> BrushBodySetup;

	FElysiumEntityHandle OwningEntity;
	EElysiumBrushSolidity BuiltSolidity = EElysiumBrushSolidity::Solid;
	FBox LocalBounds = FBox(ForceInit);

	void ApplySolidity(EElysiumBrushSolidity Solidity);

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
