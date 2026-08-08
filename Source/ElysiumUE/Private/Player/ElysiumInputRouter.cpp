#include "ElysiumInputRouter.h"

#include "ElysiumBinds.h"
#include "ElysiumInputAssets.h"
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
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumRouter, Log, All);

// The mouse axes are read as **raw counts** and scaled here, not by the engine: `AxisConfig`
// sensitivity for MouseX/MouseY is 1.0 and FOV scaling is off in `DefaultInput.ini`, so
// `sensitivity x m_yaw` is the only multiplier between the device and the view. That is VtMB's own
// 0.066 degrees per count, and it is why look never picks up a frame-rate term. The scale — and the
// response curve over it — live on `ElysiumInput::FElysiumLookTuning`, refreshed once a frame in
// `RefreshLookTuning`.

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
	BindLookAxes(Input);
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
	EnhancedActions = nullptr;
	BoundInput.Reset();
	PC.Reset();
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

void UElysiumInputRouter::BindLookAxes(UInputComponent* Input)
{
	Input->BindAxisKey(EKeys::MouseX, this, &UElysiumInputRouter::OnMouseX);
	Input->BindAxisKey(EKeys::MouseY, this, &UElysiumInputRouter::OnMouseY);
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
	if (!Move || !Move->Action || !Look || !Look->Action)
	{
		UE_LOG(LogElysiumRouter, Error,
			TEXT("gamepad action set is incomplete: Move and Look must reference generated actions"));
		return;
	}

	Enhanced->BindAction(Move->Action, ETriggerEvent::Triggered,
		this, &UElysiumInputRouter::OnAnalogMove);
	Enhanced->BindAction(Look->Action, ETriggerEvent::Triggered,
		this, &UElysiumInputRouter::OnAnalogLook);

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
		TEXT("enhanced input: Move, Look and %d command action(s) installed"), CommandCount);
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

void UElysiumInputRouter::OnMouseX(float Value)
{
	if (Value != 0.0f)
	{
		CmdBuilder.AddLook(Value * MouseYawScale(), 0.0f);
	}
}

void UElysiumInputRouter::OnMouseY(float Value)
{
	if (Value != 0.0f)
	{
		CmdBuilder.AddLook(0.0f, Value * MousePitchScale());
	}
}

void UElysiumInputRouter::OnAnalogMove(const FInputActionValue& Value)
{
	CmdBuilder.SetAnalogMove(ElysiumInput::GamepadStickToMove(Value.Get<FVector2D>()));
}

void UElysiumInputRouter::OnAnalogLook(const FInputActionValue& Value)
{
	// Degrees per SECOND, not degrees: the mapping's modifier stack scales the stick to a rate and
	// stops there, so the delta is applied once in Build against the clamped, dilated frame time
	// every other look source already uses. Multiplying it here (or in the asset, with
	// ScaleByDeltaTime) would hand the pad the raw engine delta and let a level-load hitch emit a
	// full turn in one command.
	CmdBuilder.SetAnalogLook(Value.Get<FVector2D>());
}

void UElysiumInputRouter::OnCommandDown(FName Command)
{
	FireCommand(Command.ToString());
}

void UElysiumInputRouter::OnCommandUp(FName Command)
{
	const FString Press = Command.ToString();
	if (Press.StartsWith(TEXT("+")))
	{
		FireCommand(TEXT("-") + Press.Mid(1));
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
	LookTuning.LoadFrom([](const TCHAR* Name)
	{
		return ElysiumCommandBus::Console().GetCvar(Name);
	});
	CmdBuilder.SetLookTuning(LookTuning);

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

void UElysiumInputRouter::ClearHeldButtons()
{
	CmdBuilder.ClearButtons();
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
			ReplayLog.Record(Current);
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
		if (bRecording)
		{
			Record.Record(Current);
		}
	}

	ApplyLook(Current);

	APlayerController* Controller = PC.Get();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
	{
		Body->ApplyUserCmd(Current);
	}
}

void UElysiumInputRouter::ApplyLook(const FElysiumUserCmd& Cmd)
{
	APlayerController* Controller = PC.Get();
	if (!Controller || Cmd.LookDelta.IsNearlyZero())
	{
		return;
	}
	// Written straight onto the control rotation rather than through AddYawInput/AddPitchInput: those
	// apply the engine's own legacy input scales, and the whole point of the user command is that the
	// degrees in it are the degrees applied.
	// Yaw wraps and pitch clamps (cl_pitchup / cl_pitchdown 89). Yaw is normalized on the way in
	// rather than left to accumulate: nothing downstream cares, but a control rotation that grows
	// without bound through a long session is a precision problem waiting to happen.
	FRotator Rot = Controller->GetControlRotation();
	Rot.Yaw = FRotator::NormalizeAxis(Rot.Yaw + Cmd.LookDelta.X);
	Rot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch + Cmd.LookDelta.Y),
		-ElysiumInput::PitchClampDegrees, ElysiumInput::PitchClampDegrees);
	Rot.Roll = 0.0f;
	Controller->SetControlRotation(Rot);
}

// =====================================================================================
// Record / replay
// =====================================================================================

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
