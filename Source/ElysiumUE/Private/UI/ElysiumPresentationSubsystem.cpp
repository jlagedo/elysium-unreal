#include "ElysiumPresentationSubsystem.h"

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumSignData.h"
#include "UI/ElysiumUISubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumView, Log, All);

namespace
{
	constexpr int32 MaxPendingNotifications = 64;
}

// --- The tick function ------------------------------------------------------------------------

void FElysiumPublishTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValid(Target))
	{
		Target->Publish();
	}
}

FString FElysiumPublishTickFunction::DiagnosticMessage()
{
	return TEXT("UElysiumPresentationSubsystem::Publish");
}

FName FElysiumPublishTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("ElysiumPublish"));
}

// --- Lifetime ---------------------------------------------------------------------------------

UElysiumPresentationSubsystem::UElysiumPresentationSubsystem()
{
	// Declared on the class, not set up at Initialize, so the frame position is readable off the
	// class defaults the way the map actor's two passes are (`Elysium.Substrate.FrameOrder`).
	//
	// S2 — the presentation side of the frame keeps running while the world is held. That is not a
	// nicety: pause is exactly when a conversation box has to be taken down, and a held world
	// publishes no new state of its own but still has to publish the *suppression*.
	PublishTickFunction.bCanEverTick = true;
	PublishTickFunction.bStartWithTickEnabled = true;
	PublishTickFunction.TickGroup = TG_PostUpdateWork;
	PublishTickFunction.bTickEvenWhenPaused = true;
}

UElysiumPresentationSubsystem* UElysiumPresentationSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UElysiumPresentationSubsystem>() : nullptr;
}

bool UElysiumPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only: an editor preview world has no session, no HUD and no map actor,
	// and publishing into one would only give the tick graph something to walk.
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void UElysiumPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.viewstate"),
		TEXT("elysium.viewstate — dump the published view state (app, surface, cinematic, reticle, fade, sign, dialogue, vitals)"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			const FElysiumViewState& V = ViewState;
			UE_LOG(LogElysiumView, Display, TEXT("app=%s surface=%d cinematic=%d use=%d alpha=%.2f actionable=%d fade=(%.2f,%.2f,%.2f,%.2f)"),
				ElysiumAppState::Name(V.App), V.bPlayerSurface ? 1 : 0, V.bCinematic ? 1 : 0,
				V.Interaction.Icon, V.Interaction.PromptAlpha, V.Interaction.bActionable ? 1 : 0,
				V.Fade.R, V.Fade.G, V.Fade.B, V.Fade.A);
			UE_LOG(LogElysiumView, Display, TEXT("sign=%s alpha=%.2f hideHUD=%d"),
				V.Sign ? *V.Sign->SourceFile : TEXT("<none>"), V.SignAlpha, V.bSignHidesHUD ? 1 : 0);
			UE_LOG(LogElysiumView, Display, TEXT("dialogue=%s rev=%u speaker='%s' choices=%d terminal=%d automatic=%d"),
				V.Dialogue.IsOpen() ? TEXT("open") : TEXT("<none>"), V.Dialogue.Revision,
				*V.Dialogue.Speaker, V.Dialogue.Choices.Num(), V.Dialogue.bTerminal ? 1 : 0,
				V.Dialogue.bAwaitingAutomatic ? 1 : 0);
			UE_LOG(LogElysiumView, Display, TEXT("vitals valid=%d health=%d/%d blood=%d/%d humanity=%d masq=%d"),
				V.Vitals.bValid ? 1 : 0, V.Vitals.Health, V.Vitals.MaxHealth,
				V.Vitals.BloodPool, V.Vitals.MaxBloodPool, V.Vitals.Humanity, V.Vitals.Masquerade);
		}),
		ECVF_Default));
}

void UElysiumPresentationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// A tick function needs a level to be registered against; the persistent level is the one that
	// lives as long as this subsystem does.
	if (PublishTickFunction.bCanEverTick && !PublishTickFunction.IsTickFunctionRegistered())
	{
		PublishTickFunction.Target = this;
		PublishTickFunction.SetTickFunctionEnable(PublishTickFunction.bStartWithTickEnabled);
		PublishTickFunction.RegisterTickFunction(InWorld.PersistentLevel);
	}
}

void UElysiumPresentationSubsystem::Deinitialize()
{
	if (PublishTickFunction.IsTickFunctionRegistered())
	{
		PublishTickFunction.UnRegisterTickFunction();
	}
	PublishTickFunction.Target = nullptr;

	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Reset();

	// The pointer fields die with the map epoch; nothing may read them after this.
	ViewState = FElysiumViewState();
	PendingNotifications.Reset();

	Super::Deinitialize();
}

