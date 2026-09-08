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

	// `CPlayerMove::SetupMove` `0x10186120` → `CPlayerMove::FinishMove` `0x10186c10`'s
	// `SetLocalAngles(mv->m_vecAngles)`, which is the tick's body-angle writeback: the entity's own
	// pitch and roll with `m_angEyeAngles.y` substituted for its yaw. `bUseControllerRotationYaw`
	// with pitch and roll off is that rule, so the override only has to reproduce the *suppression*
	// — `SetupMove`'s tail arm re-taking all three from `GetAngles()`, which leaves the body's yaw
	// where the pose owner put it.
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;

	// The frame's answer to `ElysiumMove::BodyYawFollowsEye` — a live grapple partner, or
	// `m_iVFlags & 0x1` with none. Pushed by `AElysiumPlayerController::ProcessPlayerInput`, which
	// is the port's `SetupMove` seam and the only place the player entity's state is read; the pawn
	// itself reads no game state (S3). It is a per-tick latch rather than a stored mode because
	// retail re-decides it every tick from live handles.
	void SetBodyPosedExternally(bool bPosed) { bBodyPosedExternally = bPosed; }
	bool IsBodyPosedExternally() const { return bBodyPosedExternally; }

	// `SetupMove`'s partner arm: `mv->m_vecAbsOrigin = partner->GetOrigin()` (z-corrected by the
	// difference of the two collision minima) and `mv->m_vecVelocity = vec3_origin`, applied every
	// tick while a grapple is live outside the nine release verbs. `FinishMove` writes both back
	// through `SetLocalOrigin` / `SetAbsVelocity`, so in the port it is a placement plus a velocity
	// kill on the body rather than a movement mode. **The argument is a feet origin**, matching the
	// entity's own `Origin`; the pawn lifts it by its half-height because the box is centred.
	void GlueBodyToFeetOrigin(const FVector& FeetOrigin);

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
	bool bBodyPosedExternally = false;

	// The last resolved draw gate, so the cloth hand-off runs on the **edge**. `ApplyDrawPolicy` is
	// called every frame by the camera manager, and resuming a garment re-teleports and resets it —
	// done per frame that is a solver that never integrates. Unset until the first refresh, so the
	// initial state is always pushed.
	TOptional<bool> LastClothDrawn;
};
