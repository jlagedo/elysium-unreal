#pragma once

#include "CoreMinimal.h"
#include "ElysiumViewState.h"
#include "ElysiumWorldServices.h"
#include "Subsystems/WorldSubsystem.h"

#include "ElysiumPresentationSubsystem.generated.h"

class AElysiumMapActor;
class AElysiumPlayerCameraManager;
class UElysiumPresentationSubsystem;
class UElysiumTerminalProjection;
class UPrimitiveComponent;

// Rebuild the view state after everything that could change it has run (`AElysiumMapActor` has two
// tick functions for the same reason). Declared as a real tick function rather than left to a
// tickable subsystem so the position is in the engine's tick graph and `dumpticks` reads it back.
USTRUCT()
struct FElysiumPublishTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	UElysiumPresentationSubsystem* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumPublishTickFunction> : public TStructOpsTypeTraitsBase2<FElysiumPublishTickFunction>
{
	enum { WithCopy = false };
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumViewPublished, const FElysiumViewState& /*View*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumDialogueEvent, const FElysiumDialogueView& /*Dialogue*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumSignOpened, const FElysiumSignData& /*Sign*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumFadeStarted, const FLinearColor& /*Color*/, float /*Duration*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumVitalsChanged, const FElysiumVitals& /*Vitals*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumNotification, const FElysiumNotification& /*Notification*/);
DECLARE_MULTICAST_DELEGATE(FOnElysiumViewEvent);

// The presentation seam.
//
// One publisher, one struct, one set of events. Everything the player sees that comes out of the
// running game is assembled here, once per frame, into an FElysiumViewState; the CommonUI screens
// and the HUD model read that and nothing else.
//
// World-scoped, because what it publishes is a world's state and it must die with the map epoch the
// `Sign` and `Dialogue` pointers point into. The app state it reports is the GI-scoped flow
// subsystem's, read each frame rather than mirrored.
//
// Production IElysiumPresenter: the substrate announces the discrete moments — a fade started, a
// panel opened, a conversation opened or closed — and those announcements are what the discrete
// delegates below carry. They are recorded when they arrive and broadcast from the publish pass, so
// a listener always sees a View() that already agrees with the event. Continuous state (the fade's
// current alpha, the panel's fade-in ramp, the aimed use icon, the meters) is sampled off the world
// in the same pass, because it is derived from the game clock and has no moment to announce.
//
// Player input goes the other way, through the command bus and the routing calls at the bottom of
// this class — never by a widget reaching into the substrate.
UCLASS()
class UElysiumPresentationSubsystem : public UWorldSubsystem, public IElysiumPresenter
{
	GENERATED_BODY()

public:
	UElysiumPresentationSubsystem();

	static UElysiumPresentationSubsystem* Get(const UWorld* World);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// This frame's state. Valid for the whole frame; its pointer fields are valid until the next
	// publish and must never be stored.
	const FElysiumViewState& View() const { return ViewState; }

	// Rebuild and broadcast. Called from the tick function; public so a test can drive one pass.
	void Publish();

	// Broadcast from the publish pass in this order: app state, fade, sign, dialogue, vitals,
	// notifications, then OnViewPublished last — so the general per-frame reconcile runs after every
	// specific reaction.
	FOnElysiumViewEvent&      OnAppStateChanged() { return AppStateChangedEvent; }
	FOnElysiumFadeStarted&    OnFadeStarted()     { return FadeStartedEvent; }
	FOnElysiumSignOpened&     OnSignOpened()      { return SignOpenedEvent; }
	FOnElysiumViewEvent&      OnSignClosed()      { return SignClosedEvent; }
	FOnElysiumDialogueEvent&  OnDialogueOpened()  { return DialogueOpenedEvent; }
	FOnElysiumDialogueEvent&  OnDialogueTurn()    { return DialogueTurnEvent; }
	FOnElysiumViewEvent&      OnDialogueClosed()  { return DialogueClosedEvent; }
	FOnElysiumVitalsChanged&  OnVitalsChanged()   { return VitalsChangedEvent; }
	FOnElysiumNotification&   OnNotification()    { return NotificationEvent; }
	FOnElysiumViewPublished&  OnViewPublished()   { return ViewPublishedEvent; }

