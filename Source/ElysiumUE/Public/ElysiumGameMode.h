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
};
