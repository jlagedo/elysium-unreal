#include "ElysiumPlayerUISubsystem.h"

#include "ElysiumHUDModel.h"
#include "ElysiumInputScope.h"
#include "ElysiumPresentationSubsystem.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumLootScreen.h"
#include "UI/ElysiumNotificationScreen.h"
#include "UI/ElysiumSignScreen.h"
#include "UI/ElysiumTerminalScreen.h"
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
	constexpr int32 MaxQueuedNotifications = 64;

	EElysiumHUDPreview PreviewFromName(const FString& Name)
	{
		if (Name.Equals(TEXT("passive"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Passive;
		if (Name.Equals(TEXT("combat"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Combat;
		if (Name.Equals(TEXT("weapon"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Weapon;
		if (Name.Equals(TEXT("discipline"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Discipline;
		if (Name.Equals(TEXT("inventory"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Inventory;
		if (Name.Equals(TEXT("critical"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Critical;
		if (Name.Equals(TEXT("radial"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Radial;
		if (Name.Equals(TEXT("brief"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Brief;
		if (Name.Equals(TEXT("elysium"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Elysium;
		if (Name.Equals(TEXT("sneak"), ESearchCase::IgnoreCase)) return EElysiumHUDPreview::Sneak;
		return EElysiumHUDPreview::Off;
	}

	bool IsKnownPreviewName(const FString& Name)
	{
		return Name.Equals(TEXT("off"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("passive"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("combat"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("weapon"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("discipline"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("inventory"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("critical"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("radial"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("brief"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("elysium"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("sneak"), ESearchCase::IgnoreCase);
	}

	bool NotificationKindFromName(const FString& Name, EElysiumNotificationKind& OutKind)
	{
		if (Name.Equals(TEXT("item"), ESearchCase::IgnoreCase))
		{
			OutKind = EElysiumNotificationKind::ItemAcquired;
			return true;
		}
		if (Name.Equals(TEXT("quest"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("updated"), ESearchCase::IgnoreCase))
		{
			OutKind = EElysiumNotificationKind::QuestUpdated;
			return true;
		}
		if (Name.Equals(TEXT("complete"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
		{
			OutKind = EElysiumNotificationKind::QuestCompleted;
			return true;
		}
		if (Name.Equals(TEXT("failure"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
		{
			OutKind = EElysiumNotificationKind::QuestFailed;
			return true;
		}
		if (Name.Equals(TEXT("generic"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("notice"), ESearchCase::IgnoreCase))
		{
			OutKind = EElysiumNotificationKind::Generic;
			return true;
		}
		return false;
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
		TEXT("elysium.hud.preview off|passive|combat|weapon|discipline|inventory|critical|radial|brief|elysium|sneak"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (!Args.IsEmpty() && !IsKnownPreviewName(Args[0]))
			{
				UE_LOG(LogElysiumPlayerUI, Warning,
					TEXT("elysium.hud.preview: unknown mode '%s'"), *Args[0]);
			}
			SetPreviewMode(Args.IsEmpty() ? EElysiumHUDPreview::Off : PreviewFromName(Args[0]));
		}), ECVF_Cheat);
	ConsoleObjects.Add(PreviewCommand);

	IConsoleObject* NotificationCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.hud.notify"),
		TEXT("elysium.hud.notify item|quest|complete|failure|generic <text> [quantity]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			ExecuteNotificationPreview(Args);
		}), ECVF_Cheat);
	ConsoleObjects.Add(NotificationCommand);
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
		NotificationHandle = Presentation->OnNotification().AddUObject(
			this, &UElysiumPlayerUISubsystem::OnNotification);
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
	if (!Root)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("UI push failed: no local-player UI root for layer %d, widget %s"),
			static_cast<int32>(Layer), *GetNameSafe(WidgetClass.Get()));
		return nullptr;
	}
	if (!WidgetClass)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("UI push failed: null widget class for layer %d"), static_cast<int32>(Layer));
		return nullptr;
	}
	if (!Init)
	{
		Init = [](UCommonActivatableWidget&) {};
	}
	UCommonActivatableWidget* Result = Root->PushWidget(Layer, WidgetClass, Init);
	if (!Result)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("UI push failed: %s was not created on layer %d"),
			*GetNameSafe(WidgetClass.Get()), static_cast<int32>(Layer));
	}
	return Result;
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
		Root->SetNotificationSurfaceVisible(bNotificationSurfaceAvailable);
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
	SignScreen = nullptr;
	LootScreen = nullptr;
	TerminalScreen = nullptr;
	NotificationScreens.Reset();
	ShownDialogSerial = 0;
	ShownSign = nullptr;
	ShownDialogueRevision = 0;
	ShownLootOwner = FElysiumEntityHandle::Invalid();
	ShownLootRevision = 0;
	ShownTerminalOwner = FElysiumEntityHandle::Invalid();
	ShownTerminalSerial = 0;
	ShownTerminalRevision = 0;
}

void UElysiumPlayerUISubsystem::UnbindPresentation()
{
	if (NotificationHandle.IsValid())
	{
		if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
		{
			Presentation->OnNotification().Remove(NotificationHandle);
		}
		NotificationHandle.Reset();
	}
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
	SetNotificationSurfaceAvailable(View.bPlayerSurface && !View.Camera.bScriptedCameraOwnsView
		&& !View.Sign && !View.Dialogue.IsOpen() && !View.Loot.IsOpen()
		&& !View.Terminal.IsOpen());
	ReconcileSign(View);
	ReconcileDialogue(View.Dialogue);
	ReconcileLoot(View.Loot);
	ReconcileTerminal(View.Terminal);
}

void UElysiumPlayerUISubsystem::OnNotification(const FElysiumNotification& Notification)
{
	if (NotificationScreens.Num() >= MaxQueuedNotifications)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("notification UI queue full (%d): dropping newest %s '%s'"),
			MaxQueuedNotifications, ElysiumNotificationKindName(Notification.Kind),
			*Notification.Subject);
		return;
	}

	UElysiumNotificationScreen* Screen = Cast<UElysiumNotificationScreen>(PushWidget(
		EElysiumUILayer::Notification,
		UElysiumNotificationScreen::StaticClass(),
		[this, Notification](UCommonActivatableWidget& Widget)
		{
			UElysiumNotificationScreen* NotificationScreen =
				CastChecked<UElysiumNotificationScreen>(&Widget);
			NotificationScreen->ApplyNotification(Notification);
			NotificationScreen->SetSuspended(!bNotificationSurfaceAvailable);
			NotificationScreen->OnFinished = FOnElysiumNotificationFinished::CreateUObject(
				this, &UElysiumPlayerUISubsystem::OnNotificationFinished);
		}));
	if (Screen)
	{
		NotificationScreens.Add(Screen);
	}
}

void UElysiumPlayerUISubsystem::OnNotificationFinished(UElysiumNotificationScreen* Screen)
{
	if (!Screen)
	{
		return;
	}
	RemoveWidget(EElysiumUILayer::Notification, Screen);
	NotificationScreens.Remove(Screen);
}

void UElysiumPlayerUISubsystem::SetNotificationSurfaceAvailable(bool bAvailable)
{
	bNotificationSurfaceAvailable = bAvailable;
	if (Root)
	{
		Root->SetNotificationSurfaceVisible(bAvailable);
	}
	for (UElysiumNotificationScreen* Screen : NotificationScreens)
	{
		if (Screen)
		{
			Screen->SetSuspended(!bAvailable);
		}
	}
}

void UElysiumPlayerUISubsystem::ExecuteNotificationPreview(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("elysium.hud.notify item|quest|complete|failure|generic <text> [quantity]"));
		return;
	}

	FElysiumNotification Notification;
	if (!NotificationKindFromName(Args[0], Notification.Kind))
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("elysium.hud.notify: unknown kind '%s'"), *Args[0]);
		return;
	}

	int32 SubjectEnd = Args.Num();
	if (Notification.Kind == EElysiumNotificationKind::ItemAcquired && Args.Num() > 2)
	{
		int32 ParsedQuantity = 0;
		if (LexTryParseString(ParsedQuantity, *Args.Last()) && ParsedQuantity > 0)
		{
			Notification.Quantity = ParsedQuantity;
			--SubjectEnd;
		}
	}
	for (int32 Index = 1; Index < SubjectEnd; ++Index)
	{
		Notification.Subject += (Index > 1 ? TEXT(" ") : TEXT("")) + Args[Index];
	}
	Notification.Subject = Notification.Subject.TrimQuotes().TrimStartAndEnd();
	if (Notification.Subject.IsEmpty())
	{
		UE_LOG(LogElysiumPlayerUI, Warning, TEXT("elysium.hud.notify: message text is empty"));
		return;
	}

	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		Presentation->PostNotification(Notification);
		return;
	}
	UE_LOG(LogElysiumPlayerUI, Warning,
		TEXT("elysium.hud.notify: no presentation publisher is bound"));
}

void UElysiumPlayerUISubsystem::ReconcileSign(const FElysiumViewState& View)
{
	if (!View.Sign || !View.Sign->bParsed || !UElysiumSignScreen::ShouldDrawSigns())
	{
		HideSign();
		return;
	}
	if (!SignScreen || ShownSign != View.Sign)
	{
		HideSign();
		ShowSign(View);
		return;
	}
	SignScreen->ApplySign(*View.Sign, View.SignAlpha, View.bSignDismissible);
}

void UElysiumPlayerUISubsystem::ShowSign(const FElysiumViewState& View)
{
	if (!View.Sign)
	{
		return;
	}
	SignScreen = Cast<UElysiumSignScreen>(PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumSignScreen::StaticClass(),
		[this, View](UCommonActivatableWidget& Widget)
		{
			UElysiumSignScreen* Screen = CastChecked<UElysiumSignScreen>(&Widget);
			Screen->ApplySign(*View.Sign, View.SignAlpha, View.bSignDismissible);
			Screen->OnDismiss.BindUObject(this, &UElysiumPlayerUISubsystem::OnSignDismiss);
			Screen->ConfigureScreenPolicy(EElysiumUIScreenKind::Sign);
		}));
	ShownSign = SignScreen ? View.Sign : nullptr;
}

void UElysiumPlayerUISubsystem::HideSign()
{
	if (SignScreen)
	{
		RemoveWidget(EElysiumUILayer::GameModal, SignScreen);
		SignScreen = nullptr;
	}
	ShownSign = nullptr;
}

void UElysiumPlayerUISubsystem::OnSignDismiss()
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		if (Presentation->DismissSign())
		{
			// Pop the CommonUI leaf now so its input scope deactivates in the same route that accepted
			// dismissal. Publish remains the authoritative repair path for scripted closes/replacements.
			HideSign();
		}
	}
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
		ShownDialogSerial, ShownDialogueRevision, Dialogue, bShownDialogueSpeaking))
	{
	case ElysiumView::EDialogueAction::Rebuild:
		if (DialogueScreen && ShownDialogSerial == Dialogue.DialogSerial)
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
			ShownDialogSerial = Dialogue.DialogSerial;
			ShownDialogueRevision = Dialogue.Revision;
			bShownDialogueSpeaking = Dialogue.bNpcSpeaking;
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
			Screen->OnSkip.BindUObject(this, &UElysiumPlayerUISubsystem::OnDialogueSkip);
			Screen->ConfigureScreenPolicy(EElysiumUIScreenKind::Dialogue);
		}));
}

void UElysiumPlayerUISubsystem::HideDialogue()
{
	if (DialogueScreen)
	{
		RemoveWidget(EElysiumUILayer::GameModal, DialogueScreen);
		DialogueScreen = nullptr;
	}
	ShownDialogSerial = 0;
	ShownDialogueRevision = 0;
	bShownDialogueSpeaking = false;
}

void UElysiumPlayerUISubsystem::OnDialogueChoice(int32 VisibleIndex, int32 LineId)
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		if (VisibleIndex < 0)
		{
			Presentation->DialogueAdvance();
		}
		else
		{
			// The row's `.dlg` id travels with its position so the world can refuse a pick aimed at
			// a band it has already replaced.
			Presentation->DialogueChoose(VisibleIndex, LineId);
		}
	}
}

void UElysiumPlayerUISubsystem::OnDialogueSkip()
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		Presentation->DialogueSkip();
	}
}

void UElysiumPlayerUISubsystem::ReconcileLoot(const FElysiumLootView& Loot)
{
	if (!Loot.IsOpen())
	{
		HideLoot();
		return;
	}
	if (!LootScreen || ShownLootOwner != Loot.Owner)
	{
		HideLoot();
		ShowLoot(Loot);
		return;
	}
	if (ShownLootRevision != Loot.Revision)
	{
		LootScreen->ApplyLoot(Loot);
		ShownLootRevision = Loot.Revision;
	}
}

void UElysiumPlayerUISubsystem::ShowLoot(const FElysiumLootView& Loot)
{
	LootScreen = Cast<UElysiumLootScreen>(PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumLootScreen::StaticClass(),
		[this, Loot](UCommonActivatableWidget& Widget)
		{
			UElysiumLootScreen* Screen = CastChecked<UElysiumLootScreen>(&Widget);
			Screen->ApplyLoot(Loot);
			Screen->OnTransfer.BindUObject(this, &UElysiumPlayerUISubsystem::OnLootTransfer);
			Screen->OnClose.BindUObject(this, &UElysiumPlayerUISubsystem::OnLootClose);
			Screen->ConfigureScreenPolicy(EElysiumUIScreenKind::Loot);
		}));
	if (!LootScreen)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("loot UI failed to open for owner %s revision %u"),
			*Loot.Owner.ToString(), Loot.Revision);
	}
	ShownLootOwner = LootScreen ? Loot.Owner : FElysiumEntityHandle::Invalid();
	ShownLootRevision = LootScreen ? Loot.Revision : 0;
}

void UElysiumPlayerUISubsystem::HideLoot()
{
	if (LootScreen)
	{
		RemoveWidget(EElysiumUILayer::GameModal, LootScreen);
		LootScreen = nullptr;
	}
	ShownLootOwner = FElysiumEntityHandle::Invalid();
	ShownLootRevision = 0;
}

void UElysiumPlayerUISubsystem::OnLootTransfer(bool bTake, int32 Slot)
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		const bool bSucceeded = bTake
			? Presentation->LootTake(Slot)
			: Presentation->LootGive(Slot);
		if (!bSucceeded)
		{
			UE_LOG(LogElysiumPlayerUI, Warning,
				TEXT("loot UI action failed: %s slot %d"), bTake ? TEXT("take") : TEXT("store"), Slot);
		}
		return;
	}
	UE_LOG(LogElysiumPlayerUI, Warning,
		TEXT("loot UI action failed: presentation subsystem is unavailable (%s slot %d)"),
		bTake ? TEXT("take") : TEXT("store"), Slot);
}

void UElysiumPlayerUISubsystem::OnLootClose()
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		if (Presentation->CloseLoot())
		{
			HideLoot();
			return;
		}
		UE_LOG(LogElysiumPlayerUI, Warning, TEXT("loot UI close failed in the substrate"));
		return;
	}
	UE_LOG(LogElysiumPlayerUI, Warning,
		TEXT("loot UI close failed: presentation subsystem is unavailable"));
}

void UElysiumPlayerUISubsystem::ReconcileTerminal(const FElysiumTerminalView& Terminal)
{
	if (!Terminal.IsOpen())
	{
		HideTerminal();
		return;
	}
	if (!TerminalScreen || ShownTerminalOwner != Terminal.Owner
		|| ShownTerminalSerial != Terminal.SessionSerial)
	{
		HideTerminal();
		ShowTerminal(Terminal);
		return;
	}
	TerminalScreen->ApplyTerminal(Terminal);
	ShownTerminalRevision = Terminal.Revision;
}

void UElysiumPlayerUISubsystem::ShowTerminal(const FElysiumTerminalView& Terminal)
{
	TerminalScreen = Cast<UElysiumTerminalScreen>(PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumTerminalScreen::StaticClass(),
		[this, Terminal](UCommonActivatableWidget& Widget)
		{
			UElysiumTerminalScreen* Screen = CastChecked<UElysiumTerminalScreen>(&Widget);
			Screen->ApplyTerminal(Terminal);
			Screen->OnCommand.BindUObject(this, &UElysiumPlayerUISubsystem::OnTerminalCommand);
			Screen->OnCharacter.BindUObject(this, &UElysiumPlayerUISubsystem::OnTerminalCharacter);
			Screen->OnAcknowledge.BindUObject(this,
				&UElysiumPlayerUISubsystem::OnTerminalAcknowledge);
			Screen->OnQuit.BindUObject(this, &UElysiumPlayerUISubsystem::OnTerminalQuit);
			Screen->OnBreak.BindUObject(this, &UElysiumPlayerUISubsystem::OnTerminalBreak);
			Screen->OnDraft.BindUObject(this, &UElysiumPlayerUISubsystem::OnTerminalDraft);
			Screen->ConfigureScreenPolicy(EElysiumUIScreenKind::Terminal);
		}));
	if (!TerminalScreen)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI failed to open for owner %s serial %u revision %u"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial, Terminal.Revision);
		ShownTerminalOwner = FElysiumEntityHandle::Invalid();
		ShownTerminalSerial = 0;
		ShownTerminalRevision = 0;
		return;
	}

	// The glass is a world-lifetime fact now: `AElysiumMapActor::RegisterUseAnchor` stood this
	// terminal's projection when its body was registered, and the authority refused the session at
	// `BeginPlayerUse` if the machine had no `screen`/`screen_axis` to sit in front of. So a missing
	// projection is a diagnostic, not a reason to close a session the authority already opened —
	// submitting `quit` here would fight the substrate for a decision that is not this layer's.
	UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (Presentation && !Presentation->FindTerminalProjection(Terminal.Owner))
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI opened for owner %s serial %u with no world projection bound; the "
				"session is live but its screen is not on the monitor"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
	}
	ShownTerminalOwner = Terminal.Owner;
	ShownTerminalSerial = Terminal.SessionSerial;
	ShownTerminalRevision = Terminal.Revision;
}

void UElysiumPlayerUISubsystem::HideTerminal()
{
	// Nothing is handed over on the way out: the authority re-arms its screensaver at `ss_start` in
	// `EndPlayerUse` and the projection keeps drawing whatever the entity writes, with or without
	// this screen (`docs/vtmb/computer-terminals.md` §13).
	if (TerminalScreen)
	{
		RemoveWidget(EElysiumUILayer::GameModal, TerminalScreen);
		TerminalScreen = nullptr;
	}
	// The third of the draft's three clears: the keyboard is gone, so the glass must stop showing
	// a line nobody is typing any more.
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		Presentation->SetTerminalDraft(FElysiumEntityHandle::Invalid(), FString());
	}
	ShownTerminalOwner = FElysiumEntityHandle::Invalid();
	ShownTerminalSerial = 0;
	ShownTerminalRevision = 0;
}

bool UElysiumPlayerUISubsystem::OnTerminalCommand(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial, const FString& Command)
{
	UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (!Presentation)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI command failed: presentation subsystem is unavailable "
				"(owner %s serial %u command '%s')"),
			*Owner.ToString(), SessionSerial, *Command);
		return false;
	}
	if (!Presentation->SubmitTerminalCommand(Owner, SessionSerial, Command))
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI command was rejected (owner %s serial %u command '%s')"),
			*Owner.ToString(), SessionSerial, *Command);
		return false;
	}
	if (Command.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		// The substrate closes synchronously. Release UI-only input immediately; publication remains
		// the repair path for scripted closure and replacement.
		//
		// `quit` is one string with two authority behaviours (§9): at the directory prompt it is
		// builtin 33 and releases the player, at a password prompt it is `FUN_10217f50`'s cancel and
		// the session stays live. Asking the world whether the session survived is the only correct
		// test, and the next publish does exactly that — so a cancel re-opens the screen on the very
		// next frame rather than tearing it down here.
		if (!Presentation->IsTerminalSessionOpen())
		{
			HideTerminal();
		}
	}
	return true;
}

bool UElysiumPlayerUISubsystem::OnTerminalCharacter(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial, TCHAR Character)
{
	UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (!Presentation)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI character failed: presentation subsystem is unavailable "
				"(owner %s serial %u)"), *Owner.ToString(), SessionSerial);
		return false;
	}
	return Presentation->SubmitCharacter(Owner, SessionSerial, Character);
}