AElysiumMapActor* UElysiumPresentationSubsystem::ResolveMapActor() const
{
	const UWorld* W = GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	// The map subsystem is GI-scoped and its current-map pointer can name the outgoing world's actor
	// for as long as a travel is in flight. This publisher is this world's, so it only ever reports
	// this world's map.
	return (Map && Map->GetWorld() == W) ? Map : nullptr;
}

// --- IElysiumPresenter — the substrate's announcements -----------------------------------------

void UElysiumPresentationSubsystem::StartFade(const FLinearColor& Color, float Duration, float HoldTime,
	float MaxAlpha, bool bFadeIn, bool bAutoReverse)
{
	bPendingFade = true;
	PendingFadeColor = Color;
	PendingFadeDuration = Duration;
}

void UElysiumPresentationSubsystem::OpenSign(const FElysiumEntityHandle& Owner,
	const TSharedPtr<const FElysiumSignData>& Data, float FadeInSeconds)
{
	bPendingSignOpened = true;
	bPendingSignClosed = false;
}

void UElysiumPresentationSubsystem::CloseSign()
{
	bPendingSignClosed = true;
	bPendingSignOpened = false;
}

void UElysiumPresentationSubsystem::OpenDialog(const FElysiumEntityHandle& Owner,
	FElysiumDlgConversation& Conversation)
{
	bPendingDialogueOpened = true;
	bPendingDialogueClosed = false;
}

void UElysiumPresentationSubsystem::CloseDialog()
{
	bPendingDialogueClosed = true;
	bPendingDialogueOpened = false;
}

void UElysiumPresentationSubsystem::PostNotification(const FElysiumNotification& Notification)
{
	FElysiumNotification Stored = Notification;
	Stored.Subject = Stored.Subject.TrimStartAndEnd();
	Stored.Quantity = FMath::Max(1, Stored.Quantity);
	if (Stored.Subject.IsEmpty())
	{
		UE_LOG(LogElysiumView, Warning, TEXT("notification refused: empty subject for kind %s"),
			ElysiumNotificationKindName(Stored.Kind));
		return;
	}
	if (PendingNotifications.Num() >= MaxPendingNotifications)
	{
		UE_LOG(LogElysiumView, Warning,
			TEXT("notification queue full (%d): dropping newest %s '%s'"),
			MaxPendingNotifications, ElysiumNotificationKindName(Stored.Kind), *Stored.Subject);
		return;
	}
	PendingNotifications.Add(MoveTemp(Stored));
}

// --- The publish pass --------------------------------------------------------------------------

void UElysiumPresentationSubsystem::DialogueChoose(int32 VisibleIndex)
{
	const AElysiumMapActor* Map = ResolveMapActor();
	if (FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr)
	{
		World->PlayerDialogChoose(VisibleIndex);
	}
}

void UElysiumPresentationSubsystem::DialogueAdvance()
{
	const AElysiumMapActor* Map = ResolveMapActor();
	if (FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr)
	{
		World->PlayerDialogAdvance();
	}
}

bool UElysiumPresentationSubsystem::LootTake(int32 Slot)
{
	const AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	return World && World->PlayerLootTake(Slot);
}

bool UElysiumPresentationSubsystem::LootGive(int32 Slot)
{
	const AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	return World && World->PlayerLootGive(Slot);
}

bool UElysiumPresentationSubsystem::CloseLoot()
{
	const AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	return World && World->PlayerCloseLoot();
}

bool UElysiumPresentationSubsystem::SubmitTerminalCommand(
	const FElysiumEntityHandle& Owner, uint32 SessionSerial, const FString& Command)
{
	const AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	return World && World->SubmitTerminalCommand(Owner, SessionSerial, Command);
}

UPrimitiveComponent* UElysiumPresentationSubsystem::ResolveTerminalDisplayTarget(
	const FElysiumEntityHandle& Owner) const
{
	const AElysiumMapActor* Map = ResolveMapActor();
	return Map ? Map->FindUseVisual(Owner) : nullptr;
}

bool UElysiumPresentationSubsystem::DismissSign()
{
	const AElysiumMapActor* Map = ResolveMapActor();
	if (FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr)
	{
		if (World->PlayerDismissSign())
		{
			// OnUseEnd may synchronously open the next tutorial sign. Keep the current modal in that
			// case until Publish reconciles its replacement content, avoiding an input-capture gap.
			return World->GetOpenSignData() == nullptr;
		}
	}
	return false;
}

