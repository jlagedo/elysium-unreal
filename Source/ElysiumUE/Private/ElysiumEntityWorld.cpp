#include "ElysiumEntityWorld.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumScriptHost.h"
#include "ElysiumSignData.h"
#include "ElysiumUseIcons.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWorld, Log, All);

// A/B toggle for the P1.5 brush bodies (per-entity convex collision + trigger overlaps). Read at
// Load, so it takes effect on the next map load (like elysium.BrushCollision for the world hulls).
static TAutoConsoleVariable<int32> CVarBrushBodies(
	TEXT("elysium.BrushBodies"),
	1,
	TEXT("Build per-brush-entity collision/overlap bodies at map load (1, default) or skip them (0)."),
	ECVF_Default);

namespace
{
	// Every world instance takes a unique epoch (game thread only), so a handle minted by one
	// map load never falsely resolves against the next. Starts at 1 — Teardown sets a world's
	// epoch to 0, which no live handle carries.
	uint32 GElysiumNextWorldEpoch = 1;

	// The maximum deliveries the queue may drain in one service pass before the loop guard
	// bails — catches a zero-delay output ring feeding itself (retail data has cycles).
	constexpr int32 GElysiumMaxDrainPerFrame = 10000;

	const TCHAR* const GSelfTarget = TEXT("!self");
	const TCHAR* const GCallerTarget = TEXT("!caller");
	const TCHAR* const GActivatorTarget = TEXT("!activator");
}

FElysiumEntityWorld::FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState)
	: Owner(InOwner)
	, GameState(InGameState)
	, Epoch(GElysiumNextWorldEpoch++)
{
	// R5 — the chokepoints are never uninstrumented: the ring buffer (always-on history) and
	// the log/VLOG stream are installed before any entity spawns. Phase 2 UI adds more sinks.
	TUniquePtr<FElysiumRingBufferSink> RingSink = MakeUnique<FElysiumRingBufferSink>(*this, 1000);
	Ring = RingSink.Get();
	Sinks.Add(MoveTemp(RingSink));
	Sinks.Add(MakeUnique<FElysiumLogSink>(*this));
}

FElysiumEntityWorld::~FElysiumEntityWorld()
{
	Teardown();
}

double FElysiumEntityWorld::NowSeconds() const
{
	return GameState ? GameState->GameClock().GetNow() : 0.0;
}

UElysiumMapSubsystem* FElysiumEntityWorld::MapSubsystem() const
{
	// The map-lifecycle owner (Travel + the P4.6 landmark-transition queue). Reached through the
	// owning actor's GameInstance — the seam trigger_changelevel uses to request a deferred travel.
	const UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
}

UElysiumAudioSubsystem* FElysiumEntityWorld::AudioSubsystem() const
{
	const UWorld* W = Owner ? Owner->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UElysiumAudioSubsystem>() : nullptr;
}

// --- Load / spawn -----------------------------------------------------------------------

void FElysiumEntityWorld::Load(FElysiumEntityDefs&& InDefs)
{
	Defs = MoveTemp(InDefs);

	EntityList.Reserve(Defs.Num());
	NameIndex.Reserve(Defs.Num());
	ClassIndex.Reserve(Defs.Num());

	for (int32 i = 0; i < Defs.Defs.Num(); ++i)
	{
		const FElysiumEntityDef& D = Defs.Defs[i];
		// The handle index IS the def-array index (R3): stable, never recycled.
		TUniquePtr<FElysiumEntity> Ent = FElysiumClassRegistry::Get().Create(D, FElysiumEntityHandle(i, Epoch));
		Ent->World = this;   // the seam an entity uses to fire outputs (set before Spawn)

		if (!D.TargetName.IsEmpty())
		{
			NameIndex.Add(FName(*D.TargetName), i);
		}
		ClassIndex.Add(FName(*D.Classname), i);
		EntityList.Add(MoveTemp(Ent));
	}

	// Spawn pass — keyvalues are already applied (Construct); Spawn() is the leaf class's own
	// wiring (no-op for base/inert records in P1.4). Then attach the brush body (P1.5): after
	// Spawn() so a leaf class can have adjusted its own state first.
	const bool bBuildBodies = CVarBrushBodies.GetValueOnGameThread() != 0;
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent)
		{
			Ent->Spawn();
			if (bBuildBodies)
			{
				BuildBrushBody(*Ent);
			}
		}
	}

	UE_LOG(LogElysiumWorld, Log, TEXT("world '%s' live: %d entities (%d brush bodies), epoch %u"),
		*Defs.MapName, EntityList.Num(), Bodies.Num(), Epoch);
}

