#include "ElysiumInputRouter.h"

#include "ElysiumBinds.h"
#include "Player/ElysiumCommandBus.h"
#include "ElysiumCommands.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumGameClock.h"
#include "ElysiumPlayerBody.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumRouter, Log, All);

namespace
{
	// The mouse axes are read as **raw counts** and scaled here, not by the engine: `AxisConfig`
	// sensitivity for MouseX/MouseY is 1.0 and FOV scaling is off in `DefaultInput.ini`, so
	// `sensitivity x m_yaw` is the only multiplier between the device and the view. That is VtMB's
	// own 0.066 degrees per count, and it is why look never picks up a frame-rate term.
	float CvarFloat(const TCHAR* Name, float Default)
	{
		const FString Value = ElysiumCommandBus::Console().GetCvar(Name);
		return Value.IsEmpty() ? Default : FCString::Atof(*Value);
	}
}

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
	// The dev layer occupies no bare key a player can bind (`input-architecture.md` § Reserved keys):
	// `v` is `+movedown` and `t` is `toggleuiside`, so the two Elysium dev toggles are chords.
	BindLine(Input, FInputChord(EKeys::V, /*bShift*/ false, /*bCtrl*/ true, false, false), IE_Pressed,
		TEXT("noclip"), /*bEngineCommand*/ false);
	BindLine(Input, FInputChord(EKeys::T, false, true, false, false), IE_Pressed,
		TEXT("elysium.togglesky"), /*bEngineCommand*/ true);
#endif
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

float UElysiumInputRouter::MouseYawScale() const
{
	return CvarFloat(TEXT("sensitivity"), ElysiumInput::DefaultSensitivity)
		 * CvarFloat(TEXT("m_yaw"), ElysiumInput::DefaultMouseYaw);
}

float UElysiumInputRouter::MousePitchScale() const
{
	// A negative `m_pitch` is VtMB's invert-Y, so the sign rides through untouched.
	return CvarFloat(TEXT("sensitivity"), ElysiumInput::DefaultSensitivity)
		 * CvarFloat(TEXT("m_pitch"), ElysiumInput::DefaultMousePitch);
}

void UElysiumInputRouter::ClearHeldButtons()
{
	CmdBuilder.ClearButtons();
}

void UElysiumInputRouter::SampleFrame(float DeltaSeconds)
{
	// Bound the delta here, at the point the command is built, so a hitch never *enters* the
	// command stream: a recording made across a level-load stutter replays as the game would have
	// simulated it, not as the stall happened to be measured.
	DeltaSeconds = static_cast<float>(ElysiumFrame::ClampFrameDelta(DeltaSeconds));

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
	Rot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch + Cmd.LookDelta.Y), -89.0f, 89.0f);
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
