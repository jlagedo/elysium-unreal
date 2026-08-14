#include "ElysiumPlayerController.h"

#include "ElysiumCheatManager.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputRouter.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerCameraManager.h"
#include "Debug/ElysiumScreenshot.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumFeed.h"

#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPC, Log, All);

AElysiumPlayerController::AElysiumPlayerController()
{
	// The controller spawns this in non-Shipping / cheats-enabled builds; it is what makes the
	// Elysium (and inherited stock) UFUNCTION(exec) cheats reachable from the console.
	CheatClass = UElysiumCheatManager::StaticClass();

	// The one final view per local player. This has to be the constructor: the manager is spawned
	// from `APlayerController::PostInitializeComponents`, so a class assigned in `BeginPlay` is
	// assigned to something that already exists.
	PlayerCameraManagerClass = AElysiumPlayerCameraManager::StaticClass();
}

void AElysiumPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}
	if (!Router)
	{
		Router = NewObject<UElysiumInputRouter>(this, TEXT("InputRouter"));
	}
	Router->Setup(this, InputComponent);
}

void AElysiumPlayerController::BeginPlay()
{
	Super::BeginPlay();
	RegisterCommands();
}

void AElysiumPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	UnregisterCommands();
	if (Router)
	{
		Router->Shutdown();
	}
	Super::EndPlay(Reason);
}

void AElysiumPlayerController::ProcessPlayerInput(const float DeltaTime, const bool bGamePaused)
{
	// Super runs the input stack, so every Enhanced Input callback for this frame has landed in the
	// builder by the time it returns. Building here rather than after `PlayerTick` is what puts the
	// look delta *ahead* of `UpdateRotation` in the same frame: the engine then applies it, clamps
	// the pitch through the camera manager, writes the control rotation and faces the pawn, all off
	// this frame's input. Building afterwards left `FaceRotation` running on the previous frame's
	// value, which a mouse flick shows as the body trailing the camera.
	Super::ProcessPlayerInput(DeltaTime, bGamePaused);

	// `TickPlayerInput` reaches here on two paths, and only one of them used to run the router:
	// `PlayerTick` on a live frame, and `TickActor`'s pause path, which `PlayerTick` never sees. A
	// held world must keep producing no command at all, so the paused entry stops here — the same
	// thing that was true when this lived behind `PlayerTick`. Pause as an explicit input policy is
	// the mapping-context landing's, not this one's.
	if (!Router || bGamePaused)
	{
		return;
	}

	Router->SampleFrame(DeltaTime);
	const FElysiumUserCmd Sampled = Router->CurrentCmd();
	FElysiumUserCmd Current = Sampled;
	FElysiumEntityWorld* EntityWorld = CurrentEntityWorld();
	if (const FElysiumPlayer* FeedPlayer = EntityWorld ? EntityWorld->FindPlayer() : nullptr;
		FeedPlayer && FeedPlayer->IsFeedPaired())
	{
		Current = ElysiumFeed::GatePairedUserCmd(Current);
		// SampleFrame already published the unfiltered player intent to the body. Replace that pending
		// snapshot before movement runs; look remains unchanged and is still integrated once below.
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(GetPawn()))
		{
			Body->ApplyUserCmd(Current);
		}
	}

	// Degrees, straight into the engine's own rotation input. `bEnableLegacyInputScales` is off, so
	// `AddYawInput`/`AddPitchInput` accumulate without a scale and the degrees in the command are
	// the degrees applied — the property the user command exists to hold.
	if (!Current.LookDelta.IsNearlyZero())
	{
		AddYawInput(Current.LookDelta.X);
		AddPitchInput(Current.LookDelta.Y);
	}

	if (FElysiumEntityWorld* World = EntityWorld)
	{
		if (Current.JustPressed(EElysiumButton::Use, PreviousCmd))
		{
			World->QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		}
		if (Current.JustReleased(EElysiumButton::Use, PreviousCmd))
		{
			World->QueuePlayerUseEdge(EElysiumUseEdge::Released);
		}
		// B6 — `+feed` / `-feed` remain the low-level command transport. Gameplay consumes them as a
		// toggle: a press starts or requests release, while button-up is inert. The accepted action
		// owns its own continuation latch from there on
		// (`docs/vtmb/feeding.md` § "Command and initial request").
		if (Current.JustPressed(EElysiumButton::Feed, PreviousCmd))
		{
			World->QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
		}
		if (Current.JustReleased(EElysiumButton::Feed, PreviousCmd))
		{
			World->QueuePlayerFeedEdge(EElysiumUseEdge::Released);
		}
	}
	// Edge history remains the raw physical sample. Otherwise an attack/use held through the paired
	// gate would look freshly pressed on the first free frame after release.
	PreviousCmd = Sampled;
}


