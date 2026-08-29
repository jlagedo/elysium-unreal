#include "ElysiumInputRouter.h"

#include "ElysiumBinds.h"
#include "ElysiumInputAssets.h"
#include "ElysiumInputScope.h"
#include "ElysiumInputSubsystem.h"
#include "Player/ElysiumCommandBus.h"
#include "ElysiumCommands.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumGameClock.h"
#include "ElysiumPlayerBody.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumRouter, Log, All);

// The stick telemetry probe: the tuning instrument for the `joy_*` surface. It counts down
// *deflected* frames rather than frames, so the window survives the seconds between arming it and a
// hand reaching the pad, and it reports the device's own value beside the degrees that reached the
// view — which is the only way to tell a pad that is noisy from a shaping that is wrong. The
// measured properties in `ElysiumLookCurve.h` were read off this.
static TAutoConsoleVariable<int32> CVarLookProbe(
	TEXT("elysium.LookProbe"), 0,
	TEXT("Log this many deflected frames of right-stick telemetry: the raw device value, the ")
	TEXT("frame delta and the yaw/pitch degrees applied."),
	ECVF_Default);

namespace
{
	// A file-static rather than a member: the probe reads a value the router does not otherwise
	// keep, and nothing outside this file may come to depend on it.
	FVector2D GProbeStickLook = FVector2D::ZeroVector;
}

// Mouse2D arrives through IA_MouseLook after Enhanced Input's Smooth modifier. Its AxisConfig scale
// is 1.0 and FOV scaling is off, so `sensitivity x m_yaw/m_pitch` remains the only multiplier between
// the smoothed counts and the view. The action stays separate from IA_Look: mouse input is a
// displacement already made this frame, while the right stick is a held deflection that Build turns
// into a rate.

void UElysiumInputRouter::Setup(APlayerController* Controller, UInputComponent* Input)
{
	PC = Controller;
	if (!Input)
	{
		return;
	}
	// Installing twice onto one component would double every binding, and a `Once` verb would fire
	// twice per press. A router is per-controller and a controller is per-world epoch, so this only
	// guards a re-entrant SetupInputComponent — but it is the cheap half of that bargain.
	if (BoundInput.Get() == Input)
	{
		return;
	}
	BoundInput = Input;

	for (const FElysiumDefaultBind& Bind : ElysiumBinds::Defaults())
	{
		if (ElysiumBinds::IsReserved(Bind.Key))
		{
			// The guarantee is a check, not a convention: a default that lands on a dev key is a bug
			// in the table, not something to silently honour.
			UE_LOG(LogElysiumRouter, Warning,
				TEXT("default bind '%s' -> '%s' lands on a reserved key; skipped"),
				*Bind.Key.ToString(), Bind.Command);
			continue;
		}
		BindDefault(Input, Bind);
	}
	BindDebugChords(Input);
	BindEnhancedActions(Input);

	// Every `+cmd` latch this local player's keys produce lands in our builder.
	FElysiumCommands::Get().SetUserCmdSink(&CmdBuilder);

	UE_LOG(LogElysiumRouter, Log, TEXT("input router: %d default binds installed"),
		ElysiumBinds::Defaults().Num());
}

void UElysiumInputRouter::Shutdown()
{
	if (FElysiumCommands::Get().GetUserCmdSink() == &CmdBuilder)
	{
		FElysiumCommands::Get().SetUserCmdSink(nullptr);
	}
	CmdBuilder.Reset();
	// `Reset` already zeroed the button field, so the owed releases have nothing left to clear and
	// firing them here would run `-cmd` handlers after the sink is gone.
	PendingTapReleases.Reset();
	EnhancedActions = nullptr;
	BoundInput.Reset();
	PC.Reset();
}

