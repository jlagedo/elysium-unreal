#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumPawn.generated.h"

class UCameraComponent;

// The player's **body** (S3, 11.4): collision, movement, the camera, and the handle of the entity
// it embodies. All player game state — the sheet, health, money, blood, the law counters — lives on
// that entity (`FElysiumPlayer`), and nothing a save would need lives here.
//
// Controls it still owns, because they are movement:
//   WASD           move (run by default)
//   Mouse          look (pitch-clamped)
//   Shift          slow walk (VtMB's +speed gait; run is the default)
//   Space          jump (walk) / ascend (noclip)
//   E / Ctrl       ascend / descend (noclip)
//   V              toggle noclip (fly through geometry)
// The verbs that are not movement (+use, dismissing a sign, the skybox toggle) are the player
// controller's. Walking uses Unreal's CharacterMovementComponent; noclip flies the capsule with
// collision disabled. The box root + `UElysiumMovementComponent` this re-bases onto are 11.6's,
// and Source-accurate acceleration/friction is 4.7's.
UCLASS()
class AElysiumPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AElysiumPawn();

	virtual void BeginPlay() override;

	bool IsNoclip() const { return bNoclip; }
	// Enter/leave noclip (fly through geometry). Drives collision + movement mode; the V key and
	// the `Noclip` cheat both route here.
	void SetNoclip(bool bEnable);

	// The entity this body embodies (11.4). Set by the map actor when the player entity is created;
	// Invalid on a body with no entity behind it (a bare dev world). The body never reads game state
	// through it — it is here so a system holding the pawn can name the entity without a search.
	FElysiumEntityHandle GetPlayerEntity() const { return PlayerEntity; }
	void SetPlayerEntity(const FElysiumEntityHandle& Handle) { PlayerEntity = Handle; }

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

private:
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	bool bNoclip = false;
	// Shift held: VtMB's `+speed` is a press/release pair, so the gait is a latch the bindings set
	// rather than a key the tick polls. 11.6 replaces the latch with a field on FElysiumUserCmd.
	bool bWalkGait = false;

	FElysiumEntityHandle PlayerEntity;

	// Speeds in cm/s (Source units * 2.54). Run 225 u/s, walk 100 u/s.
	float RunSpeed = 571.f;
	float WalkSpeed = 254.f;
	float NoclipSpeed = 1200.f;
	float NoclipBoost = 3.f;   // Shift multiplier in noclip

	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void OnJumpPressed();
	void OnJumpReleased();
	void ToggleNoclip();
	void OnWalkPressed();
	void OnWalkReleased();
	// Push the gait latch onto the movement component (walk speed, or the noclip fly boost).
	void ApplyGait();
};
