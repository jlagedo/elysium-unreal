#include "ElysiumPlayerUISubsystem.h"

#include "ElysiumHUDModel.h"
#include "ElysiumInputScope.h"
#include "ElysiumPresentationSubsystem.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumUIRoot.h"

#include "CommonActivatableWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPlayerUI, Log, All);

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

UElysiumPlayerUISubsystem* UElysiumPlayerUISubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	ULocalPlayer* LP = GI ? GI->GetFirstGamePlayer() : nullptr;
	return LP ? LP->GetSubsystem<UElysiumPlayerUISubsystem>() : nullptr;
}

void UElysiumPlayerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Model = NewObject<UElysiumHUDModel>(this);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UElysiumPlayerUISubsystem::OnPostLoadMap);

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

void UElysiumPlayerUISubsystem::Deinitialize()
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

void UElysiumPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	RemoveRoot();
	RebindToWorld(NewPlayerController ? NewPlayerController->GetWorld() : nullptr);
}

void UElysiumPlayerUISubsystem::RebindToWorld(UWorld* World)
{
	// PostLoadMap listeners have no ordering contract. Game flow may request the runtime-loading
	// layer before our own listener runs, so make rebinding idempotent and replace only a root that
	// still belongs to the previous world.
	if (Root && Root->GetWorld() != World)
	{
		RemoveRoot();
	}
	UnbindPresentation();
	UElysiumPresentationSubsystem* Presentation = UElysiumPresentationSubsystem::Get(World);
	if (Presentation)
	{
		BoundPresentation = Presentation;
		ViewPublishedHandle = Presentation->OnViewPublished().AddUObject(
			this, &UElysiumPlayerUISubsystem::OnViewPublished);
		OnViewPublished(Presentation->View());
	}
	EnsureRoot();
}

void UElysiumPlayerUISubsystem::SetPreviewMode(EElysiumHUDPreview Mode)
{
	PreviewMode = Mode;
	static const FElysiumViewState Empty;
	const UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (Model)
	{
		Model->Apply(Presentation ? Presentation->View() : Empty, PreviewMode);
	}
}

void UElysiumPlayerUISubsystem::SetHUDSurfaceVisible(bool bVisible)
{
	if (bHUDSurfaceVisible == bVisible)
	{
		return;
	}
	bHUDSurfaceVisible = bVisible;
	if (Root)
	{
		Root->SetHUDSurfaceVisible(bVisible);
	}
}

UCommonActivatableWidget* UElysiumPlayerUISubsystem::PushWidget(
	EElysiumUILayer Layer,
	TSubclassOf<UCommonActivatableWidget> WidgetClass,
	TFunction<void(UCommonActivatableWidget&)> Init)
{
	EnsureRoot();
	if (!Root || !WidgetClass)
	{
		return nullptr;
	}
	if (!Init)
	{
		Init = [](UCommonActivatableWidget&) {};
	}
	return Root->PushWidget(Layer, WidgetClass, Init);
}

void UElysiumPlayerUISubsystem::RemoveWidget(EElysiumUILayer Layer, UCommonActivatableWidget* Widget)
{
	if (Root)
	{
		Root->RemoveWidget(Layer, Widget);
	}
}

UCommonActivatableWidget* UElysiumPlayerUISubsystem::GetActiveWidget(EElysiumUILayer Layer) const
{
	return Root ? Root->GetActiveWidget(Layer) : nullptr;
}

void UElysiumPlayerUISubsystem::EnsureRoot()
{
	if (Root || !Model)
	{
		return;
	}
	ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
	if (!PC)
	{
		return;
	}
	Root = CreateWidget<UElysiumUIRoot>(PC, UElysiumUIRoot::StaticClass());
	if (Root)
	{
		Root->SetModel(Model);
		Root->SetHUDSurfaceVisible(bHUDSurfaceVisible);
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Root->AddToPlayerScreen(10);
		UE_LOG(LogElysiumPlayerUI, Log, TEXT("Created local-player UI root"));
	}
}

void UElysiumPlayerUISubsystem::RemoveRoot()
{
	if (Root)
	{
		// CommonUI deactivation is what releases each screen's Elysium input scope. Do it before
		// detaching a root during travel/controller replacement so an old world's UI cannot leave
		// UI-only input latched into the next one.
		Root->DeactivateAllScreens();
		Root->RemoveFromParent();
		Root = nullptr;
	}
	DialogueScreen = nullptr;
	ShownDialogue = nullptr;
	ShownDialogueRevision = 0;
}

void UElysiumPlayerUISubsystem::UnbindPresentation()
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

void UElysiumPlayerUISubsystem::OnViewPublished(const FElysiumViewState& View)
{
	if (Model)
	{
		Model->Apply(View, PreviewMode);
	}
	EnsureRoot();
	ReconcileDialogue(View.Dialogue);
}

void UElysiumPlayerUISubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	ULocalPlayer* LP = GetLocalPlayer();
	if (LP && LP->GetWorld() == LoadedWorld)
	{
		RebindToWorld(LoadedWorld);
	}
}

void UElysiumPlayerUISubsystem::ReconcileDialogue(const FElysiumDialogueView& Dialogue)
{
	switch (ElysiumView::ReconcileDialogue(
		ShownDialogue, ShownDialogueRevision, Dialogue))
	{
	case ElysiumView::EDialogueAction::Rebuild:
		if (DialogueScreen && ShownDialogue == Dialogue.Conversation)
		{
			// A turn is new content inside one modal lifetime. Keep the screen, focus and input scope.
			DialogueScreen->ApplyDialogue(Dialogue);
		}
		else
		{
			HideDialogue();
			ShowDialogue(Dialogue);
		}
		if (DialogueScreen)
		{
			ShownDialogue = Dialogue.Conversation;
			ShownDialogueRevision = Dialogue.Revision;
		}
		break;

	case ElysiumView::EDialogueAction::Teardown:
		HideDialogue();
		break;

	case ElysiumView::EDialogueAction::None:
		break;
	}
}

void UElysiumPlayerUISubsystem::ShowDialogue(const FElysiumDialogueView& Dialogue)
{
	DialogueScreen = Cast<UElysiumDialogueScreen>(PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumDialogueScreen::StaticClass(),
		[this, Dialogue](UCommonActivatableWidget& Widget)
		{
			UElysiumDialogueScreen* Screen = CastChecked<UElysiumDialogueScreen>(&Widget);
			Screen->ApplyDialogue(Dialogue);
			Screen->OnChoice.BindUObject(this, &UElysiumPlayerUISubsystem::OnDialogueChoice);
			Screen->ConfigureInputScope(TEXT("Dialogue"), ElysiumInput::Priority::Dialogue);
		}));
}

void UElysiumPlayerUISubsystem::HideDialogue()
{
	if (DialogueScreen)
	{
		RemoveWidget(EElysiumUILayer::GameModal, DialogueScreen);
		DialogueScreen = nullptr;
	}
	ShownDialogue = nullptr;
	ShownDialogueRevision = 0;
}

void UElysiumPlayerUISubsystem::OnDialogueChoice(int32 VisibleIndex)
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		if (VisibleIndex < 0)
		{
			Presentation->DialogueAdvance();
		}
		else
		{
			Presentation->DialogueChoose(VisibleIndex);
		}
	}
}