	// The substrate's announcements. Each records the moment; the publish pass drains it.
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) override;
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) override;
	virtual void CloseSign() override;
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) override;
	virtual void CloseDialog() override;
	virtual void PostNotification(const FElysiumNotification& Notification) override;

	// Diagnostic visibility for focused tests and `elysium.viewstate`; no consumer mutates the FIFO.
	int32 NumPendingNotifications() const { return PendingNotifications.Num(); }
	const TArray<FElysiumNotification>& PendingNotificationQueue() const
	{
		return PendingNotifications;
	}

	// The player's pick on the open conversation. The box reports the row's position AND the `.dlg`
	// line id it was showing; this resolves the world and hands both to the same
	// PlayerDialogChoose/PlayerDialogAdvance chokepoint `elysium.dlg.choose` uses, so the UI never
	// holds an FElysiumEntityWorld to talk back through. The id is what lets the world refuse a
	// stale pick: a position alone would name whatever sentence now sits there.
	void DialogueChoose(int32 VisibleIndex, int32 ExpectedLineId = INDEX_NONE);
	void DialogueAdvance();
	// M-SKIP — the hurry verb, same chokepoint: `World->PlayerDialogSkip()`.
	void DialogueSkip();
	bool LootTake(int32 Slot);
	bool LootGive(int32 Slot);
	bool CloseLoot();
	bool SubmitTerminalCommand(const FElysiumEntityHandle& Owner, uint32 SessionSerial,
		const FString& Command);
	// --- the five terminal intents -------------------
	// Retail's client has exactly one channel to the terminal — the `hackcmd` ConCommand
	// (`FUN_100dace0`) — and its key handler picks the string. These name the five strings that
	// handler can send, so the widget never spells a command out and every one re-resolves the map,
	// the world, the active use session and the serial through the same `SubmitTerminalCommand`
	// chokepoint. `Quit` and `Break` are literal words on purpose: `quit` is builtin 33 at the
	// directory and `FUN_10217f50`'s cancel at a password prompt (one string, two authority
	// behaviours, as retail), and `break` acts only on the pending-password arm.
	bool SubmitCommand(const FElysiumEntityHandle& Owner, uint32 SessionSerial,
		const FString& Command);
	bool SubmitCharacter(const FElysiumEntityHandle& Owner, uint32 SessionSerial, TCHAR Character);
	bool Acknowledge(const FElysiumEntityHandle& Owner, uint32 SessionSerial);
	bool Quit(const FElysiumEntityHandle& Owner, uint32 SessionSerial);
	bool Break(const FElysiumEntityHandle& Owner, uint32 SessionSerial);

	// Whether the world still holds a live terminal session RIGHT NOW, not in the last published
	// frame. `quit` is one string with two authority behaviours (§9) — a release at the directory
	// prompt, a cancel at a password prompt — so the only way to know whether the screen may close
	// is to ask the world after the submit, before the next publish.
	bool IsTerminalSessionOpen() const;

	// The local line the focused widget is holding, mirrored onto the monitor's glass. Retail
	// composes the typed characters into the CLIENT's own cell buffer before rasterizing
	// (`FUN_100c6d50` -> `FUN_100c8060`, §8.1/TERM13), so the typed line is genuinely on the screen
	// and not in a viewport overlay. An unset owner (or an empty string) means nothing is typed.
	void SetTerminalDraft(const FElysiumEntityHandle& Owner, const FString& Draft);
	const FString& TerminalDraft() const { return TerminalDraftText; }
	const FElysiumEntityHandle& TerminalDraftOwner() const { return TerminalDraftHandle; }
	// --- world-lifetime terminal glass ---------------
	// A monitor's screen is not session state: retail's screensaver think writes into the entity's
	// own cell buffer from map load onward, so the render target lives with the BODY. The map actor
	// registers one here when a terminal's use anchor is stood, re-registers it when that body is
	// replaced, and releases every one when the anchors are cleared with the map epoch.
	void RegisterTerminalProjection(const FElysiumEntityHandle& Owner, UPrimitiveComponent* Body);
	void ReleaseTerminalProjection(const FElysiumEntityHandle& Owner);
	void ReleaseAllTerminalProjections();
	UElysiumTerminalProjection* FindTerminalProjection(const FElysiumEntityHandle& Owner) const;
	int32 NumTerminalProjections() const { return TerminalProjections.Num(); }
	// True means the request closed the panel without synchronously opening a replacement, so the
	// local-player owner may remove its modal immediately instead of waiting for the next publish.
	bool DismissSign();

	// Redraw every projection whose authority revision moved and whose body is actually on screen.
	// Public because it is the only observable half of the glass under `-nullrhi`: nothing is ever
	// rendered there, so `Elysium.Substrate.Terminal.ProjectionRedraw` drives this directly with a
	// forced residency answer rather than through a frame that cannot happen.
	void RedrawTerminalProjections(const FElysiumViewState& State);

	// Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPublishTickFunction PublishTickFunction;

