#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ElysiumGameInstance.generated.h"

// Session state that survives map travels. It anchors the game-instance subsystems: map
// lifecycle/console (UElysiumMapSubsystem) and persistent game state — the `G` store, quest
// map, and game clock (UElysiumGameStateSubsystem). The character sheet and save-game
// serialization land on that state subsystem as they are implemented.
UCLASS()
class UElysiumGameInstance : public UGameInstance
{
	GENERATED_BODY()
};