void FElysiumEntityWorld::BuildBrushBody(FElysiumEntity& Ent)
{
	// R1 — only brush entities get a body; point/logic entities never do. A killed entity (a
	// class Spawn() may have self-destructed) gets nothing.
	if (!Owner || !Ent.Def || !Ent.Def->IsBrush() || Ent.Def->Hulls.Num() == 0 || Ent.IsDead())
	{
		return;
	}
	USceneComponent* Root = Owner->GetRootComponent();
	if (!Root)
	{
		return;
	}

	const EElysiumBrushSolidity Sol = ElysiumBrushSolidityForClass(Ent.Def->Classname);

	// Standard runtime-component recipe: NewObject → cook the setup + place → SetupAttachment →
	// RegisterComponent (which creates the physics body from the now-valid setup, at the origin).
	// P1.7 — a readable Outliner name (Body_<idx>_<name>_<class>); the exact canonical debug string
	// rides along as a component tag (engine-core.md: labels mirror the debug string).
	FName BodyName = NAME_None;
#if WITH_EDITOR
	const FString EntName = Ent.TargetName.IsEmpty() ? TEXT("noname") : Ent.TargetName;
	BodyName = ElysiumEditorObjectName(FString::Printf(TEXT("Body_%d_%s_%s"),
		Ent.Handle.Index, *EntName, *Ent.Def->Classname));
#endif
	UElysiumBrushComponent* Body = NewObject<UElysiumBrushComponent>(Owner, BodyName);
	Body->InitBrush(Ent.Handle, Ent.Def->Hulls, Sol);
	Body->SetupAttachment(Root);
	Body->SetRelativeLocation(Ent.Def->Origin);   // hulls are entity-local; origin places them
	Body->RegisterComponent();
	Owner->AddInstanceComponent(Body);            // shows the body in the editor Outliner
#if WITH_EDITOR
	Body->ComponentTags.Add(FName(*Ent.DebugString()));
#endif

	Ent.Body = Body;
	Bodies.Add(Body);

	// Born hidden (R6) → the body starts non-solid/untouchable. Construct set bHidden without a
	// body to gate; do it now.
	if (Ent.IsInert())
	{
		Body->SetDormant(true);
	}
}

void FElysiumEntityWorld::AddSink(TUniquePtr<IElysiumIOSink> InSink)
{
	// Appended to the live sink list, so it is notified at the same points as the always-on ring
	// buffer and log sinks. Owned here; Teardown frees it with the rest when the world dies.
	if (InSink)
	{
		Sinks.Add(MoveTemp(InSink));
	}
}

void FElysiumEntityWorld::RouteBrushTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	FElysiumEntity* E = Resolve(Brush);
	if (!E || E->IsInert())
	{
		return;   // a dormant/dead brush cannot be touched (R6)
	}
	if (bBegin)
	{
		++TouchBeginCount;
		E->OnTouchStart(Activator);
	}
	else
	{
		++TouchEndCount;
		E->OnTouchEnd(Activator);
	}
	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) touch %s %s"),
		NowSeconds(), bBegin ? TEXT("begin") : TEXT("end"), *E->DebugString());
}

// --- +use look-cursor (P4.2) ------------------------------------------------------------

namespace
{
	// Arm's-reach for the look-cursor pick. VtMB's player use radius is ~80 Source units; 200 cm is
	// that in Unreal space. P4.4 calibrates it against the retail reach when the use-icon HUD lands.
	constexpr double GElysiumUseReachCm = 200.0;
}