private:
	// The map actor belonging to *this* world, or null (a world with no map built, or a stale
	// current-map pointer mid-travel).
	AElysiumMapActor* ResolveMapActor() const;

	// This world's player camera manager, or null where no player rig runs (character generation, a
	// backdrop). The frame's draw policy is read off its stamped sample rather than re-derived here.
	const AElysiumPlayerCameraManager* ResolveLocalCameraManager() const;

	FElysiumViewState ViewState;

	// An array rather than a handle-keyed map: `FElysiumEntityHandle` is a plain substrate value and
	// not a `USTRUCT`, so it cannot key a reflected container — and reflection is what keeps these
	// render targets off the GC's list of things to collect. A map carries at most a handful of
	// terminals, so the scan is the cheaper half of the trade.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UElysiumTerminalProjection>> TerminalProjections;

	// The mirrored draft and the terminal it belongs to. Plain values: the handle is a substrate
	// type and the string is presentation-local, neither is authority state, and both are dropped
	// when the session's screen goes away.
	FElysiumEntityHandle TerminalDraftHandle;
	FString TerminalDraftText;

	// Whether the stale-sample warning has already been issued for the run of frames currently in
	// progress. The manager can legitimately publish nothing for many consecutive frames, and the
	// interesting event is the transition into that state, not each frame of it.
	bool bReportedStaleCameraSample = false;

	// The weapon-selection peek. Presentation owns the whole of it: the publisher notices the active
	// weapon changing and raises the peek, then lets it fall on its own. Nothing in gameplay knows
	// the selector is on screen, and nothing persists it.
	// Advance the peek for this frame's equipment and return its alpha. Raises the peek whenever the
	// weapon in hand differs from the one the last frame described.
	float AdvanceWeaponPeek(const FElysiumEquipmentView& Equipment);

	FString LastActiveWeaponClass;
	bool bSeenActiveWeapon = false;
	float WeaponPeekSecondsLeft = 0.0f;
	double LastWeaponPeekRealSeconds = 0.0;

	// Drained by the publish pass. A second announcement of the same kind in one frame overwrites
	// the first, which is the same replace-the-running-one rule the world itself applies.
	bool bPendingFade = false;
	FLinearColor PendingFadeColor = FLinearColor::Black;
	float PendingFadeDuration = 0.0f;
	bool bPendingSignOpened = false;
	bool bPendingSignClosed = false;
	bool bPendingDialogueOpened = false;
	bool bPendingDialogueClosed = false;
	TArray<FElysiumNotification> PendingNotifications;

	FOnElysiumViewEvent      AppStateChangedEvent;
	FOnElysiumFadeStarted    FadeStartedEvent;
	FOnElysiumSignOpened     SignOpenedEvent;
	FOnElysiumViewEvent      SignClosedEvent;
	FOnElysiumDialogueEvent  DialogueOpenedEvent;
	FOnElysiumDialogueEvent  DialogueTurnEvent;
	FOnElysiumViewEvent      DialogueClosedEvent;
	FOnElysiumVitalsChanged  VitalsChangedEvent;
	FOnElysiumNotification   NotificationEvent;
	FOnElysiumViewPublished  ViewPublishedEvent;

	TArray<IConsoleObject*> ConsoleObjects;
};