bool UElysiumInputRouter::TapCommand(const FString& Line)
{
	FString Press;
	FString Release;
	if (!ElysiumCommandBus::ParseTap(Line, Press, Release))
	{
		UE_LOG(LogElysiumRouter, Warning,
			TEXT("tap refused: '%s' does not name a declared +/- button verb, so there is nothing to "
			     "press and release"), *Line);
		return false;
	}
	if (bReplaying)
	{
		// A replayed frame takes its command from the stream and never samples the builder, so the
		// press would latch where nothing reads it. Refused out loud rather than accepted and lost:
		// a driver that thinks it clicked and did not is the whole failure this verb exists to avoid.
		UE_LOG(LogElysiumRouter, Warning,
			TEXT("tap '%s' refused: a command stream is replaying, and a replayed frame carries the "
			     "recorded intent rather than anything pressed live"), *Line);
		return false;
	}
	// The press goes through the ordinary bus, so an alias, a handler and the latch all behave exactly
	// as they do for a key — the tap owns only when the release happens.
	ElysiumCommandBus::Exec(Press);
	PendingTapReleases.Add(MoveTemp(Release));
	return true;
}

void UElysiumInputRouter::ReleaseTaps()
{
	if (PendingTapReleases.IsEmpty())
	{
		return;
	}
	// Moved out first: a `-cmd` handler that taps again must owe its release to the NEXT frame rather
	// than have it consumed by the loop that is running.
	TArray<FString> Owed = MoveTemp(PendingTapReleases);
	PendingTapReleases.Reset();
	for (const FString& Line : Owed)
	{
		ElysiumCommandBus::Exec(Line);
	}
}

void UElysiumInputRouter::BindLine(UInputComponent* Input, const FInputChord& Chord, EInputEvent Event,
	const FString& Line, bool bEngineCommand)
{
	// `UInputComponent::BindKey` carries no payload overload (only `BindAction` does), so the binding
	// is built by hand and the command string rides in the lambda's capture. A weak lambda, because
	// the input component outlives nothing here but the delegate must still be safe if it did.
	FInputKeyBinding Binding(Chord, Event);
	Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Line, bEngineCommand]()
	{
		if (bEngineCommand)
		{
			FireEngineCommand(Line);
		}
		else
		{
			FireCommand(Line);
		}
	});
	// Escape has to work while the world is held, which is exactly when it is needed: the pause menu
	// pauses, and a paused world stops delivering bindings that do not ask for this. A release must
	// land through a pause too, or the latch it lifts sticks.
	Binding.bExecuteWhenPaused = true;
	Input->KeyBindings.Emplace(MoveTemp(Binding));
}

void UElysiumInputRouter::BindDefault(UInputComponent* Input, const FElysiumDefaultBind& Bind)
{
	const FString Command(Bind.Command);
	BindLine(Input, FInputChord(Bind.Key), IE_Pressed, Command, /*bEngineCommand*/ false);

	if (Command.StartsWith(TEXT("+")))
	{
		BindLine(Input, FInputChord(Bind.Key), IE_Released, TEXT("-") + Command.Mid(1), false);
	}
}

void UElysiumInputRouter::BindDebugChords(UInputComponent* Input)
{
#if !UE_BUILD_SHIPPING
	// The dev layer occupies no bare key a player can bind (`docs/architecture/input-architecture.md` § Reserved keys):
	// `v` is `+movedown` and `t` is `toggleuiside`, so the two Elysium dev toggles are chords.
	BindLine(Input, FInputChord(EKeys::V, /*bShift*/ false, /*bCtrl*/ true, false, false), IE_Pressed,
		TEXT("noclip"), /*bEngineCommand*/ false);
	BindLine(Input, FInputChord(EKeys::T, false, true, false, false), IE_Pressed,
		TEXT("elysium.togglesky"), /*bEngineCommand*/ true);
#endif
}

