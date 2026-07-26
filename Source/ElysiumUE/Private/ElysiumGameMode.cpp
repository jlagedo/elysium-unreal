#include "ElysiumGameMode.h"

#include "ElysiumGameStateSubsystem.h"
#include "ElysiumHUD.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"
#include "ElysiumUISubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

// The menu backdrop camera, as "x,y,z,pitch,yaw" in world centimetres and degrees. Deliberately
// NOT an entry in ElysiumVantages::Table: that table is the profiling and screenshot baseline, and
// Resolve("") returns every vantage for a map — adding one there would silently change what
// profile.bat and shots.bat measure. Retune in-game with `elysium.campos`, which logs a
// paste-ready position/rotation including pitch.
static TAutoConsoleVariable<FString> CVarMenuVantage(
	TEXT("elysium.MenuVantage"),
	TEXT("-3160,-1910,-90,0,83"),
	TEXT("Menu backdrop camera as x,y,z,pitch,yaw (world cm / degrees)."),
	ECVF_Default);

// The map the menu stands in. Santa Monica's hub — the Asylum frontage — rather than the story
// entry: it is the game's signature exterior, it carries NPCs who idle in frame, and it is not the
// map New Game enters, so the menu never has to look like the level it is about to load.
static TAutoConsoleVariable<FString> CVarMenuMap(
	TEXT("elysium.MenuMap"),
	TEXT("sm_hub_1"),
	TEXT("Map loaded as the menu backdrop."),
	ECVF_Default);

// Boot to the menu (1) or straight into play (0). -ElysiumMap= already bypasses the menu entirely
// via ShouldBootNewGame, so this is the A/B for the story boot path.
static TAutoConsoleVariable<int32> CVarBootMenu(
	TEXT("elysium.BootMenu"),
	1,
	TEXT("1 = cold boot raises the main menu over a backdrop; 0 = boot straight into New Game."),
	ECVF_Default);

AElysiumGameMode::AElysiumGameMode()
{
	DefaultPawnClass = AElysiumPawn::StaticClass();
	HUDClass = AElysiumHUD::StaticClass();
	PlayerControllerClass = AElysiumPlayerController::StaticClass();
}

UClass* AElysiumGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const UElysiumMapSubsystem* Maps =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (Maps && Maps->IsMenuBackdrop())
	{
		return nullptr;
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void AElysiumGameMode::EnterMenuBackdrop()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Parse the vantage; a malformed cvar falls back to the world origin looking level rather than
	// refusing to show a menu.
	FVector Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	TArray<FString> Parts;
	CVarMenuVantage.GetValueOnGameThread().ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 5)
	{
		Loc = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		Rot = FRotator(FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]), 0.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("elysium.MenuVantage is malformed — using the world origin"));
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Loc, Rot, Params))
	{
		// Blend time zero: the menu is the first thing on screen, so there is nothing to blend from.
		PC->SetViewTarget(Camera);
	}

	if (UElysiumUISubsystem* UI = GetGameInstance()->GetSubsystem<UElysiumUISubsystem>())
	{
		UI->ShowMenu(/*bPauseMode*/ false);
	}
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
		// 8.6 — a backdrop world builds the look only; seat the fixed camera and raise the menu over
		// it. New Game leaves this state by Travelling the same map again with the substrate built.
		if (Maps->IsMenuBackdrop())
		{
			EnterMenuBackdrop();
		}
		return;
	}

	// Cold boot (no pending load). The default is New Game — seed a fresh story context and enter the
	// tutorial at its landmark — so a bare launch starts the story rather than a bare map load. An
	// explicit -ElysiumMap= (play.bat <map>) keeps the unseeded dev path, and -ElysiumNewGame=0 boots
	// the story map bare for A/B. Both go through Travel, which opens the map's baked level.
	if (Maps->ShouldBootNewGame())
	{
		// 8.6 — the story boot now lands on the menu, standing over the story entry map as a
		// backdrop; New Game re-enters the same map for real. `elysium.BootMenu 0` keeps the old
		// straight-into-play boot for A/B.
		if (CVarBootMenu.GetValueOnGameThread() != 0)
		{
			Maps->TravelForMenu(CVarMenuMap.GetValueOnGameThread());
		}
		else
		{
			Maps->NewGame();
		}
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
