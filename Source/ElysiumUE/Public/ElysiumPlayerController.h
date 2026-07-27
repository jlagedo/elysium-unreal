#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ElysiumPlayerController.generated.h"

class AElysiumMapActor;
class FElysiumEntityWorld;

// Player controller for the boot game mode. It hosts UElysiumCheatManager (CheatClass) - a
// UCheatManager is spawned by the controller, so registering the Elysium cheats (Noclip,
// ElysiumTeleport, + the stock UCheatManager execs) needs a controller of ours - and it owns every
// key that is not movement.
//
// That last part is 11.4's half of the pawn demotion (S3): the pawn is the player's *body* and
// keeps collision, movement and the camera; the verbs a key press stands for — +use, dismissing a
// sign, the dev skybox toggle, pause — belong to whatever owns input, which is this class until
// 11.5 (the input scope stack) and 11.6 (the command registry + user command) take them. They also
// have to live somewhere that exists when no pawn does: a menu backdrop world seats none.
UCLASS()
class AElysiumPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AElysiumPlayerController();

	virtual void SetupInputComponent() override;

private:
	// Esc -> UElysiumGameFlowSubsystem::TogglePause (11.3). 11.5 replaces the direct key bind with
	// the input scope stack, and 10.6 gives the verb a rebindable name.
	void OnPauseKey();
	// E — +use: press whatever the entity world's look-cursor is aimed at (P4.2/P4.4).
	void OnUsePressed();
	// Left-click: dismiss an open sign/popup window (P4.10). No-op when none is up.
	void OnPrimaryClick();
	// T — show/hide the 3D skybox (a dev toggle, not a player verb).
	void OnToggleSky();

	// The current map's actor, cached per world so a key press is one weak-pointer check rather
	// than a GameInstance -> map subsystem -> map actor walk. 11.6 replaces the whole path with a
	// named command the registry dispatches.
	AElysiumMapActor* CurrentMap() const;
	FElysiumEntityWorld* CurrentEntityWorld() const;
	mutable TWeakObjectPtr<AElysiumMapActor> CachedMap;
};
