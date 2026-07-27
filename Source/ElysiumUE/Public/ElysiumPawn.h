#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumPlayerBody.h"
#include "GameFramework/Pawn.h"

#include "ElysiumPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class UElysiumMovementComponent;

// The player's **body** (S3, roadmap 11.4/11.6): collision, movement, the camera, and the handle of
// the entity it embodies. All player game state — the sheet, health, money, blood, the law counters
// — lives on that entity (`FElysiumPlayer`), and nothing a save would need lives here.
//
// The root is a **box**, not a capsule, and that is a recovered requirement rather than a
// preference: Source's player hull is an AABB (`-16,-16,0`..`16,16,72`) and `StepMove` depends on
// it — a capsule's rounded bottom catches a step's top edge and reports a normal of ~0.65 against
// the 0.7 standable test, so every climb is rejected (`docs/source_movement.md`). `ACharacter`
// creates a `UCapsuleComponent` as its root and does not allow substitution, so this derives from
// `APawn` and brings its own `UElysiumMovementComponent`. `AElysiumCapsulePawn` is the A/B baseline
// behind `elysium.SourceMovement 0`.
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

	// --- IElysiumPlayerBody ---------------------------------------------------------------
	virtual bool IsNoclip() const override;
	virtual void SetNoclip(bool bEnable) override;
	virtual FElysiumEntityHandle GetPlayerEntity() const override { return PlayerEntity; }
	virtual void SetPlayerEntity(const FElysiumEntityHandle& Handle) override { PlayerEntity = Handle; }
	virtual float GetBodyHalfHeight() const override;
	virtual void SetMovementFrozen(bool bFrozen) override;
	virtual void ApplyUserCmd(const FElysiumUserCmd& Cmd) override;

private:
	UPROPERTY() TObjectPtr<UBoxComponent> Hull;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<UElysiumMovementComponent> Movement;

	FElysiumEntityHandle PlayerEntity;
};
