#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ElysiumPawn.generated.h"

class UCameraComponent;

// First-person player matching the Godot build's controls:
//   WASD           move (run by default)
//   Mouse          look (pitch-clamped)
//   Shift          slow walk (VtMB's +speed gait; run is the default)
//   Space          jump (walk) / ascend (noclip)
//   E / Ctrl       ascend / descend (noclip)
//   V              toggle noclip (fly through geometry)
//   T              toggle the 3D skybox
// Walking uses Unreal's CharacterMovementComponent (gravity, floor, stair-stepping);
// noclip flies the capsule with collision disabled. Source-accurate acceleration/friction
// is an M1 refinement over this.
UCLASS()
class AElysiumPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AElysiumPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool IsNoclip() const { return bNoclip; }
	// Enter/leave noclip (fly through geometry). Drives collision + movement mode; the V key and
	// the `Noclip` cheat both route here.
	void SetNoclip(bool bEnable);

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

private:
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	bool bNoclip = false;

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
	void ToggleSky();
	void ToggleDebug();
	// E — +use: press whatever the entity world's look-cursor is aimed at (P4.2). The full
	// use-icon HUD + use-only trace channel land in P4.4; this fires the aimed button's press.
	void OnUsePressed();
};
