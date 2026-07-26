#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ElysiumGameMode.generated.h"

// Boot game mode: spawns a flying/colliding DefaultPawn and, on BeginPlay, the world
// actor that loads the map from the runtime intermediates. Set as the project's default
// game mode so an otherwise-empty boot level renders the map.
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

private:
	// Stand the menu camera at the vantage `elysium.MenuVantage` names and raise the menu.
	void EnterMenuBackdrop();
};
