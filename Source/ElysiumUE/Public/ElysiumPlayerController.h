#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ElysiumPlayerController.generated.h"

// Player controller for the boot game mode. It hosts UElysiumCheatManager (CheatClass) - a
// UCheatManager is spawned by the controller, so registering the Elysium cheats (Noclip,
// ElysiumTeleport, + the stock UCheatManager execs) needs a controller of ours - and it owns the
// pause key, which belongs here rather than on the pawn because a menu backdrop world seats no pawn.
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
};
