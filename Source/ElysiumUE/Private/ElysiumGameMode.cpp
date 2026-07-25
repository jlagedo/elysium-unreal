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

	// Boot: the map subsystem owns all map lifecycle from here on. Under OpenLevel hard travel this
	// BeginPlay runs in every fresh world, so first check whether we arrived here from a Travel: if a
	// map load is pending, this world is the baked level the engine opened for it — spawn that map's
	// actor and stop (no boot decision, no re-seed).
	UElysiumMapSubsystem* Maps = GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>();
	if (!Maps)
	{
		return;
	}
	if (Maps->HasPendingMapLoad())
	{
		Maps->SpawnPendingMap();
		return;
	}

	// Cold boot (no pending load). The default is New Game — seed a fresh story context and enter the
	// tutorial at its landmark — so a bare launch starts the story rather than a bare map load. An
	// explicit -ElysiumMap= (play.bat <map>) keeps the unseeded dev path, and -ElysiumNewGame=0 boots
	// the story map bare for A/B. Both go through Travel, which opens the map's baked level.
	if (Maps->ShouldBootNewGame())
	{
		Maps->NewGame();
	}
	else
	{
		// Bare dev load (play.bat <map>). Chargen is not built yet (game_runtime.md §6), so seed a
		// mock character the same way the New Game command does its initial game data — clan Tremere,
		// male — via BeginNewGame. This binds the player sheet the dialogue gates read (`IsClan(pc,…)`,
		// `pc.base_*`), sets Story_State=-4/Tut_Jack=0/Tut_Patch=0, and seeds Linux_Wine=1 (which also
		// keeps the tutorial's `linux_check` popup down). BeginNewGame only seeds state — it does not
		// travel — so the dev map load below still runs. Interim stand-in for chargen (9.4).
		if (UElysiumGameStateSubsystem* State = GetGameInstance()->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			State->BeginNewGame(FElysiumPlayerSheet::ClanFromName(TEXT("Tremere")), /*bMale*/ true);
		}
		Maps->Travel(Maps->ResolveBootMap());
	}
}