void UElysiumInputRouter::BindEnhancedActions(UInputComponent* Input)
{
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input);
	if (!Enhanced)
	{
		UE_LOG(LogElysiumRouter, Error,
			TEXT("gamepad input unavailable: controller input component is not EnhancedInputComponent"));
		return;
	}

	EnhancedActions = LoadObject<UElysiumInputActionSet>(nullptr, ElysiumInputAssets::ActionSetPath);
	if (!EnhancedActions)
	{
		UE_LOG(LogElysiumRouter, Error,
			TEXT("gamepad input assets are missing; run `uv run elysium export bundle policy` (expected %s)"),
			ElysiumInputAssets::ActionSetPath);
		return;
	}

	const FElysiumInputActionDefinition* Move = EnhancedActions->Find(TEXT("Move"));
	const FElysiumInputActionDefinition* Look = EnhancedActions->Find(TEXT("Look"));
	const FElysiumInputActionDefinition* MouseLook = EnhancedActions->Find(TEXT("MouseLook"));
	if (!Move || !Move->Action || !Look || !Look->Action || !MouseLook || !MouseLook->Action)
	{
		UE_LOG(LogElysiumRouter, Error,
			TEXT("input action set is incomplete: Move, Look and MouseLook must reference generated actions"));
		return;
	}

	Enhanced->BindAction(Move->Action, ETriggerEvent::Triggered,
		this, &UElysiumInputRouter::OnAnalogMove);
	Enhanced->BindAction(Look->Action, ETriggerEvent::Triggered,
		this, &UElysiumInputRouter::OnAnalogLook);
	Enhanced->BindAction(MouseLook->Action, ETriggerEvent::Triggered,
		this, &UElysiumInputRouter::OnMouseLook);

	int32 CommandCount = 0;
	for (const FElysiumInputActionDefinition& Definition : EnhancedActions->Actions)
	{
		if (!Definition.Action || Definition.Command.IsEmpty())
		{
			continue;
		}
		const FName Command(*Definition.Command);
		Enhanced->BindAction(Definition.Action, ETriggerEvent::Started,
			this, &UElysiumInputRouter::OnCommandDown, Command);
		if (Definition.bButtonPair)
		{
			Enhanced->BindAction(Definition.Action, ETriggerEvent::Completed,
				this, &UElysiumInputRouter::OnCommandUp, Command);
			Enhanced->BindAction(Definition.Action, ETriggerEvent::Canceled,
				this, &UElysiumInputRouter::OnCommandUp, Command);
		}
		++CommandCount;
	}

	UE_LOG(LogElysiumRouter, Log,
		TEXT("enhanced input: Move, Look, MouseLook and %d command action(s) installed"), CommandCount);
}

void UElysiumInputRouter::FireCommand(FString Line)
{
	// Straight into the bus, so a bound key is indistinguishable from a level script, a `.dlg`
	// action or an MCP call firing the same verb.
	ElysiumCommandBus::Exec(Line);
}

void UElysiumInputRouter::FireEngineCommand(FString Line)
{
	// Plane 1, the dev layer: an `elysium.*` verb is an engine console command, not a VtMB one, and
	// it deliberately does not go through the command bus — the two planes never share a name.
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), *Line);
	}
}

void UElysiumInputRouter::OnMouseLook(const FInputActionValue& Value)
{
	const FVector2D Mouse = Value.Get<FVector2D>();
	if (!Mouse.IsNearlyZero())
	{
		CmdBuilder.AddLook(Mouse.X * MouseYawScale(), Mouse.Y * MousePitchScale());
	}
}

void UElysiumInputRouter::OnAnalogMove(const FInputActionValue& Value)
{
	// Raw, in the pad's own frame. The mapping carries no dead zone and no scalar: shaping a stick
	// needs the frame's clamped delta and per-frame filter state, neither of which an Enhanced Input
	// modifier has, and splitting it across an asset and a function would give one feel two owners.
	CmdBuilder.SetStickMove(Value.Get<FVector2D>());
}

void UElysiumInputRouter::OnAnalogLook(const FInputActionValue& Value)
{
	// **Raw deflection, not a rate.** The mapping's only modifier is the device-frame Y negate; the
	// dead zone, the saturation, the response curve, the filter and the ramp are all
	// `ElysiumInput::ShapeStickLook`, applied in Build against the clamped, dilated frame time every
	// other look source already uses. That placement is forced, not stylistic: the filter is a
	// half-life and the ramp is a charge, so both need a delta they can trust, and an Enhanced Input
	// modifier only ever sees the raw engine one.
	CmdBuilder.SetStickLook(Value.Get<FVector2D>());
	GProbeStickLook = Value.Get<FVector2D>();
}