void AElysiumPlayerController::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// `+attack` — until weapons exist (4.9) the primary click's only job is dismissing an open sign
	// panel, which is what every VtMB popup instructs ("left-click to continue"). The world no-ops
	// when none is up, and MinShowTime holds the panel so a click already in flight cannot skip it.
	Bindings.Add(Registry.Bind(TEXT("attack"), [this](const FElysiumCommandCall& Call)
	{
		if (!Call.bPressed)
		{
			return;
		}
		if (FElysiumEntityWorld* World = CurrentEntityWorld())
		{
			World->PlayerDismissSign();
		}
	}));

	Bindings.Add(Registry.Bind(TEXT("noclip"), [this](const FElysiumCommandCall&)
	{
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(GetPawn()))
		{
			Body->SetNoclip(!Body->IsNoclip());
			UE_LOG(LogElysiumPC, Display, TEXT("noclip %s"), Body->IsNoclip() ? TEXT("on") : TEXT("off"));
		}
	}));

	// `god` writes the same latch `events_player`'s MakePlayerUnkillable does — one gate, whether the
	// map asks for it or the player does.
	Bindings.Add(Registry.Bind(TEXT("god"), [this](const FElysiumCommandCall&)
	{
		FElysiumEntityWorld* World = CurrentEntityWorld();
		FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
		if (!Player)
		{
			UE_LOG(LogElysiumPC, Warning, TEXT("god: no player entity in this world"));
			return;
		}
		Player->SetUnkillable(!Player->IsUnkillable());
		UE_LOG(LogElysiumPC, Display, TEXT("god %s"), Player->IsUnkillable() ? TEXT("on") : TEXT("off"));
	}));

	// `teleport_player <targetname>` / `teleport_player <x> <y> <z>` — the vampire.dll verb, whose own
	// help string is "Teleports the player to a named entity, or to an X Y Z coordinate". Both forms
	// are reproduced because the image accepts both; the named form is the one the content uses, and
	// it is how the chargen wizard leaves genesis (`teleport_player firetrans`).
	Bindings.Add(Registry.Bind(TEXT("teleport_player"), [this](const FElysiumCommandCall& Call)
	{
		if (FElysiumEntityWorld* World = CurrentEntityWorld())
		{
			ElysiumCommands::TeleportPlayer(*World, Call.Args);
		}
	}));

	// The server-authoritative half of the loot panel. CommonUI is intentionally absent from this
	// slice: +use opens a container session and this verb performs Take/Give against that session.
	Bindings.Add(Registry.Bind(TEXT("vbarter"), [this](const FElysiumCommandCall& Call)
	{
		if (FElysiumEntityWorld* World = CurrentEntityWorld())
		{
			ElysiumItems::ExecuteBarter(*World, Call.Args);
		}
	}));

	Bindings.Add(Registry.Bind(TEXT("snapshot"), [](const FElysiumCommandCall&)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") /
			FString::Printf(TEXT("elysium_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		ElysiumScreenshot::Request([Path](int32 W, int32 H, const TArray<FColor>& Bitmap)
		{
			if (W > 0 && ElysiumScreenshot::SavePng(W, H, Bitmap, Path))
			{
				UE_LOG(LogElysiumPC, Display, TEXT("snapshot -> %s"), *Path);
			}
		}, /*TimeoutFrames*/ 300, /*bShowUI*/ true);
	}));

	Bindings.RemoveAll([](const FElysiumCommandBinding& B) { return !B.IsValid(); });
}

void AElysiumPlayerController::UnregisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();
	for (FElysiumCommandBinding& Binding : Bindings)
	{
		Registry.Unbind(Binding);
	}
	Bindings.Reset();
}

AElysiumMapActor* AElysiumPlayerController::CurrentMap() const
{
	if (AElysiumMapActor* Cached = CachedMap.Get())
	{
		return Cached;
	}
	const UGameInstance* GI = GetGameInstance();
	const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	CachedMap = Map;
	return Map;
}

FElysiumEntityWorld* AElysiumPlayerController::CurrentEntityWorld() const
{
	AElysiumMapActor* Map = CurrentMap();
	return Map ? Map->GetEntityWorld() : nullptr;
}