void FElysiumEntityWorld::UpdateUseCursor()
{
	UWorld* UW = Owner ? Owner->GetWorld() : nullptr;
	APlayerController* PC = UW ? UW->GetFirstPlayerController() : nullptr;

	// Camera-ray-pick the nearest usable, non-inert brush entity within reach. A single blocking
	// trace naturally handles occlusion: a wall (or any solid) closer than the button ends the ray,
	// and func_button bodies are Solid (BlockAll), so they block ECC_Visibility like the world does.
	FElysiumEntityHandle Hit;
	if (PC)
	{
		FVector Loc; FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		const FVector End = Loc + Rot.Vector() * GElysiumUseReachCm;

		FCollisionQueryParams Params(FName(TEXT("ElysiumUseCursor")), /*bTraceComplex*/ false);
		Params.AddIgnoredActor(PC->GetPawn());
		FHitResult H;
		// The dedicated +use channel (ELYSIUM_USE_CHANNEL, default-Block): world + solid bodies
		// occlude the ray, so a wall between the player and a button ends it, while staying isolated
		// from ECC_Visibility. func_button/func_door bodies are Solid (BlockAll), so they block it.
		if (UW->LineTraceSingleByChannel(H, Loc, End, ELYSIUM_USE_CHANNEL, Params))
		{
			if (const UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent()))
			{
				const FElysiumEntity* E = Resolve(B->GetOwningEntity());
				if (E && E->IsUsable() && !E->IsInert())
				{
					Hit = E->Handle;
				}
			}
		}
	}

	if (Hit == AimedUsable)
	{
		return;   // no transition — nothing to fire
	}

	// Leave the old, enter the new (either may be empty). Resolve guards stale/dead handles, so an
	// entity that was killed or hidden while aimed at silently drops out without a spurious OnOut.
	if (FElysiumEntity* Old = Resolve(AimedUsable))
	{
		Old->OnUseCursorLeave();
	}
	AimedUsable = Hit;
	if (FElysiumEntity* New = Resolve(AimedUsable))
	{
		New->OnUseCursorEnter();
	}

	// The look-cursor transition is otherwise invisible when a button does not wire OnIn/OnOut
	// (the tutorial buttons don't), so trace it — `log LogElysiumWorld Verbose` surfaces the aim
	// enter/leave for UAT and confirms the pick even without a wired output.
	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) use-cursor -> %s"),
		NowSeconds(), AimedUsable.IsSet() ? *DescribeHandle(AimedUsable) : TEXT("<none>"));
}

void FElysiumEntityWorld::PlayerUse()
{
	// Press whatever the cursor settled on this frame. The player is not an entity yet, so the
	// activator is Invalid (matching the trigger touch path); P4-later makes the pawn an entity.
	if (FElysiumEntity* E = Resolve(AimedUsable))
	{
		E->Use(FElysiumEntityHandle::Invalid());
	}
}

int32 FElysiumEntityWorld::GetAimedUseIcon() const
{
	// The reticle icon the HUD draws this frame: the aimed usable's GetUseIcon() (locked_icon when
	// use-locked, else use_icon), or 0 when nothing usable is under the cursor. Const-resolves the
	// sticky AimedUsable handle set by the last UpdateUseCursor.
	const FElysiumEntity* E = Resolve(AimedUsable);
	return E ? E->GetUseIcon() : 0;
}

// --- Screen fade (P4.5 env_fade) --------------------------------------------------------

void FElysiumEntityWorld::StartScreenFade(const FLinearColor& Color, float Duration, float HoldTime,
	float MaxAlpha, bool bReverse, bool bStayOut)
{
	ScreenFade.bActive   = true;
	ScreenFade.Color     = Color;
	ScreenFade.MaxAlpha  = FMath::Clamp(MaxAlpha, 0.0f, 1.0f);
	ScreenFade.Duration  = FMath::Max(Duration, 0.0f);
	ScreenFade.HoldTime  = FMath::Max(HoldTime, 0.0f);
	ScreenFade.bReverse  = bReverse;
	ScreenFade.bStayOut  = bStayOut;
	ScreenFade.StartTime = NowSeconds();
}

