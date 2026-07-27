#include "ElysiumPlayerController.h"

#include "ElysiumCheatManager.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputRouter.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumScreenshot.h"

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

void AElysiumPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (Router)
	{
		Router->SampleFrame(DeltaTime);
	}
}

void AElysiumPlayerController::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// `+use` — press whatever the entity world's look cursor is aimed at (P4.2/P4.4). The release
	// half latches IN_USE and does nothing else, which is what VtMB's own `+use` does.
	Bindings.Add(Registry.Bind(TEXT("use"), [this](const FElysiumCommandCall& Call)
	{
		if (!Call.bPressed)
		{
			return;
		}
		if (FElysiumEntityWorld* World = CurrentEntityWorld())
		{
			World->PlayerUse();
		}
	}));

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
