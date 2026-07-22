#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ElysiumGameInstance.generated.h"

// Session state that survives map travels. Today it only anchors the game-instance
// subsystems (map, console); the character sheet, quest/story state, and save-game
// serialization land here as they are implemented.
UCLASS()
class UElysiumGameInstance : public UGameInstance
{
	GENERATED_BODY()
};