bool FElysiumEntityWorld::GetScreenFade(FLinearColor& OutColor) const
{
	if (!ScreenFade.bActive)
	{
		return false;
	}
	const double T = NowSeconds() - ScreenFade.StartTime;
	const float Dur = ScreenFade.Duration;
	float Alpha;
	if (ScreenFade.bReverse)
	{
		// SF_FADE_IN: reveal. Start opaque at MaxAlpha and clear over Duration, then idle.
		if (T >= Dur)
		{
			return false;
		}
		Alpha = (Dur > KINDA_SMALL_NUMBER) ? FMath::Lerp(ScreenFade.MaxAlpha, 0.0f, (float)(T / Dur)) : 0.0f;
	}
	else
	{
		const float HoldEnd = Dur + ScreenFade.HoldTime;
		const float ReturnEnd = HoldEnd + Dur;
		if (T < Dur)                                   // covering: 0 -> MaxAlpha
		{
			Alpha = (Dur > KINDA_SMALL_NUMBER) ? FMath::Lerp(0.0f, ScreenFade.MaxAlpha, (float)(T / Dur)) : ScreenFade.MaxAlpha;
		}
		else if (T < HoldEnd || ScreenFade.bStayOut)   // holding (or staying out forever)
		{
			Alpha = ScreenFade.MaxAlpha;
		}
		else if (T < ReturnEnd)                        // fading back: MaxAlpha -> 0
		{
			Alpha = (Dur > KINDA_SMALL_NUMBER) ? FMath::Lerp(ScreenFade.MaxAlpha, 0.0f, (float)((T - HoldEnd) / Dur)) : 0.0f;
		}
		else
		{
			return false;                              // finished, faded back to clear
		}
	}
	OutColor = ScreenFade.Color;
	OutColor.A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return OutColor.A > KINDA_SMALL_NUMBER;
}

// --- Open sign window (P4.10) -----------------------------------------------------------

void FElysiumEntityWorld::OpenSign(const FElysiumEntityHandle& NewOwner,
	TSharedPtr<const FElysiumSignData> Data, float FadeInSeconds)
{
	// A second OpenWindow replaces the first (CSignUI keeps one panel). The outgoing sign closes
	// silently: retail does not fire OnUseEnd for a panel the player never dismissed.
	if (OpenSignOwner.IsSet() && OpenSignOwner != NewOwner)
	{
		CloseSign(/*bSilent*/ true);
	}
	OpenSignOwner = NewOwner;
	OpenSignData = MoveTemp(Data);
	OpenSignFadeIn = FMath::Max(FadeInSeconds, 0.0f);
	OpenSignTime = NowSeconds();
}

void FElysiumEntityWorld::CloseSign(bool bSilent)
{
	if (!OpenSignOwner.IsSet())
	{
		return;
	}
	const FElysiumEntityHandle Closing = OpenSignOwner;
	OpenSignOwner = FElysiumEntityHandle();
	OpenSignData.Reset();
	OpenSignFadeIn = 0.0f;
	OpenSignTime = 0.0;

	if (!bSilent)
	{
		// OnUseEnd is the tutorial's whole progression hook (popup_3 -> popup_4, popup_6 ->
		// chopdoor_brush.ScriptHide). Fire it through the entity so it takes the real output path.
		if (FElysiumEntity* Ent = Resolve(Closing))
		{
			static const FName OnUseEnd(TEXT("OnUseEnd"));
			Ent->FireOutput(OnUseEnd, FElysiumEntityHandle());
		}
	}
}

void FElysiumEntityWorld::PlayerDismissSign()
{
	if (!OpenSignOwner.IsSet() || !OpenSignData.IsValid())
	{
		return;
	}
	if (!OpenSignData->bCloseOnLeftClick)
	{
		return;   // a Rules block can opt out (the panel then waits for a scripted CloseWindow)
	}
	if (OpenSignData->MinShowTime > 0.0f &&
		(NowSeconds() - OpenSignTime) < double(OpenSignData->MinShowTime))
	{
		return;   // still inside the enforced dwell
	}
	CloseSign(/*bSilent*/ false);
}

FElysiumEntityHandle FElysiumEntityWorld::GetOpenSign(double* OutOpenTime) const
{
	if (OutOpenTime)
	{
		*OutOpenTime = OpenSignTime;
	}
	return OpenSignOwner;
}

