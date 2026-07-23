#include "ElysiumGameMode.h"

#include "ElysiumHUD.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"

AElysiumGameMode::AElysiumGameMode()
{
	DefaultPawnClass = AElysiumPawn::StaticClass();
	HUDClass = AElysiumHUD::StaticClass();
	PlayerControllerClass = AElysiumPlayerController::StaticClass();
}

void AElysiumGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Boot: the map subsystem owns all map lifecycle from here on. The default boot is New Game —
	// seed a fresh story context and enter the tutorial at its landmark — so a bare launch starts
	// the story rather than a bare map load. An explicit -ElysiumMap= (play.bat <map>) keeps the
	// unseeded dev path, and -ElysiumNewGame=0 boots the story map bare for A/B.
	if (UElysiumMapSubsystem* Maps = GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>())
	{
		if (Maps->ShouldBootNewGame())
		{
			Maps->NewGame();
		}
		else
		{
			Maps->Travel(Maps->ResolveBootMap());
		}
	}
}
