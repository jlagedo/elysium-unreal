#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ElysiumGameMode.generated.h"

// The per-world game mode: the pawn/HUD/controller classes, the no-pawn-in-front-end rule, and
// one `NotifyWorldReady` on BeginPlay. Boot, the app state machine and the session live on
// UElysiumGameFlowSubsystem — a game mode is per-world, and all three must survive travel
// (roadmap 11.3, `docs/architecture/runtime-architecture.md` §10).
UCLASS()
class AElysiumGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AElysiumGameMode();

	virtual void BeginPlay() override;

	// The empty front-end shell seats no pawn. Runs during PostLogin, which is why quit-to-menu
	// latches the mode before opening the shell. Cold boot reaches the latch during BeginPlay, after
	// PostLogin, but the shell contains no game map and the static menu fully owns presentation.
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
};
