#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ElysiumGameMode.generated.h"

// The per-world game mode: the pawn/HUD/controller classes, the no-pawn-on-a-backdrop rule, and
// one `NotifyWorldReady` on BeginPlay. Boot, the app state machine and the session live on
// UElysiumGameFlowSubsystem — a game mode is per-world, and all three must survive travel
// (roadmap 11.3, `docs/runtime-architecture.md` §10).
UCLASS()
class AElysiumGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AElysiumGameMode();

	virtual void BeginPlay() override;

	// 8.6 — a menu backdrop world seats no pawn: the player is looking at a fixed camera while the
	// menu is up, and a pawn would have nothing to stand on (a backdrop builds no collision). Runs
	// during PostLogin, which is why the map subsystem latches the mode at Travel time rather than
	// when the map actor is spawned.
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
};
