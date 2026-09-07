#include "ElysiumPresentationSubsystem.h"

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumSignData.h"
#include "UI/ElysiumDialogueWidget.h"
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

	// How long the weapon peek stays up after a switch, and how long it takes to fade once the hold
	// expires. Long enough to read the neighbouring entries while cycling, short enough that it is
	// gone before the next thing the player looks at.
	constexpr float WeaponPeekHoldSeconds = 1.5f;
	constexpr float WeaponPeekFadeSeconds = 0.35f;
}

// Tick function.

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

// Lifetime.

UElysiumPresentationSubsystem::UElysiumPresentationSubsystem()
{
	// Declared on the class, not set up at Initialize, so the frame position is readable off the
	// class defaults the way the map actor's two passes are (`Elysium.Substrate.FrameOrder`).
	//
	// The presentation side of the frame keeps running while the world is held. That is not a
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
			UE_LOG(LogElysiumView, Display, TEXT("app=%s surface=%d scriptedcam=%d use=%d alpha=%.2f actionable=%d fade=(%.2f,%.2f,%.2f,%.2f)"),
				ElysiumAppState::Name(V.App), V.bPlayerSurface ? 1 : 0,
				V.Camera.bScriptedCameraOwnsView ? 1 : 0,
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

const AElysiumPlayerCameraManager* UElysiumPresentationSubsystem::ResolveLocalCameraManager() const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	// Null during character generation and on a backdrop, where the view target is an `ACameraActor`
	// and no player rig runs. Both leave `bPlayerSurface` false, so no reader sees the gap.
	return PC ? Cast<AElysiumPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
}

// IElysiumPresenter — the substrate's announcements.

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

// The publish pass.

void UElysiumPresentationSubsystem::DialogueChoose(int32 VisibleIndex, int32 ExpectedLineId)
{
	const AElysiumMapActor* Map = ResolveMapActor();
	if (FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr)
	{
		World->PlayerDialogChoose(VisibleIndex, ExpectedLineId);
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

void UElysiumPresentationSubsystem::DialogueSkip()
{
	// M-SKIP — retail's hurry verb (`dialogpick -2` -> `0x102c0bb0`). The world ends the voice,
	// completes the face and flushes the NPC's parked col-5 exactly as `NPCNotifyDoneTalking`
	// would; the response band is untouched.
	const AElysiumMapActor* Map = ResolveMapActor();
	if (FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr)
	{
		World->PlayerDialogSkip();
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

// The weapon peek's whole lifetime. It is raised by the active weapon changing and by nothing else:
// no verb announces a switch, so a script grant, a scripted holster and the selector all show the
// same readout for the same reason.
float UElysiumPresentationSubsystem::AdvanceWeaponPeek(const FElysiumEquipmentView& Equipment)
{
	const UWorld* W = GetWorld();
	// Real time, not game time: the peek is a screen animation and keeps fading while the game is
	// paused underneath it.
	const double Now = W ? W->GetRealTimeSeconds() : 0.0;
	const float Delta = LastWeaponPeekRealSeconds > 0.0
		? static_cast<float>(FMath::Max(0.0, Now - LastWeaponPeekRealSeconds))
		: 0.0f;
	LastWeaponPeekRealSeconds = Now;

	// The peek follows the SELECTION, not only the hand: browsing a non-weapon category moves the
	// cursor without changing what is held, and that still deserves the readout.
	const FElysiumInventoryEntryView* Selected = Equipment.Selected();
	const FString ActiveClass = Selected ? Selected->Classname : FString();

	if (!bSeenActiveWeapon)
	{
		// The first frame with a player establishes what is in hand; it is not a switch, so arriving
		// in a map does not open the selector.
		bSeenActiveWeapon = true;
		LastActiveWeaponClass = ActiveClass;
	}
	else if (ActiveClass != LastActiveWeaponClass)
	{
		LastActiveWeaponClass = ActiveClass;
		// An empty hand is a switch worth showing too: holstering should confirm itself.
		WeaponPeekSecondsLeft = WeaponPeekHoldSeconds + WeaponPeekFadeSeconds;
	}
	else
	{
		WeaponPeekSecondsLeft = FMath::Max(0.0f, WeaponPeekSecondsLeft - Delta);
	}

	if (WeaponPeekSecondsLeft <= 0.0f)
	{
		return 0.0f;
	}
	// Full opacity through the hold, then a linear fall across the fade tail.
	return WeaponPeekSecondsLeft >= WeaponPeekFadeSeconds
		? 1.0f
		: WeaponPeekSecondsLeft / WeaponPeekFadeSeconds;
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
	// means, and it is why a conversation on screen when the pause menu opens comes down instead of
	// drawing through — the box reconciles against a state that says it is not open.
	const AElysiumMapActor* Map = Next.bPlayerSurface ? ResolveMapActor() : nullptr;
	const FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (World)
	{
		// Authored cutscenes take the view through camera_track or SetCamera and return it through
		// RestoreCameraToPlayerControl / RemoveCamera. Camera ownership is the exact lifetime; scene
		// playback alone would also catch ambient NPC choreography. This is **context, not a HUD
		// gate** — it suppresses the interaction prompt and toasts, nothing else.
		Next.Camera.bScriptedCameraOwnsView = World->HasTrackCamera() || World->HasScriptedCamera();
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
			// The identity the UI reconciles on. `Conv` is a heap address that a closed conversation
			// can hand straight back to its successor, and `Revision()` restarts at 1, so the pair
			// can repeat across two conversations in one frame; the world's open serial cannot.
			D.DialogSerial = World->GetOpenDialogSerial();
			D.Owner = World->GetOpenDialogOwner();
			if (const FElysiumEntity* OwnerEnt = World->Resolve(D.Owner))
			{
				D.Speaker = OwnerEnt->Def ? OwnerEnt->Def->TargetName : FString();
			}

			// The turn itself — subtitle, band, labels, the no-valid-reply substitution — is a rule
			// about the conversation and is projected by the UI layer's own pure function.
			ElysiumDialogueUI::FillTurn(*Conv, D);
			D.bTerminal = Conv->IsTerminalLine() || World->CanPlayerAdvanceAutomatic();
			// M-REVEAL / M-SKIP. The band is published with the line rather than withheld behind
			// `ShowPlayerChoices`; these two say whether the voice is still running, which is what
			// draws the skip hint and what routes Space to the hurry verb.
			D.bNpcSpeaking = World->IsDialogueNpcSpeaking();
			D.bCanSkip = World->CanPlayerSkipDialogue();
		}

		World->BuildLootView(Next.Loot);
		World->BuildTerminalView(Next.Terminal);

		const FElysiumPlayer* PlayerEnt = World->FindPlayer();
		if (PlayerEnt)
		{
			// The victim blood meter's SOURCE, in retail's own precedence
			// (`CBasePlayer::UpdateClientActionState` `vampire.dll` `0x101755d0`): the authoritative
			// feed target `m_hFeedTarget` (`+0x149c`) whenever its handle still resolves, otherwise
			// the grapple partner (`+0x1538`) while this player is the role-0 half (`+0x153c == 0`)
			// and the continuation latch (`+0x14a8`) is set. Nothing else on the feed side owns it —
			// not "is paired" and not "is focused".
			//
			// OPEN — retail has a THIRD arm at `0x10175bbb` for the case with no feed target and no
			// continuation grapple: it reads the active weapon's blood source (weapon vfunc `0x474`
			// -> `+0x9c`) and publishes that entity's slot 12 on the same panel, which is how a
			// carried blood pack reads its remaining doses. This runtime has no blood-source weapon,
			// so that arm has no producer and is left saying nothing.
			FElysiumEntityHandle Source = PlayerEnt->FeedState.Target;
			if (World->Resolve(Source) == nullptr
				&& !PlayerEnt->FeedState.bVictim && PlayerEnt->FeedState.bContinuation)
			{
				Source = PlayerEnt->FeedState.Peer;
			}

			ElysiumFeedBar::FUpdate Update;
			const FElysiumEntity* SourceEnt = World->Resolve(Source);
			// No `IsInert` test: retail reads slot 12 off whatever the handle resolves to and lets
			// the value gate decide. A victim killed mid-feed still has whatever it had left, and
			// one drained to nothing is hidden because it is at zero, not because it is dead.
			if (const FElysiumCombatCharacter* Victim =
				SourceEnt ? SourceEnt->AsCombatCharacter() : nullptr)
			{
				Update.bHasSource = true;
				Update.Source = Source;
				// The value, as the server sends it: the victim's own slot 12 `BloodPool`, negatives
				// masked to zero (`((int)v < 1) - 1 & v` at `0x10175c9c`).
				Update.BloodPool = FMath::Max(0, Victim->BloodPoolValue());
				// The DENOMINATOR, recovered: `CFeedBar::vfunc114` `0x100503d0` seeds it with the
				// literal `0xf` and then overrides it with the player's replicated
				// `m_iClientFeedMaxBloodPool` whenever that is non-zero. That field is written in
				// exactly one place — `CBaseCombatCharacter::EnterGrappleState` `0x10329760`, from
				// the VICTIM's char-template `Attributes[BloodPool]` (`template+0xd0` `+0x30`, the
				// same word `CAI_BaseNPCTroika` `0x1029a0b0` seeds the victim's own slot 12 from).
				// So the bar reads "how much of the pool this critter stood up with is left", not a
				// share of the 15-point stat cap and not `BloodPool_Max` — slot 13 has no reader in
				// the binary at all. A victim that authors no template pool falls back to its own
				// stat ceiling, which the shipped `stats.txt` makes the same 15 the client hardcodes.
				Update.AuthoredMax = Victim->TemplateBloodPool() > 0
					? Victim->TemplateBloodPool() : Victim->BloodPoolCapacity();
			}
			// `+0x14d4`/`+0x14d0`, the replicated feeding flag and pulse interval `vfunc98` reads.
			// The release family has the fangs off the neck and performs no pulse, so it does not
			// drive the anticipation ramp either.
			Update.bPaired = PlayerEnt->IsFeedPaired() && !PlayerEnt->FeedState.bVictim;
			Update.bPulsing = PlayerEnt->FeedState.IsTransacting()
				&& PlayerEnt->FeedState.Phase != EElysiumFeedPhase::Release;
			Update.PulseInterval = PlayerEnt->FeedState.Interval;
			Update.Phase = static_cast<uint8>(PlayerEnt->FeedState.Phase);

			Next.Feed = ElysiumFeedBar::Update(Previous.Feed, Update, World->NowSeconds());

			// The owner's live run has no other way to read these numbers. Gated on the three the
			// panel is built from — the anticipation ramp moves `Percent` every frame and would
			// otherwise flood the log.
			if (Next.Feed.bVisible != Previous.Feed.bVisible
				|| Next.Feed.BloodPool != Previous.Feed.BloodPool
				|| Next.Feed.MaxBloodPool != Previous.Feed.MaxBloodPool
				|| Next.Feed.Target != Previous.Feed.Target)
			{
				UE_LOG(LogElysiumFeed, Display,
					TEXT("Feed bar: visible=%d blood=%d max=%d percent=%.2f paired=%d source=%s"),
					Next.Feed.bVisible ? 1 : 0, Next.Feed.BloodPool, Next.Feed.MaxBloodPool,
					Next.Feed.Percent, Next.Feed.bPaired ? 1 : 0, *Next.Feed.Target.ToString());
			}
		}

		if (Next.Camera.bScriptedCameraOwnsView || Next.bSignHidesHUD || bModalScreen
			|| Next.Dialogue.IsOpen() || Next.Loot.IsOpen() || Next.Feed.bPaired)
		{
			Next.Interaction = FElysiumInteractionView();
		}

		// **The camera's draw policy, taken from the stamped sample rather than re-derived.**
		// `TG_PostUpdateWork` runs after the local player controller's tick, which is where the camera
		// manager produces the frame's view, so the sample is this frame's. A stale one is a real
		// failure: hold the previous projection and say so, rather than publishing a default that
		// would silently stand the body or the reticle in the wrong state.
		if (const AElysiumPlayerCameraManager* Manager = ResolveLocalCameraManager())
		{
			const FElysiumCameraSample& S = Manager->GetCameraSample();
			if (S.Frame == GFrameCounter)
			{
				bReportedStaleCameraSample = false;
				Next.Camera.bValid = true;
				Next.Camera.bThirdPerson = S.Draw.bThirdPerson;
				Next.Camera.bDrawViewmodel = S.Draw.bViewmodelEligible;
				Next.Camera.bDrawPlayerBody = S.Draw.bBodyEligible;
				Next.Camera.PlayerBodyAlpha = S.Draw.BodyAlpha;
				Next.Camera.bDrawWorldWeapon = S.Draw.bWorldWeaponEligible;
				Next.Camera.ReticlePath = S.Draw.Reticle;

				// Three named contributors and only three, each derived from a **named** `SetCamera`
				// shot. A Worldcraft `camera_track` authors no `ShowHud` key and so contributes to
				// none of them (`docs/vtmb/camera-view-modes.md` §5).
				Next.Camera.bShowHud = S.Draw.bShowHud && !World->DialogueCameraHidesHud();
			}
			else
			{
				Next.Camera = Previous.Camera;
				// Reported on the **edge**, not per frame. The manager legitimately publishes nothing
				// for a run of frames — the view target is not a player body, or its rig has not
				// solved yet — and a warning per frame for the length of that run buries the one
				// transition that carries the information. The latch clears the moment a fresh sample
				// arrives, so a second stall reports again.
				if (!bReportedStaleCameraSample)
				{
					bReportedStaleCameraSample = true;
					UE_LOG(LogElysiumView, Warning,
						TEXT("camera sample is stale (frame %llu, now %llu) - holding the previous draw ")
						TEXT("policy; the reticle and player body describe the last resolved frame ")
						TEXT("until a sample is published again"),
						S.Frame, GFrameCounter);
				}
			}
		}

		// The meters come off the player entity's own fields, live — the session record is the
		// durable copy, and reading it here would report the value the entity is about to overwrite.
		// No player entity (a backdrop, a headless logic world) leaves bValid false.
		if (PlayerEnt)
		{
			FElysiumVitals& Vit = Next.Vitals;
			Vit.bValid = true;
			Vit.Health = PlayerEnt->Health;
			Vit.MaxHealth = PlayerEnt->MaxHealth;
			const FElysiumSheet& Sheet = PlayerEnt->Sheet;
			Vit.BloodPool  = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool);
			// The droplet COUNT is the pool's ceiling, so it is the `BloodPool` stat definition's own
			// effective Max — the bound `IncBloodPool` refuses to pass — and the shipped `stats.txt`
			// makes that 15, i.e. three groups of five. Reading slot 13 `BloodPool_Max` here was the
			// defect: that stat is a critter's STARTING pool (its `Default` is 10), it never clamps
			// `BloodPool`, and dividing by it hid the third group.
			Vit.MaxBloodPool = PlayerEnt->BloodPoolCapacity();
			Vit.Humanity   = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity);
			Vit.Masquerade = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade);
		}

		// The stance half of the stealth readout, off the same locomotion sample the animation graph
		// is steered by. The concealment gauge and the observer stay invalid until stealth authority
		// commits them; presentation runs no perception of its own.
		Next.Stealth.bSneaking = Map->IsPlayerSneaking();

		// The hand, what is worn, and the browsed section. Built every frame from the inventory
		// itself, so an item picked up, dropped or reloaded is on the readout the frame after it
		// happened without gameplay announcing anything.
		ElysiumItems::BuildInventoryView(*World, Next.Equipment);
		Next.Equipment.PeekAlpha = AdvanceWeaponPeek(Next.Equipment);
	}
	else
	{
		// No entity world means no inventory to describe. The peek does not survive it: a selector
		// still fading when the map goes away would fade over the next one.
		bSeenActiveWeapon = false;
		WeaponPeekSecondsLeft = 0.0f;
		LastWeaponPeekRealSeconds = 0.0;
	}

	ViewState = MoveTemp(Next);

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
			&& ViewState.Dialogue.DialogSerial == Previous.Dialogue.DialogSerial
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
