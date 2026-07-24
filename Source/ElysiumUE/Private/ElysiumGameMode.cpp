#include "ElysiumGameMode.h"

#include "ElysiumGameStateSubsystem.h"
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
			// Bare dev load (play.bat <map> / -ElysiumNewGame=0): no New Game seed runs, so `Linux_Wine`
			// is unset. The tutorial's `linux_check` (logic_pythoncheck `G.Linux_Wine == 1`) then reads
			// OnFalse and opens `popup_linux` — the Unofficial Patch's "your Python didn't compile / you
			// are in a Linux Wine environment" warning. That is a false alarm here: the embedded CPython
			// always runs. Seed the sentinel to 1 (as BeginNewGame does) before the map builds, so the
			// check takes OnTrue and the popup stays down on the dev path.
			if (UElysiumGameStateSubsystem* State = GetGameInstance()->GetSubsystem<UElysiumGameStateSubsystem>())
			{
				State->SetGlobalInt(TEXT("Linux_Wine"), 1);
			}
			Maps->Travel(Maps->ResolveBootMap());
		}
	}
}
