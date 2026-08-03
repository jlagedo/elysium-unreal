#pragma once

#include "CoreMinimal.h"
#include "ElysiumWorldServices.h"
#include "GameFramework/Character.h"
#include "ElysiumNpcBody.generated.h"

// The engine half of one mobile NPC. The imported skeletal component remains the visible body and
// is attached at feet-relative offset; this actor supplies the capsule, CharacterMovement, Detour
// crowd following, and the small engine-neutral IElysiumNpcMotor surface.
UCLASS(Transient, NotBlueprintable)
class AElysiumNpcBody final : public ACharacter, public IElysiumNpcMotor
{
	GENERATED_BODY()

public:
	AElysiumNpcBody(const FObjectInitializer& ObjectInitializer);

	void InitializeAtFeet(const FVector& FeetOrigin, float YawDegrees);
	void SetRuntimeReady(bool bReady);

	virtual void Tick(float DeltaSeconds) override;

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false) override;
	virtual void Face(float YawDegrees) override;
	virtual void Stop() override;
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) override;
	virtual void SetEnabled(bool bEnabled) override;
	virtual void SetFrozen(bool bFrozen) override;
	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override;

private:
	FVector FeetLocation() const;
	void ApplyEnabledState();
	// Solidity is three independent decisions — enabled, frozen, and character-ignoring — so it is
	// resolved in one place and re-applied from every one of them.
	void ApplyCollisionState();
	void ApplyCrowdState();
	FVector RequestedFeet = FVector::ZeroVector;
	float RequestedAcceptanceCm = 20.0f;
	float RequestedYaw = 0.0f;
	bool bMoveRequested = false;
	bool bFaceRequested = false;
	bool bRequestedEnabled = true;
	bool bRuntimeReady = false;
	// Borrowed by a cutscene and given back: a frozen body cannot move or be touched but stays on
	// screen; an ignoring body still collides with the world, just not with other characters.
	bool bFrozen = false;
	bool bIgnoreCharacterCollision = false;
};
