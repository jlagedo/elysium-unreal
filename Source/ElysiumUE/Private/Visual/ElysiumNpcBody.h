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

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond) override;
	virtual void Stop() override;
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) override;
	virtual void SetEnabled(bool bEnabled) override;
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override;

private:
	FVector FeetLocation() const;
	void ApplyEnabledState();
	FVector RequestedFeet = FVector::ZeroVector;
	float RequestedAcceptanceCm = 20.0f;
	bool bMoveRequested = false;
	bool bRequestedEnabled = true;
	bool bRuntimeReady = false;
};
