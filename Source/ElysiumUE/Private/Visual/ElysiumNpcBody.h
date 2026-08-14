#pragma once

#include "CoreMinimal.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumWorldServices.h"
#include "GameFramework/Character.h"
#include "Templates/PimplPtr.h"
#include "ElysiumNpcBody.generated.h"

class AElysiumNpcBody;

// CCC4 — the cast's animation pass, in TG_PostPhysics. A SECOND tick function rather than the
// actor's own tick, because the engine wires no prerequisite between an actor's tick and its own
// CharacterMovement: a selection read from `Tick` would be reading whichever of the two happened to
// register first. Declaring it as a class default is also what lets `Elysium.Substrate.FrameOrder`
// assert the ordering, which a runtime `AddPrerequisite` could not. The turn-in-place stays in
// pre-physics where it was.
USTRUCT()
struct FElysiumNpcAnimTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	AElysiumNpcBody* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumNpcAnimTickFunction>
	: public TStructOpsTypeTraitsBase2<FElysiumNpcAnimTickFunction>
{
	enum { WithCopy = false };
};

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
	// The plain-C++ NPC this engine body embodies. Collision ingress uses this identity so an
	// ACharacter overlap remains an NPC toucher instead of being collapsed to !player.
	void SetOwningEntity(const FElysiumEntityHandle& InOwner) { OwningEntity = InOwner; }
	FElysiumEntityHandle GetOwningEntity() const { return OwningEntity; }
	// The model this body wears and the repeatable token its weighted picks ride on. Set once when
	// the motor is built, because that is the one place that knows both.
	void SetModelStem(const FString& InStem, USkeletalMeshComponent* InVisual, int32 InVariant);

	virtual void Tick(float DeltaSeconds) override;
	virtual void RegisterActorTickFunctions(bool bRegister) override;

	// CCC4 — this body's own animation pass, driven from the tick function below.
	void AnimTick(float DeltaSeconds);
	// The frame's selection record. Never null, for the same reason the player's is not.
	const FElysiumAnimationSelection& GetAnimSelection() const;

	// Public so a test can read the declared frame order off the class default.
	UPROPERTY()
	FElysiumNpcAnimTickFunction AnimTickFunction;

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false) override;
	virtual void Face(float YawDegrees) override;
	virtual void Stop() override;
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) override;
	virtual void SetEnabled(bool bEnabled) override;
	virtual void SetFrozen(bool bFrozen) override;
	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override;
	virtual FElysiumLocomotionSample SampleLocomotion() const override;

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
	FElysiumEntityHandle OwningEntity;

	// CCC4 — the same driver the player body runs, on the same contract. Held by value: it is plain
	// C++ with no UObject in it, and it dies with the body.
	TPimplPtr<struct FElysiumAnimationDriver> AnimDriver;
	FString ModelStem;
	int32 AnimVariant = 0;
	TWeakObjectPtr<USkeletalMeshComponent> Visual;
};
