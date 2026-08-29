#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ElysiumGameInstance.generated.h"

// Session state that survives map travels. It anchors the game-instance subsystems: map
// lifecycle/console (UElysiumMapSubsystem) and persistent game state — the `G` store, quest
// map, game clock, player record and save serialization (UElysiumGameStateSubsystem).
UCLASS()
class UElysiumGameInstance : public UGameInstance
{
	GENERATED_BODY()
};
