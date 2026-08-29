#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumPlayerBody.h"
#include "GameFramework/Pawn.h"

#include "ElysiumPawn.generated.h"

class UBoxComponent;
class UElysiumCameraComponent;
class UElysiumMovementComponent;
class USkeletalMeshComponent;

// The player's **body** (S3): collision, movement, the camera, and the handle of
// the entity it embodies. All player game state — the sheet, health, money, blood, the law counters
// — lives on that entity (`FElysiumPlayer`), and nothing a save would need lives here.
//
// The root is a **box**, not a capsule, and that is a recovered requirement rather than a
// preference: Source's player hull is an AABB (`-16,-16,0`..`16,16,72`) and `StepMove` depends on
// it — a capsule's rounded bottom catches a step's top edge and reports a normal of ~0.65 against
// the 0.7 standable test, so every climb is rejected (`docs/vtmb/source_movement.md`). `ACharacter`
// creates a `UCapsuleComponent` as its root and does not allow substitution, so this derives from
// `APawn` and brings its own `UElysiumMovementComponent`.
//
// It binds no keys and reads no key state. One `FElysiumUserCmd` per frame arrives from
// `UElysiumInputRouter` (S5) and the movement component consumes it.
UCLASS()
class AElysiumPawn : public APawn, public IElysiumPlayerBody
{
	GENERATED_BODY()

public:
	AElysiumPawn();

	virtual void BeginPlay() override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	// The one place the view is modified — the structural analogue of VtMB's `CAM_ApplyToView`.
	// It delegates to the camera component's `GetCameraView` first, then lets it apply the weight
	// stack; skipping that delegation is the documented cause of first-person rendering silently not
	// applying.
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	// IElysiumPlayerBody
	virtual bool IsNoclip() const override;
	virtual void SetNoclip(bool bEnable) override;
	virtual FElysiumEntityHandle GetPlayerEntity() const override { return PlayerEntity; }
	virtual void SetPlayerEntity(const FElysiumEntityHandle& Handle) override { PlayerEntity = Handle; }
	virtual float GetBodyHalfHeight() const override;
	virtual void SetMovementFrozen(bool bFrozen) override;
	virtual void ApplyUserCmd(const FElysiumUserCmd& Cmd) override;
	virtual FElysiumLocomotionSample GetLocomotionSample() const override;
	virtual UElysiumCameraComponent* GetCameraComponent() const override { return Camera; }
	virtual USkeletalMeshComponent* GetPlayerVisual() const override { return PlayerVisual; }
	virtual void SetPlayerVisual(USkeletalMeshComponent* InVisual) override;
	virtual void ApplyDrawPolicy(const FElysiumCameraDrawPolicy& Policy) override;
	virtual void SetBodyEntityHidden(bool bInHidden) override;

	// Swap the hull between the standing and ducked sizes (`docs/vtmb/source_movement.md` → "The hulls
	// and the view offsets"). The mover owns *when*; the pawn owns *how*, because the box extent
	// and the camera's relative Z have to move together or the view detaches from the body for a
	// frame. Not on `IElysiumPlayerBody` — it is the mover's own seam, not every body's.
	void SetHullHeight(float HeightCm, float EyeAboveFeetCm, bool bAnchorFeet);

	// Move the eye alone, leaving the hull as it is. The duck transition slides the view offset
	// between the standing and ducked heights while the body is still the standing size.
	void SetEyeHeight(float EyeAboveFeetCm);

private:
	UPROPERTY() TObjectPtr<UBoxComponent> Hull;
	UPROPERTY() TObjectPtr<UElysiumCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UElysiumMovementComponent> Movement;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> PlayerVisual;

	// The two independent gates on the surface, and the one place that resolves them. Kept apart
	// because they have different owners and different lifetimes: the policy is republished every
	// frame by the camera, the entity gate changes only when the entity's dormancy does.
	void RefreshBodyVisibility();

	FElysiumEntityHandle PlayerEntity;
	FElysiumCameraDrawPolicy DrawPolicy;
	bool bEntityHidden = false;

	// The last resolved draw gate, so the cloth hand-off runs on the **edge**. `ApplyDrawPolicy` is
	// called every frame by the camera manager, and resuming a garment re-teleports and resets it —
	// done per frame that is a solver that never integrates. Unset until the first refresh, so the
	// initial state is always pushed.
	TOptional<bool> LastClothDrawn;
};
