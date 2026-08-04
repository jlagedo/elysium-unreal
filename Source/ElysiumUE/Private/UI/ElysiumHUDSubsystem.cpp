#include "ElysiumHUDSubsystem.h"

#include "ElysiumHUDModel.h"
#include "ElysiumPresentationSubsystem.h"
#include "UI/ElysiumHUDRoot.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHUDSubsystem, Log, All);

namespace
{
	EElysiumHUDPreview PreviewFromName(const FString& Name)
	{
		if (Name.Equals(TEXT("passive"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Passive;
		if (Name.Equals(TEXT("combat"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Combat;
		if (Name.Equals(TEXT("weapon"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Weapon;
		if (Name.Equals(TEXT("discipline"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Discipline;
		if (Name.Equals(TEXT("inventory"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Inventory;
		if (Name.Equals(TEXT("critical"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Critical;
		return EElysiumHUDPreview::Off;
	}
}

UElysiumHUDSubsystem* UElysiumHUDSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	ULocalPlayer* LP = GI ? GI->GetFirstGamePlayer() : nullptr;
	return LP ? LP->GetSubsystem<UElysiumHUDSubsystem>() : nullptr;
}

void UElysiumHUDSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Model = NewObject<UElysiumHUDModel>(this);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UElysiumHUDSubsystem::OnPostLoadMap);

#if !UE_BUILD_SHIPPING
	IConsoleObject* PreviewCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.hud.preview"),
		TEXT("elysium.hud.preview off|passive|combat|weapon|discipline|inventory|critical"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			SetPreviewMode(Args.IsEmpty() ? EElysiumHUDPreview::Off : PreviewFromName(Args[0]));
		}), ECVF_Cheat);
	ConsoleObjects.Add(PreviewCommand);
#endif

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		RebindToWorld(LP->GetWorld());
	}
}

void UElysiumHUDSubsystem::Deinitialize()
{
	RemoveRoot();
	UnbindPresentation();
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	for (IConsoleObject* Object : ConsoleObjects)
	{
		if (Object)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Object);
		}
	}
	ConsoleObjects.Reset();
	Model = nullptr;
	Super::Deinitialize();
}

void UElysiumHUDSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	RemoveRoot();
	RebindToWorld(NewPlayerController ? NewPlayerController->GetWorld() : nullptr);
}

void UElysiumHUDSubsystem::RebindToWorld(UWorld* World)
{
	UnbindPresentation();
	UElysiumPresentationSubsystem* Presentation = UElysiumPresentationSubsystem::Get(World);
	if (Presentation)
	{
		BoundPresentation = Presentation;
		ViewPublishedHandle = Presentation->OnViewPublished().AddUObject(
			this, &UElysiumHUDSubsystem::OnViewPublished);
		if (Model)
		{
			Model->Apply(Presentation->View(), PreviewMode);
		}
	}
	EnsureRoot();
}

void UElysiumHUDSubsystem::SetPreviewMode(EElysiumHUDPreview Mode)
{
	PreviewMode = Mode;
	static const FElysiumViewState Empty;
	const UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (Model)
	{
		Model->Apply(Presentation ? Presentation->View() : Empty, PreviewMode);
	}
}

void UElysiumHUDSubsystem::SetHidden(bool bInHidden)
{
	if (bHidden == bInHidden)
	{
		return;
	}
	bHidden = bInHidden;
	if (bHidden)
	{
		RemoveRoot();
	}
	else
	{
		EnsureRoot();
	}
}

void UElysiumHUDSubsystem::EnsureRoot()
{
	// Every publish calls through here, so this is also what keeps the root off screen for as long
	// as something is holding it hidden rather than only until the next frame.
	if (Root || !Model || bHidden)
	{
		return;
	}
	ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
	if (!PC)
	{
		return;
	}
	Root = CreateWidget<UElysiumHUDRoot>(PC, UElysiumHUDRoot::StaticClass());
	if (Root)
	{
		Root->SetModel(Model);
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Root->AddToPlayerScreen(10);
		UE_LOG(LogElysiumHUDSubsystem, Log, TEXT("Created local-player HUD root"));
	}
}

void UElysiumHUDSubsystem::RemoveRoot()
{
	if (Root)
	{
		Root->RemoveFromParent();
		Root = nullptr;
	}
}

void UElysiumHUDSubsystem::UnbindPresentation()
{
	if (ViewPublishedHandle.IsValid())
	{
		if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
		{
			Presentation->OnViewPublished().Remove(ViewPublishedHandle);
		}
		ViewPublishedHandle.Reset();
	}
	BoundPresentation.Reset();
}

void UElysiumHUDSubsystem::OnViewPublished(const FElysiumViewState& View)
{
	if (Model)
	{
		Model->Apply(View, PreviewMode);
	}
	EnsureRoot();
}

void UElysiumHUDSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	ULocalPlayer* LP = GetLocalPlayer();
	if (LP && LP->GetWorld() == LoadedWorld)
	{
		RemoveRoot();
		RebindToWorld(LoadedWorld);
	}
}
