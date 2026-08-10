#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumLocomotionSample.h"
#include "UObject/Interface.h"

#include "ElysiumPlayerBody.generated.h"

class UElysiumCameraComponent;
class USkeletalMeshComponent;
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
// It is an interface rather than a base class because the player body is `AElysiumPawn` (`APawn` +
// a **box** root + `UElysiumMovementComponent` — the faithful hull, `docs/vtmb/source_movement.md`)
// while the bodies a view can retarget to are not all pawns.
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

	// The frame's settled body state (CCC1) — the same record the NPC motor publishes, so the
	// player's locomotion and the cast's cannot become two systems that happen to play the same
	// files (`docs/architecture/animation-architecture.md` §3.2). It is on the interface rather than
	// on the pawn because both bodies can answer it: the box body hands back what its mover
	// published, the capsule body derives it from CharacterMovement.
	virtual FElysiumLocomotionSample GetLocomotionSample() const = 0;

	// The body's camera (11.7): the weight stack, the boom solve and the scripted-shot channel.
	// Never null on a spawned body.
	virtual UElysiumCameraComponent* GetCameraComponent() const = 0;

	// The skeletal surface shared by both movement implementations. It is animation-only and
	// non-solid; the pawn's hull remains the authoritative body. Camera alpha is applied as a
	// masked-dither scalar, with a hard hidden fast path only at true zero.
	virtual USkeletalMeshComponent* GetPlayerVisual() const = 0;
	virtual void SetPlayerVisual(USkeletalMeshComponent* InVisual) = 0;
	virtual void ApplyPlayerModelAlpha(float Alpha) = 0;
};