bool UElysiumPlayerUISubsystem::OnTerminalAcknowledge(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial)
{
	UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (!Presentation)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI acknowledge failed: presentation subsystem is unavailable "
				"(owner %s serial %u)"), *Owner.ToString(), SessionSerial);
		return false;
	}
	return Presentation->Acknowledge(Owner, SessionSerial);
}

bool UElysiumPlayerUISubsystem::OnTerminalQuit(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial)
{
	// The literal `hackcmd quit`, and the same close-on-release rule as a typed one.
	return OnTerminalCommand(Owner, SessionSerial, TEXT("quit"));
}

bool UElysiumPlayerUISubsystem::OnTerminalBreak(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial)
{
	UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get();
	if (!Presentation)
	{
		UE_LOG(LogElysiumPlayerUI, Warning,
			TEXT("terminal UI break failed: presentation subsystem is unavailable "
				"(owner %s serial %u)"), *Owner.ToString(), SessionSerial);
		return false;
	}
	return Presentation->Break(Owner, SessionSerial);
}

void UElysiumPlayerUISubsystem::OnTerminalDraft(
	const FElysiumEntityHandle& Owner, const FString& Draft)
{
	if (UElysiumPresentationSubsystem* Presentation = BoundPresentation.Get())
	{
		Presentation->SetTerminalDraft(Owner, Draft);
	}
}
