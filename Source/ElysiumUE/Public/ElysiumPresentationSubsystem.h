#pragma once

#include "CoreMinimal.h"
#include "ElysiumViewState.h"
#include "ElysiumWorldServices.h"
#include "Subsystems/WorldSubsystem.h"

#include "ElysiumPresentationSubsystem.generated.h"

class AElysiumMapActor;
class UElysiumPresentationSubsystem;

// Step 9 of the frame: rebuild the view state after everything that could change it has run
// (`AElysiumMapActor` has two tick functions for the same reason). Declared as a real tick function
// rather than left to a tickable subsystem so the position is in the engine's tick graph and
// `dumpticks` reads it back, the way S2 requires of every ordered pass.
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
DECLARE_MULTICAST_DELEGATE(FOnElysiumViewEvent);

// The presentation seam (roadmap 11.8, runtime-architecture.md section 11).
//
// **One publisher, one struct, one set of events (S8).** Everything the player sees that comes out
// of the running game is assembled here, once per frame, into an FElysiumViewState; AElysiumHUD,
// the CommonUI screens and the dialogue box read that and nothing else. Before this, each surface
// polled FElysiumEntityWorld from its own draw path and gated itself on IsMenuUp(), so every new
// screen added another poll, another gate, and another thing that could not be tested without a
// world.
//
// World-scoped, because what it publishes is a world's state and it must die with the map epoch the
// `Sign` and `Dialogue` pointers point into. The app state it reports is the GI-scoped flow
// subsystem's, read each frame rather than mirrored.
//
// It is also the production **IElysiumPresenter** (11.2's fourth service, null until now): the
// substrate announces the discrete moments — a fade started, a panel opened, a conversation opened
// or closed — and those announcements are what the discrete delegates below carry. They are
// recorded when they arrive and broadcast from the publish pass, so a listener always sees a View()
// that already agrees with the event. Continuous state (the fade's current alpha, the panel's
// fade-in ramp, the aimed use icon, the meters) is sampled off the world in the same pass, because
// it is derived from the game clock and has no moment to announce.
//
// Player *input* goes the other way, through the command bus and the two routing calls at the
// bottom of this class — never by a widget reaching into the substrate.
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

	// --- The events ---------------------------------------------------------------------------
	// Broadcast from the publish pass in this order: app state, fade, sign, dialogue, vitals, then
	// OnViewPublished last — so the general per-frame reconcile runs after every specific reaction.
	FOnElysiumViewEvent&      OnAppStateChanged() { return AppStateChangedEvent; }
	FOnElysiumFadeStarted&    OnFadeStarted()     { return FadeStartedEvent; }
	FOnElysiumSignOpened&     OnSignOpened()      { return SignOpenedEvent; }
	FOnElysiumViewEvent&      OnSignClosed()      { return SignClosedEvent; }
	FOnElysiumDialogueEvent&  OnDialogueOpened()  { return DialogueOpenedEvent; }
	FOnElysiumDialogueEvent&  OnDialogueTurn()    { return DialogueTurnEvent; }
	FOnElysiumViewEvent&      OnDialogueClosed()  { return DialogueClosedEvent; }
	FOnElysiumVitalsChanged&  OnVitalsChanged()   { return VitalsChangedEvent; }
	FOnElysiumViewPublished&  OnViewPublished()   { return ViewPublishedEvent; }

	// --- IElysiumPresenter --------------------------------------------------------------------
	// The substrate's announcements. Each records the moment; the publish pass drains it.
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) override;
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) override;
	virtual void CloseSign() override;
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) override;
	virtual void CloseDialog() override;

	// --- The return path ----------------------------------------------------------------------
	// The player's pick on the open conversation. The box reports an index; this resolves the world
	// and hands it to the same PlayerDialogChoose/PlayerDialogAdvance chokepoint `elysium.dlg.choose`
	// uses, so the UI never holds an FElysiumEntityWorld to talk back through.
	void DialogueChoose(int32 VisibleIndex);
	void DialogueAdvance();
	// True means the request closed the panel without synchronously opening a replacement, so the
	// local-player owner may remove its modal immediately instead of waiting for the next publish.
	bool DismissSign();

	// Step 9's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPublishTickFunction PublishTickFunction;

private:
	// The map actor belonging to *this* world, or null (a world with no map built, or a stale
	// current-map pointer mid-travel).
	AElysiumMapActor* ResolveMapActor() const;

	FElysiumViewState ViewState;

	// Drained by the publish pass. A second announcement of the same kind in one frame overwrites
	// the first, which is the same replace-the-running-one rule the world itself applies.
	bool bPendingFade = false;
	FLinearColor PendingFadeColor = FLinearColor::Black;
	float PendingFadeDuration = 0.0f;
	bool bPendingSignOpened = false;
	bool bPendingSignClosed = false;
	bool bPendingDialogueOpened = false;
	bool bPendingDialogueClosed = false;

	FOnElysiumViewEvent      AppStateChangedEvent;
	FOnElysiumFadeStarted    FadeStartedEvent;
	FOnElysiumSignOpened     SignOpenedEvent;
	FOnElysiumViewEvent      SignClosedEvent;
	FOnElysiumDialogueEvent  DialogueOpenedEvent;
	FOnElysiumDialogueEvent  DialogueTurnEvent;
	FOnElysiumViewEvent      DialogueClosedEvent;
	FOnElysiumVitalsChanged  VitalsChangedEvent;
	FOnElysiumViewPublished  ViewPublishedEvent;

	TArray<IConsoleObject*> ConsoleObjects;
};