APawn* FElysiumEntityWorld::GetPlayerPawn() const
{
	const UWorld* W = Owner ? Owner->GetWorld() : nullptr;
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

// --- Tick (think-first, retail order) ---------------------------------------------------

void FElysiumEntityWorld::Tick(double Now)
{
	RunThinks(Now);
	ServiceEvents(Now);
}

void FElysiumEntityWorld::RunThinks(double Now)
{
	// Retail: Physics_RunThinkFunctions runs before the event queue. A due, non-inert entity
	// thinks; its next-think is cleared first (Source semantics) so a Think() that doesn't
	// reschedule stops firing. No class overrides Think() in P1.4, so this is inert here.
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityList)
	{
		if (!EntPtr)
		{
			continue;
		}
		FElysiumEntity& Ent = *EntPtr;
		if (Ent.IsInert() || Ent.NextThink == ELYSIUM_NEVER_THINK || Ent.NextThink > Now)
		{
			continue;
		}
		Ent.NextThink = ELYSIUM_NEVER_THINK;
		Ent.Think();
	}
}

void FElysiumEntityWorld::ServiceEvents(double Now)
{
	// Drain every due event, including zero-delay chains queued *during* this pass, until the
	// queue has nothing due or the loop guard trips. Pause holds delivery unless steps are armed.
	int32 Delivered = 0;
	while (EventQueue.HasDue(Now))
	{
		if (EventQueue.IsPaused())
		{
			if (EventQueue.StepsPending() <= 0)
			{
				break;
			}
			EventQueue.ConsumeStep();
		}

		if (Delivered >= GElysiumMaxDrainPerFrame)
		{
			for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
			{
				Sink->OnLoopGuard(Now, Delivered);
			}
			break;
		}

		FElysiumIOEvent Ev;
		EventQueue.PopEarliest(Ev);
		DeliverEvent(Ev, Now);
		++Delivered;
	}
}

// --- Chokepoints ------------------------------------------------------------------------

void FElysiumEntityWorld::AddEvent(FElysiumIOEvent&& Event)
{
	// Chokepoint 2 (R5): the sole entry to the queue. Notify sinks before the queue consumes the
	// event (Add sorts it into place, so it is not necessarily the tail afterwards); sinks read
	// the event's fields, not its Serial, which Add assigns.
	const double Now = NowSeconds();
	for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
	{
		Sink->OnQueued(Now, Event);
	}
	EventQueue.Add(MoveTemp(Event));
}

void FElysiumEntityWorld::FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator,
	const FElysiumVariant& ValueOverride)
{
	if (!Source.Def)
	{
		return;
	}
	const double Now = NowSeconds();
	for (int32 i = 0; i < Source.Def->Outputs.Num(); ++i)
	{
		const FElysiumOutputDef& O = Source.Def->Outputs[i];
		if (FName(*O.Name) != OutputName)   // FName compare folds case
		{
			continue;
		}

		// `times` countdown lives on the entity (the def is immutable); 0 = spent, -1 = unlimited.
		int32& Remaining = Source.OutputTimesRemaining[i];
		if (Remaining == 0)
		{
			continue;
		}
		if (Remaining > 0)
		{
			--Remaining;
		}

		for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
		{
			Sink->OnOutputFired(Now, Source, O);
		}

		FElysiumIOEvent Ev;
		Ev.FireTime = Now + O.Delay;
		Ev.Target = O.Target;
		Ev.Input = FName(*O.Input);
		// A Source COutput<T> fires with its runtime value only where the map author left the param
		// blank; a specified param always wins. Void override => keep the (possibly empty) map param.
		Ev.Param = (!O.Param.IsEmpty() || ValueOverride.IsVoid())
			? FElysiumVariant::String(O.Param) : ValueOverride;
		Ev.PythonSrc = O.Python;
		Ev.Activator = Activator;
		Ev.Caller = Source.Handle;
		AddEvent(MoveTemp(Ev));
	}
}

void FElysiumEntityWorld::EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param,
	double Delay, const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// Hand-made injection through chokepoint 2: build the event and hand it to AddEvent, exactly as
	// FireOutput does for a game output. No Def row, so nothing counts down; the target string is
	// resolved (with !self/!activator) at dispatch, same as any queued delivery.
	FElysiumIOEvent Ev;
	Ev.FireTime = NowSeconds() + FMath::Max(0.0, Delay);
	Ev.Target = Target;
	Ev.Input = Input;
	Ev.Param = Param;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	AddEvent(MoveTemp(Ev));
}

