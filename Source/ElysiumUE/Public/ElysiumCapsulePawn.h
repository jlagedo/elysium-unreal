#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumPlayerBody.h"
#include "GameFramework/Character.h"

#include "ElysiumCapsulePawn.generated.h"

class UElysiumCameraComponent;

// The A/B baseline body, behind `elysium.SourceMovement 0`: `ACharacter` on a capsule over
// `UCharacterMovementComponent`, which is what the player had before 11.6 put the faithful box hull
// and `UElysiumMovementComponent` in front of it. It is kept so a movement or feel change can be
// compared against something that is known to work, which is what `remaster-direction.md`'s
// "keep it A/B-able" asks for.
//
// It consumes the same `FElysiumUserCmd` the box body does (S5) — the A/B is over the *mover*, not
// over the input path, or it would compare two things at once.
UCLASS()
class AElysiumCapsulePawn : public ACharacter, public IElysiumPlayerBody
{
	GENERATED_BODY()

public:
	AElysiumCapsulePawn();

	virtual void BeginPlay() override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	// --- IElysiumPlayerBody ---------------------------------------------------------------
	virtual bool IsNoclip() const override { return bNoclip; }
	virtual void SetNoclip(bool bEnable) override;
	virtual FElysiumEntityHandle GetPlayerEntity() const override { return PlayerEntity; }
	virtual void SetPlayerEntity(const FElysiumEntityHandle& Handle) override { PlayerEntity = Handle; }
	virtual float GetBodyHalfHeight() const override { return GetDefaultHalfHeight(); }
	virtual void SetMovementFrozen(bool bFrozen) override;
	virtual void ApplyUserCmd(const FElysiumUserCmd& Cmd) override;
	virtual UElysiumCameraComponent* GetCameraComponent() const override { return Camera; }

private:
	UPROPERTY() TObjectPtr<UElysiumCameraComponent> Camera;

	bool bNoclip = false;
	bool bWasJumpDown = false;

	FElysiumEntityHandle PlayerEntity;

	// Speeds in cm/s (Source units x 2.54). Run 225 u/s, walk 100 u/s.
	float RunSpeed = 571.0f;
	float WalkSpeed = 254.0f;
	float NoclipSpeed = 1200.0f;
	float NoclipBoost = 3.0f;

	// Push the frame's gait onto the movement component (walk speed, or the noclip fly boost).
	void ApplyGait(bool bWalk);
};
