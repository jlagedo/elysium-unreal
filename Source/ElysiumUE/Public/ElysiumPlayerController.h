#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ElysiumPlayerController.generated.h"

// Player controller for the boot game mode. Its only job today is to host UElysiumCheatManager
// (CheatClass) - a UCheatManager is spawned by the controller, so registering the Elysium cheats
// (Noclip, ElysiumTeleport, + the stock UCheatManager execs) needs a controller of ours. The
// natural home for future input/HUD wiring as well.
UCLASS()
class AElysiumPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AElysiumPlayerController();
};
