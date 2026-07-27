#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "UObject/Interface.h"

#include "ElysiumPlayerBody.generated.h"

class UElysiumCameraComponent;
struct FElysiumUserCmd;

UINTERFACE()
class UElysiumPlayerBody : public UInterface
{
	GENERATED_BODY()
};

// The player's **body**, as everything outside it needs it (S3, roadmap 11.4/11.6). All player game
// state lives on the player *entity* (`FElysiumPlayer`); a body is collision, movement, the camera
// and nothing else, which is exactly the surface here.
//
// It is an interface because 11.6 ships two of them and they cannot share a base: `AElysiumPawn`
// (`APawn` + a **box** root + `UElysiumMovementComponent` — the faithful hull, `source_movement.md`)
// and `AElysiumCapsulePawn` (`ACharacter`, the A/B baseline behind `elysium.SourceMovement 0`).
// `ACharacter` creates its capsule as its root and does not allow substitution, so the split is the
// engine's, not a preference.
class IElysiumPlayerBody
{
	GENERATED_BODY()

public:
	// Fly through geometry. Drives collision and the movement mode; the debug key and the `noclip`
	// verb both route here.
	virtual bool IsNoclip() const = 0;
	virtual void SetNoclip(bool bEnable) = 0;

	// The entity this body embodies. Invalid on a body with no entity behind it (a bare dev world).
	// The body never reads game state through it — it is here so a system holding the body can name
	// the entity without a search.
	virtual FElysiumEntityHandle GetPlayerEntity() const = 0;
	virtual void SetPlayerEntity(const FElysiumEntityHandle& Handle) = 0;

	// Half the body's height, in cm. Source places an entity's absorigin at its **feet** and both
	// bodies are centred, so the teleport seam asks the body rather than assuming a shape.
	virtual float GetBodyHalfHeight() const = 0;

	// Hold the body still — no gravity, no movement — while the map's async collision cook produces
	// ground under the spawn point. Releasing is the map actor's call.
	virtual void SetMovementFrozen(bool bFrozen) = 0;

	// One frame of intent (S5). The router calls this after it has built the command and applied the
	// look delta to the controller; everything else the body does with it is the body's business.
	virtual void ApplyUserCmd(const FElysiumUserCmd& Cmd) = 0;

	// The body's camera (11.7): the weight stack, the boom solve and the scripted-shot channel. Both
	// bodies carry the same one, so the `elysium.SourceMovement` A/B compares the movers and not two
	// camera paths. Never null on a spawned body.
	virtual UElysiumCameraComponent* GetCameraComponent() const = 0;
};