void UElysiumInputRouter::OnCommandDown(FName Command)
{
	UE_LOG(LogElysiumRouter, Display, TEXT("enhanced input action pressed: %s"),
		*Command.ToString());
	FireCommand(Command.ToString());
}

void UElysiumInputRouter::OnCommandUp(FName Command)
{
	const FString Press = Command.ToString();
	if (Press.StartsWith(TEXT("+")))
	{
		const FString Release = TEXT("-") + Press.Mid(1);
		UE_LOG(LogElysiumRouter, Display, TEXT("enhanced input action released: %s"),
			*Release);
		FireCommand(Release);
	}
}

float UElysiumInputRouter::MouseYawScale() const
{
	return LookTuning.YawScale();
}

float UElysiumInputRouter::MousePitchScale() const
{
	// A negative `m_pitch` is VtMB's invert-Y, so the sign rides through untouched.
	return LookTuning.PitchScale();
}

void UElysiumInputRouter::RefreshLookTuning()
{
	// Read live, so a `sensitivity` write from the console or the options screen takes effect without
	// a reload — the same thing `FElysiumMoveTuning::LoadFrom` does for the mover. The mouse *scale*
	// therefore uses the tuning refreshed on the previous frame, because the axis callbacks run ahead
	// of `PlayerTick`; that one-frame latency is the price of having a single reader of the cvar.
	const auto ReadCvar = [](const TCHAR* Name)
	{
		return ElysiumCommandBus::Console().GetCvar(Name);
	};
	LookTuning.LoadFrom(ReadCvar);
	CmdBuilder.SetLookTuning(LookTuning);

	// The pad's tuning off the same store, on the same frame, so `elysium.cmd joy_smoothing 0.06`
	// takes effect the way `sensitivity` does. There is no retail-linear predicate to guard here:
	// gamepad feel has no original, so no value of it is a divergence to announce.
	ElysiumInput::FElysiumStickTuning StickTuning;
	StickTuning.LoadFrom(ReadCvar);
	CmdBuilder.SetStickTuning(StickTuning);

	// Say so out loud the first time the curve is engaged. VtMB's mouse path is linear, so this is a
	// Feel divergence rather than a setting, and it belongs in any A/B run's log rather than only in
	// a cvar dump (`docs/architecture/input-architecture.md` § Feel).
	if (!LookTuning.IsRetailLinear() && !bWarnedLookCurve)
	{
		bWarnedLookCurve = true;
		UE_LOG(LogElysiumRouter, Warning,
			TEXT("look curve engaged (look_curve=%.3f exponent=%.2f threshold=%.1f max=%.2f): VtMB's ")
			TEXT("mouse path is linear, so this is a stated Feel divergence, not a setting."),
			LookTuning.Curve, LookTuning.Exponent, LookTuning.Threshold, LookTuning.MaxScale);
	}
}

void UElysiumInputRouter::ApplyScopeState(const FElysiumInputState& State)
{
	CmdBuilder.ClearButtons();
	Current = ElysiumInput::GateGameplayCommand(State, Current);
	if (!ElysiumInput::AllowsGameplayCommands(State))
	{
		PublishCurrentToBody();
	}
}