void UElysiumPresentationSubsystem::Publish()
{
	const FElysiumViewState Previous = ViewState;
	FElysiumViewState Next;

	const UWorld* W = GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UElysiumGameFlowSubsystem* Flow = GI ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
	const UElysiumUISubsystem* UI = GI ? GI->GetSubsystem<UElysiumUISubsystem>() : nullptr;
	const bool bModalScreen = UI && UI->IsModalScreenOpen();

	Next.App = Flow ? Flow->AppState() : EElysiumAppState::Boot;
	Next.bPlayerSurface = ElysiumView::ShowsPlayerSurface(Next.App, UI && UI->IsMenuOpen());

	// The one gate. Everything below fills a surface; not filling it is what "stand the HUD down"
	// means now, and it is why a conversation on screen when the pause menu opens comes down
	// instead of drawing through — the box reconciles against a state that says it is not open.
	const AElysiumMapActor* Map = Next.bPlayerSurface ? ResolveMapActor() : nullptr;
	const FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (World)
	{
		// Authored cutscenes take the view through camera_track or SetCamera and return it through
		// RestoreCameraToPlayerControl / RemoveCamera. Camera ownership is the exact suppression
		// lifetime; scene playback alone would also catch ambient NPC choreography.
		Next.bCinematic = World->HasTrackCamera() || World->HasScriptedCamera()
			|| World->DialogueCameraHidesHud();
		Next.Interaction = World->GetInteractionView();

		FLinearColor FadeColor;
		if (World->GetScreenFade(FadeColor))
		{
			Next.Fade = FadeColor;
		}

		if (const FElysiumSignData* Sign = World->GetOpenSignData())
		{
			double OpenTime = 0.0;
			Next.SignOwner = World->GetOpenSign(&OpenTime);
			Next.Sign = Sign;
			Next.bSignHidesHUD = Sign->bHideHUD;
			Next.bSignDismissible = World->CanPlayerDismissSign();

			// fade_in ramps the whole panel up off the game clock, like every other timed entity
			// state. Resolved here so nothing downstream needs the clock to draw a panel.
			const float FadeIn = World->GetOpenSignFadeIn();
			Next.SignAlpha = (FadeIn > KINDA_SMALL_NUMBER)
				? FMath::Clamp(float(World->NowSeconds() - OpenTime) / FadeIn, 0.0f, 1.0f)
				: 1.0f;
		}

		if (const FElysiumDlgConversation* Conv = World->GetOpenDialog())
		{
			FElysiumDialogueView& D = Next.Dialogue;
			D.Conversation = Conv;
			D.Revision = Conv->Revision();
			D.Owner = World->GetOpenDialogOwner();
			if (const FElysiumEntity* OwnerEnt = World->Resolve(D.Owner))
			{
				D.Speaker = OwnerEnt->Def ? OwnerEnt->Def->TargetName : FString();
			}

			const bool bMale = Conv->PlayerMale();
			const bool bMalk = Conv->PlayerMalkavian();
			if (const FElysiumDlgLine* NpcLine = Conv->CurrentNpcLine())
			{
				D.Line = NpcLine->DisplayText(bMale, bMalk);
			}
			for (int32 v = 0; v < Conv->VisibleChoices().Num(); ++v)
			{
				if (const FElysiumDlgLine* Choice = Conv->VisibleChoice(v))
				{
					D.Choices.Add(Choice->DisplayText(bMale, bMalk));
					D.ChoiceIds.Add(Choice->Id);
				}
			}
			D.bAwaitingAutomatic = Conv->IsAwaitingAutomatic();
			D.bTerminal = Conv->IsTerminalLine() || World->CanPlayerAdvanceAutomatic();
		}

		World->BuildLootView(Next.Loot);
		World->BuildTerminalView(Next.Terminal);

		const FElysiumPlayer* PlayerEnt = World->FindPlayer();
		if (PlayerEnt)
		{
			FElysiumEntityHandle FeedTarget;
			bool bPaired = false;
			if (PlayerEnt->IsFeedPaired() && !PlayerEnt->FeedState.bVictim)
			{
				// Peer exists from accepted engage through the release tail. Target is intentionally
				// absent before 4007 and cleared at 4006, so it cannot own the visible lifetime.
				FeedTarget = PlayerEnt->FeedState.Peer;
				bPaired = true;
			}
			else if (Previous.Feed.bVisible
				&& World->GetFocusedUsable() == Previous.Feed.Target)
			{
				// Retail keeps the partially drained bar while the released victim remains focused.
				// Focus loss, invalidation or travel clears it without a second gameplay owner.
				FeedTarget = Previous.Feed.Target;
			}

			const FElysiumEntity* TargetEnt = World->Resolve(FeedTarget);
			const FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
			if (Victim && !Victim->IsInert())
			{
				FElysiumFeedView& Feed = Next.Feed;
				Feed.bVisible = true;
				Feed.bPaired = bPaired;
				Feed.Target = FeedTarget;
				Feed.BloodPool = FMath::Max(0, Victim->BloodPoolValue());
				const int32 AuthoredMax = Victim->Sheet.GetCurrent(
					EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPoolMax);
				// Presentation needs a positive denominator even in a content-free fixture. Never write
				// the clamp back: authored sheet state remains the sole gameplay value. Retain the first
				// observed capacity when an NPC has no authored maximum, or the denominator would shrink
				// with every pulse and the visible bar would incorrectly remain full.
				const int32 ObservedMax = Previous.Feed.Target == FeedTarget
					? Previous.Feed.MaxBloodPool : 0;
				Feed.MaxBloodPool = FMath::Max(1,
					FMath::Max(Feed.BloodPool, FMath::Max(AuthoredMax, ObservedMax)));
				Feed.Phase = static_cast<uint8>(PlayerEnt->FeedState.Phase);
			}
		}

		if (Next.bCinematic || Next.bSignHidesHUD || bModalScreen || Next.Dialogue.IsOpen()
			|| Next.Loot.IsOpen() || Next.Feed.bPaired)
		{
			Next.Interaction = FElysiumInteractionView();
		}

		// The meters come off the player entity's own fields (11.4), live — the session record is
		// the durable copy, and reading it here would report the value the entity is about to
		// overwrite. No player entity (a backdrop, a headless logic world) leaves bValid false.
		if (PlayerEnt)
		{
			FElysiumVitals& Vit = Next.Vitals;
			Vit.bValid = true;
			Vit.Health = PlayerEnt->Health;
			Vit.MaxHealth = PlayerEnt->MaxHealth;
			const FElysiumSheet& Sheet = PlayerEnt->Sheet;
			Vit.BloodPool  = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool);
			Vit.MaxBloodPool = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPoolMax);
			Vit.Humanity   = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity);
			Vit.Masquerade = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade);
		}
	}

	ViewState = MoveTemp(Next);

	// --- the discrete events ------------------------------------------------------------------
	// Broadcast after the state is in place, so a listener that reads View() sees what the event is
	// telling it about. An announcement that arrived while the surface is suppressed is dropped
	// rather than queued: the state itself carries the sign or the conversation back when the
	// screen over it closes.
	const bool bSuppressed = !ViewState.bPlayerSurface;

	if (ViewState.App != Previous.App)
	{
		AppStateChangedEvent.Broadcast();
	}
	if (bPendingFade && !bSuppressed)
	{
		FadeStartedEvent.Broadcast(PendingFadeColor, PendingFadeDuration);
	}
	if (bPendingSignOpened && !bSuppressed && ViewState.Sign)
	{
		SignOpenedEvent.Broadcast(*ViewState.Sign);
	}
	if (bPendingSignClosed && !bSuppressed)
	{
		SignClosedEvent.Broadcast();
	}
	if (!bSuppressed)
	{
		if (bPendingDialogueOpened && ViewState.Dialogue.IsOpen())
		{
			DialogueOpenedEvent.Broadcast(ViewState.Dialogue);
		}
		else if (ViewState.Dialogue.IsOpen() && Previous.Dialogue.IsOpen()
			&& ViewState.Dialogue.Conversation == Previous.Dialogue.Conversation
			&& ViewState.Dialogue.Revision != Previous.Dialogue.Revision)
		{
			// A turn advanced. There is no announcement for this — the branch machine moves inside
			// the conversation — so it is the one dialogue event derived from the state.
			DialogueTurnEvent.Broadcast(ViewState.Dialogue);
		}
		if (bPendingDialogueClosed)
		{
			DialogueClosedEvent.Broadcast();
		}
	}
	if (ViewState.Vitals != Previous.Vitals)
	{
		VitalsChangedEvent.Broadcast(ViewState.Vitals);
	}
	// Notifications are the one announcement family retained while the player surface is
	// suppressed or before a local-player listener binds. A grant during New Game loading is still
	// new information when play begins; it has no retained world state that could reconstruct it.
	if (!bSuppressed && NotificationEvent.IsBound() && !PendingNotifications.IsEmpty())
	{
		TArray<FElysiumNotification> Delivering = MoveTemp(PendingNotifications);
		PendingNotifications.Reset();
		for (const FElysiumNotification& Notification : Delivering)
		{
			NotificationEvent.Broadcast(Notification);
		}
	}

	bPendingFade = false;
	bPendingSignOpened = false;
	bPendingSignClosed = false;
	bPendingDialogueOpened = false;
	bPendingDialogueClosed = false;

	ViewPublishedEvent.Broadcast(ViewState);
}