void FElysiumEntityWorld::EnqueuePython(const FString& Source, double Delay,
	const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// ScheduleTask(delay, "<source>") (P5 5.4): a python-only deferred event — no I/O target, just a
	// field-6 source string that DeliverEvent hands to the script host at fire time. Same chokepoint
	// (2) and same queue as a delayed output, so it single-steps and serializes like everything else.
	FElysiumIOEvent Ev;
	Ev.FireTime = NowSeconds() + FMath::Max(0.0, Delay);
	Ev.PythonSrc = Source;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	AddEvent(MoveTemp(Ev));
}

FElysiumVariant FElysiumEntityWorld::EvalCondition(const FString& Source,
	const FElysiumEntityHandle& Self, const FElysiumEntityHandle& Activator)
{
	// logic_pythoncheck's Test (and, later, dlg conditions): evaluate the expression through the
	// installed script host so it obeys the same live/off switch as field-6 and lands in the eval
	// log. Empty source or no host -> Void (error-to-false -> the caller reads OnFalse).
	if (Source.IsEmpty() || !GameState)
	{
		return FElysiumVariant::Void();
	}
	FElysiumScriptContext Ctx;
	Ctx.Self = Self;
	Ctx.Activator = Activator;
	Ctx.World = this;
	return GameState->ScriptHost().Eval(Source, Ctx);
}