void UElysiumInputRouter::SampleFrame(float DeltaSeconds)
{
	// Bound the delta here, at the point the command is built, so a hitch never *enters* the
	// command stream: a recording made across a level-load stutter replays as the game would have
	// simulated it, not as the stall happened to be measured. The scale comes from the world
	// settings rather than the substrate clock — it is the same number `FixupDeltaSeconds` already
	// multiplied this delta by, and reading it here keeps Player off a Session dependency.
	const UWorld* SampleWorld = GetWorld();
	const AWorldSettings* Settings = SampleWorld ? SampleWorld->GetWorldSettings() : nullptr;
	const double FrameScale = Settings ? Settings->GetEffectiveTimeDilation() : 1.0;
	DeltaSeconds = static_cast<float>(ElysiumFrame::ClampFrameDelta(DeltaSeconds, FrameScale));

	RefreshLookTuning();

	if (bReplaying)
	{
		if (const FElysiumUserCmd* Recorded = Replay.Next())
		{
			Current = *Recorded;
			// The replayed frame keeps its recorded intent but runs on this frame's delta, so a
			// replay at a different frame rate is still the same input.
			Current.DeltaSeconds = DeltaSeconds;
		}
		else
		{
			StopReplay();
			return;
		}
	}
	else
	{
		Current = CmdBuilder.Build(DeltaSeconds);
	}

	// The press has now been sampled into this frame's command, so the key-up it is owed runs here and
	// the next frame's command carries the bit cleared. On a replayed frame nothing sampled the
	// builder, and the release still runs — a latch the stream never carried must not survive it.
	ReleaseTaps();

	if (const APlayerController* Controller = PC.Get())
	{
		if (const UElysiumInputSubsystem* InputSubsystem =
			UElysiumInputSubsystem::Get(Controller->GetGameInstance()))
		{
			Current = ElysiumInput::GateGameplayCommand(InputSubsystem->State(), Current);
		}
	}
	if (bReplaying)
	{
		ReplayLog.Record(Current);
	}
	else if (bRecording)
	{
		Record.Record(Current);
	}

	if (const int32 ProbeFrames = CVarLookProbe.GetValueOnGameThread(); ProbeFrames > 0)
	{
		const APlayerController* Probe = PC.Get();
		const FVector Raw = Probe && Probe->PlayerInput
			? Probe->PlayerInput->GetRawVectorKeyValue(EKeys::Gamepad_Right2D)
			: FVector::ZeroVector;
		// Only a deflected frame spends the budget, so the window survives the seconds between
		// arming the probe and a hand reaching the stick.
		if (!Raw.IsNearlyZero() || !GProbeStickLook.IsNearlyZero())
		{
			UE_LOG(LogElysiumRouter, Warning,
				TEXT("[lookprobe] f=%llu dt=%.5f raw=(%+.4f,%+.4f) |raw|=%.4f in=(%+.4f,%+.4f) ")
				TEXT("deg=(%+.4f,%+.4f)"),
				GFrameCounter, DeltaSeconds, Raw.X, Raw.Y, FVector2D(Raw.X, Raw.Y).Size(),
				GProbeStickLook.X, GProbeStickLook.Y, Current.LookDelta.X, Current.LookDelta.Y);
			CVarLookProbe->Set(ProbeFrames - 1, ECVF_SetByCode);
		}
	}
	GProbeStickLook = FVector2D::ZeroVector;

	// The look delta is **not** applied here. `AElysiumPlayerController::ProcessPlayerInput` feeds it
	// to `AddYawInput`/`AddPitchInput`, and the engine's own `UpdateRotation` — which runs later in
	// the same `PlayerTick` — integrates it, runs the camera manager's pitch limits over it, writes
	// the control rotation and faces the pawn. One writer, one frame.
	PublishCurrentToBody();
}

void UElysiumInputRouter::PublishCurrentToBody()
{
	const APlayerController* Controller = PC.Get();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
	{
		Body->ApplyUserCmd(Current);
	}
}

// Record / replay

void UElysiumInputRouter::StartRecording()
{
	Record.Reset();
	bRecording = true;
}

void UElysiumInputRouter::StopRecording()
{
	bRecording = false;
}

void UElysiumInputRouter::StartReplay(const FElysiumUserCmdStream& Stream)
{
	Replay = Stream;
	Replay.Rewind();
	ReplayLog.Reset();
	// Held keys are dropped on the way in: a replay is the whole of the intent, not a layer over
	// whatever the player happened to be pressing.
	CmdBuilder.ClearButtons();
	bReplaying = true;
}

void UElysiumInputRouter::StopReplay()
{
	bReplaying = false;
}
