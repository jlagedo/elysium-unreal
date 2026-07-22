#include "ElysiumGameMode.h"

#include "ElysiumHUD.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"

AElysiumGameMode::AElysiumGameMode()
{
	DefaultPawnClass = AElysiumPawn::StaticClass();
	HUDClass = AElysiumHUD::StaticClass();
}

void AElysiumGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Boot travel: the map subsystem owns all map lifecycle from here on.
	if (UElysiumMapSubsystem* Maps = GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>())
	{
		Maps->Travel(Maps->ResolveBootMap());
	}
}