void FElysiumEntityWorld::AcceptInput(const FString& Target, FName Input, const FElysiumVariant& Param,
	const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// Chokepoint 1 (R5): the sole input path. A transient event carries the dispatch context to
	// the sinks whether the caller is the queue (DeliverEvent) or a hand-fired console verb.
	const double Now = NowSeconds();
	FElysiumIOEvent Ev;
	Ev.FireTime = Now;
	Ev.Target = Target;
	Ev.Input = Input;
	Ev.Param = Param;
	Ev.Activator = Activator;
	Ev.Caller = Caller;

	TArray<FElysiumEntity*> Targets;
	ResolveTargets(Ev, Targets);
	if (Targets.Num() == 0)
	{
		++UnknownTargetCount;
		const FString Key = FString::Printf(TEXT("%s.%s"), *Target, *Input.ToString());
		if (!UnknownLogged.Contains(Key))
		{
			UnknownLogged.Add(Key);
			for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
			{
				Sink->OnUnknownTarget(Now, Ev);
			}
		}
		return;
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (FElysiumEntity* T : Targets)
	{
		const FElysiumInputThunk Thunk = T->Class ? Reg.FindInput(*T->Class, Input) : nullptr;
		if (!Thunk)
		{
			++UnknownInputCount;
			const FString Key = FString::Printf(TEXT("%s.%s"), *T->Def->Classname, *Input.ToString());
			if (!UnknownLogged.Contains(Key))
			{
				UnknownLogged.Add(Key);
				for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
				{
					Sink->OnUnknownInput(Now, *T, Ev);
				}
			}
			continue;
		}

		FElysiumInputArgs Args;
		Args.Param = Param;
		Args.Activator = Activator;
		Args.Caller = Caller;
		Thunk(*T, Args);

		for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
		{
			Sink->OnDelivered(Now, *T, Ev);
		}
	}
}

void FElysiumEntityWorld::DeliverEvent(const FElysiumIOEvent& Event, double Now)
{
	// The I/O half goes through AcceptInput (the one input path); the field-6 Python half goes
	// to the script host. An output can carry both (105 in the game do).
	if (!Event.Target.IsEmpty())
	{
		AcceptInput(Event.Target, Event.Input, Event.Param, Event.Activator, Event.Caller);
	}

	if (!Event.PythonSrc.IsEmpty() && GameState)
	{
		FElysiumScriptContext Ctx;
		Ctx.Self = Event.Caller;
		Ctx.Activator = Event.Activator;
		Ctx.World = this;
		const FElysiumVariant Result = GameState->ScriptHost().Eval(Event.PythonSrc, Ctx);
		for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
		{
			Sink->OnPython(Now, Event, Result);
		}
	}
}

void FElysiumEntityWorld::ResolveTargets(const FElysiumIOEvent& Event, TArray<FElysiumEntity*>& Out)
{
	const FString& T = Event.Target;
	// Runtime references resolve at dispatch time (R3), against the event's provenance.
	if (T.Equals(GSelfTarget, ESearchCase::IgnoreCase) || T.Equals(GCallerTarget, ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = Resolve(Event.Caller))
		{
			Out.Add(E);
		}
		return;
	}
	if (T.Equals(GActivatorTarget, ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = Resolve(Event.Activator))
		{
			Out.Add(E);
		}
		return;
	}

	// Targetnames are non-unique — fan out over every live (non-dead) match.
	const FName Name(*T);
	for (auto It = NameIndex.CreateConstKeyIterator(Name); It; ++It)
	{
		const int32 Idx = It.Value();
		if (EntityList.IsValidIndex(Idx))
		{
			FElysiumEntity* E = EntityList[Idx].Get();
			if (E && !E->IsDead())
			{
				Out.Add(E);
			}
		}
	}
}

// --- Resolution -------------------------------------------------------------------------

FElysiumEntity* FElysiumEntityWorld::Resolve(const FElysiumEntityHandle& Handle)
{
	if (!Handle.IsSet() || Handle.Epoch != Epoch || !EntityList.IsValidIndex(Handle.Index))
	{
		return nullptr;
	}
	FElysiumEntity* E = EntityList[Handle.Index].Get();
	return (E && !E->IsDead()) ? E : nullptr;
}

const FElysiumEntity* FElysiumEntityWorld::Resolve(const FElysiumEntityHandle& Handle) const
{
	return const_cast<FElysiumEntityWorld*>(this)->Resolve(Handle);
}

FElysiumEntity* FElysiumEntityWorld::FindByName(const FString& Name)
{
	const FName N(*Name);
	for (auto It = NameIndex.CreateConstKeyIterator(N); It; ++It)
	{
		if (EntityList.IsValidIndex(It.Value()))
		{
			FElysiumEntity* E = EntityList[It.Value()].Get();
			if (E && !E->IsDead())
			{
				return E;
			}
		}
	}
	return nullptr;
}

FElysiumEntity* FElysiumEntityWorld::FindLandmark(const FString& Name)
{
	// info_landmark lookup for the P4.6 landmark transition (the source-map anchor a
	// trigger_changelevel measures the player against, and the dest-map anchor the next load places
	// against). Same name index as FindByName, but filtered to the info_landmark classname so a
	// coincidental targetname reuse can't be mistaken for the landmark.
	static const FString LandmarkClass(TEXT("info_landmark"));
	const FName N(*Name);
	for (auto It = NameIndex.CreateConstKeyIterator(N); It; ++It)
	{
		if (EntityList.IsValidIndex(It.Value()))
		{
			FElysiumEntity* E = EntityList[It.Value()].Get();
			if (E && !E->IsDead() && E->Def && E->Def->Classname == LandmarkClass)
			{
				return E;
			}
		}
	}
	return nullptr;
}

void FElysiumEntityWorld::ForEachNamed(FName Name, TFunctionRef<void(FElysiumEntity&)> Fn)
{
	for (auto It = NameIndex.CreateConstKeyIterator(Name); It; ++It)
	{
		if (EntityList.IsValidIndex(It.Value()))
		{
			if (FElysiumEntity* E = EntityList[It.Value()].Get())
			{
				if (!E->IsDead())
				{
					Fn(*E);
				}
			}
		}
	}
}

// --- Formatting -------------------------------------------------------------------------

FString FElysiumEntityWorld::DescribeHandle(const FElysiumEntityHandle& Handle) const
{
	if (!Handle.IsSet())
	{
		return TEXT("#<null>");
	}
	if (const FElysiumEntity* E = Resolve(Handle))
	{
		return E->DebugString();
	}
	return FString::Printf(TEXT("#%d <stale>"), Handle.Index);
}

FString FElysiumEntityWorld::FormatEventLine(double Now, const FElysiumIOEvent& Event,
	const FString& TargetLabel, const TCHAR* Note) const
{
	return FString::Printf(TEXT("(%8.3f) %s -> %s.%s(%s)%s%s"),
		Now, *DescribeHandle(Event.Caller), *TargetLabel,
		*Event.Input.ToString(), *Event.Param.ToString(),
		Note ? TEXT(" ") : TEXT(""), Note ? Note : TEXT(""));
}

// --- Teardown ---------------------------------------------------------------------------

void FElysiumEntityWorld::Teardown()
{
	// Epoch 0 matches no minted handle, so every outstanding handle goes stale at once (R3).
	Epoch = 0;
	EventQueue.Reset();
	NameIndex.Empty();
	ClassIndex.Empty();

	// Bodies are the world's embodiments — destroy them with the world. (The map actor also frees
	// them when it is destroyed; this handles a world rebuild on a surviving actor, e.g. reload.)
	for (const TWeakObjectPtr<UElysiumBrushComponent>& Body : Bodies)
	{
		if (UElysiumBrushComponent* B = Body.Get())
		{
			B->DestroyComponent();
		}
	}
	Bodies.Empty();

	EntityList.Empty();
	Ring = nullptr;
	Sinks.Empty();
}

// --- Verification / test verbs ----------------------------------------------------------
// The P1.4 test harness: prove the world, the two chokepoints, the queue, and the ring buffer
// end-to-end with nothing but the log. Phase 2 replaces these with the Cog entity/queue windows
// and the full `ent_*` verb set (which fire through this same AcceptInput/queue).

static FElysiumEntityWorld* ElysiumCurrentWorld(UWorld* W)
{
	if (!W)
	{
		return nullptr;
	}
	if (const UGameInstance* GI = W->GetGameInstance())
	{
		if (UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
		{
			if (AElysiumMapActor* Map = Maps->GetCurrentMap())
			{
				return Map->GetEntityWorld();
			}
		}
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldCmd(
	TEXT("elysium.world"),
	TEXT("elysium.world — summarize the live entity world (counts, classname histogram, queue, ring, dead wires)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world: no live world (load a map first)"));
			return;
		}

		UE_LOG(LogElysiumWorld, Display, TEXT("world: %d entities, epoch %u, now %.3fs"),
			EW->NumEntities(), EW->GetEpoch(), EW->NowSeconds());

		TMap<FString, int32> Histo;
		for (const TUniquePtr<FElysiumEntity>& E : EW->Entities())
		{
			if (E && E->Def)
			{
				++Histo.FindOrAdd(E->Def->Classname);
			}
		}
		Histo.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 Shown = 0;
		for (const TPair<FString, int32>& Pair : Histo)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("  %5d  %s"), Pair.Value, *Pair.Key);
			if (++Shown >= 15)
			{
				UE_LOG(LogElysiumWorld, Display, TEXT("  ... (%d classnames total)"), Histo.Num());
				break;
			}
		}

		UE_LOG(LogElysiumWorld, Display,
			TEXT("queue: %d pending%s | ring: %d/%d | dead wires: %d unknown targets, %d unknown inputs"),
			EW->Queue().Num(), EW->Queue().IsPaused() ? TEXT(" (paused)") : TEXT(""),
			EW->RingBuffer().Num(), EW->RingBuffer().Capacity(),
			EW->UnknownTargets(), EW->UnknownInputs());
		UE_LOG(LogElysiumWorld, Display, TEXT("brush bodies: %d | touches: %d begin, %d end"),
			EW->NumBrushBodies(), EW->TouchBegins(), EW->TouchEnds());
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldIoCmd(
	TEXT("elysium.world.io"),
	TEXT("elysium.world.io [n] — dump the last n I/O history lines from the ring buffer (default 40)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world.io: no live world (load a map first)"));
			return;
		}
		const int32 N = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 40;
		TArray<FString> Lines;
		EW->RingBuffer().CollectOrdered(N, Lines);
		UE_LOG(LogElysiumWorld, Display, TEXT("I/O history: %d lines (of %d recorded)"),
			Lines.Num(), EW->RingBuffer().Num());
		for (const FString& L : Lines)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("%s"), *L);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldFireCmd(
	TEXT("elysium.world.fireinput"),
	TEXT("elysium.world.fireinput <target> <Input> [param] — inject an input through AcceptInput (test harness; P2's ent_fire supersedes)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world.fireinput: no live world (load a map first)"));
			return;
		}
		if (Args.Num() < 2)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("usage: elysium.world.fireinput <target> <Input> [param]"));
			return;
		}
		const FString Param = Args.Num() >= 3 ? Args[2] : FString();
		UE_LOG(LogElysiumWorld, Display, TEXT("fireinput %s.%s(%s)"), *Args[0], *Args[1], *Param);
		EW->AcceptInput(Args[0], FName(*Args[1]), FElysiumVariant::String(Param),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	}));
