#include "ElysiumUserCmd.h"

namespace
{
	// Declaration order is the report order; the bit is the identity.
	struct FButtonName { EElysiumButton Button; const TCHAR* Name; };
	const FButtonName GButtonNames[] = {
		{ EElysiumButton::Forward,            TEXT("Forward") },
		{ EElysiumButton::Back,               TEXT("Back") },
		{ EElysiumButton::MoveLeft,           TEXT("MoveLeft") },
		{ EElysiumButton::MoveRight,          TEXT("MoveRight") },
		{ EElysiumButton::MoveUp,             TEXT("MoveUp") },
		{ EElysiumButton::MoveDown,           TEXT("MoveDown") },
		{ EElysiumButton::Left,               TEXT("Left") },
		{ EElysiumButton::Right,              TEXT("Right") },
		{ EElysiumButton::LookUp,             TEXT("LookUp") },
		{ EElysiumButton::LookDown,           TEXT("LookDown") },
		{ EElysiumButton::Speed,              TEXT("Speed") },
		{ EElysiumButton::Strafe,             TEXT("Strafe") },
		{ EElysiumButton::Duck,               TEXT("Duck") },
		{ EElysiumButton::Jump,               TEXT("Jump") },
		{ EElysiumButton::KLook,              TEXT("KLook") },
		{ EElysiumButton::MLook,              TEXT("MLook") },
		{ EElysiumButton::JLook,              TEXT("JLook") },
		{ EElysiumButton::Attack,             TEXT("Attack") },
		{ EElysiumButton::Attack2,            TEXT("Attack2") },
		{ EElysiumButton::SecondaryAtk,       TEXT("SecondaryAtk") },
		{ EElysiumButton::Reload,             TEXT("Reload") },
		{ EElysiumButton::Use,                TEXT("Use") },
		{ EElysiumButton::Feed,               TEXT("Feed") },
		{ EElysiumButton::CharEditor,         TEXT("CharEditor") },
		{ EElysiumButton::QuestLog,           TEXT("QuestLog") },
		{ EElysiumButton::CamIn,              TEXT("CamIn") },
		{ EElysiumButton::CamOut,             TEXT("CamOut") },
		{ EElysiumButton::CamPitchUp,         TEXT("CamPitchUp") },
		{ EElysiumButton::CamPitchDown,       TEXT("CamPitchDown") },
		{ EElysiumButton::CamYawLeft,         TEXT("CamYawLeft") },
		{ EElysiumButton::CamYawRight,        TEXT("CamYawRight") },
		{ EElysiumButton::CamMouseMove,       TEXT("CamMouseMove") },
		{ EElysiumButton::CamDistance,        TEXT("CamDistance") },
		{ EElysiumButton::CommanderMouseMove, TEXT("CommanderMouseMove") },
	};
}

const TCHAR* ElysiumInput::ButtonName(EElysiumButton Button)
{
	for (const FButtonName& Entry : GButtonNames)
	{
		if (Entry.Button == Button)
		{
			return Entry.Name;
		}
	}
	return TEXT("?");
}

FString ElysiumInput::DescribeButtons(uint64 Buttons)
{
	if (Buttons == 0)
	{
		return TEXT("-");
	}
	FString Out;
	for (const FButtonName& Entry : GButtonNames)
	{
		if ((Buttons & static_cast<uint64>(Entry.Button)) != 0)
		{
			if (!Out.IsEmpty())
			{
				Out.AppendChar(TEXT('|'));
			}
			Out.Append(Entry.Name);
		}
	}
	return Out;
}

FString FElysiumUserCmd::Describe() const
{
	return FString::Printf(TEXT("#%u dt=%.4f move=(%.3f,%.3f) up=%.3f look=(%.3f,%.3f) buttons=%s"),
		Seq, DeltaSeconds, Move.X, Move.Y, Up, LookDelta.X, LookDelta.Y,
		*ElysiumInput::DescribeButtons(Buttons));
}

FString FElysiumUserCmd::ToLine() const
{
	return FString::Printf(TEXT("%u %.6f %.6f %.6f %.6f %.6f %.6f %llu"),
		Seq, DeltaSeconds, Move.X, Move.Y, Up, LookDelta.X, LookDelta.Y,
		static_cast<unsigned long long>(Buttons));
}

bool FElysiumUserCmd::FromLine(const FString& Line, FElysiumUserCmd& Out)
{
	TArray<FString> Fields;
	Line.ParseIntoArrayWS(Fields);
	if (Fields.Num() != 8)
	{
		return false;
	}
	Out = FElysiumUserCmd();
	Out.Seq          = static_cast<uint32>(FCString::Strtoui64(*Fields[0], nullptr, 10));
	Out.DeltaSeconds = FCString::Atof(*Fields[1]);
	Out.Move.X       = FCString::Atod(*Fields[2]);
	Out.Move.Y       = FCString::Atod(*Fields[3]);
	Out.Up           = FCString::Atof(*Fields[4]);
	Out.LookDelta.X  = FCString::Atod(*Fields[5]);
	Out.LookDelta.Y  = FCString::Atod(*Fields[6]);
	Out.Buttons      = FCString::Strtoui64(*Fields[7], nullptr, 10);
	return true;
}

// FElysiumUserCmdBuilder

void FElysiumUserCmdBuilder::SetButton(EElysiumButton Button, bool bDown)
{
	SetButtonBits(static_cast<uint64>(Button), bDown);
}

void FElysiumUserCmdBuilder::SetButtonBits(uint64 Bits, bool bDown)
{
	if (bDown)
	{
		Buttons |= Bits;
	}
	else
	{
		Buttons &= ~Bits;
	}
}

void FElysiumUserCmdBuilder::AddLook(float YawDegrees, float PitchDegrees)
{
	LookAccum.X += YawDegrees;
	LookAccum.Y += PitchDegrees;
}

FElysiumUserCmd FElysiumUserCmdBuilder::Build(float DeltaSeconds)
{
	FElysiumUserCmd Cmd;
	Cmd.Seq = ++Seq;
	Cmd.DeltaSeconds = DeltaSeconds;
	Cmd.Buttons = Buttons;

	const auto Axis = [this](EElysiumButton Pos, EElysiumButton Neg) -> float
	{
		return (IsDown(Pos) ? 1.0f : 0.0f) - (IsDown(Neg) ? 1.0f : 0.0f);
	};

	Cmd.Move.X = Axis(EElysiumButton::Forward, EElysiumButton::Back);
	Cmd.Move.Y = Axis(EElysiumButton::MoveRight, EElysiumButton::MoveLeft);
	Cmd.Up     = Axis(EElysiumButton::MoveUp, EElysiumButton::MoveDown);

	// `+strafe` held turns the turn keys into strafe, which is why they are buttons and not a look
	// axis: the same two keys mean two different things depending on a third.
	const float TurnAxis = Axis(EElysiumButton::Right, EElysiumButton::Left);
	if (IsDown(EElysiumButton::Strafe))
	{
		Cmd.Move.Y += TurnAxis;
	}
	else
	{
		Cmd.LookDelta.X += TurnAxis * ElysiumInput::KeyboardYawSpeed * DeltaSeconds;
	}
	Cmd.LookDelta.Y +=
		Axis(EElysiumButton::LookUp, EElysiumButton::LookDown) * ElysiumInput::KeyboardPitchSpeed * DeltaSeconds;

	// The movement stick, shaped here for the same reason the look stick is: the dead zone is feel,
	// and feel has exactly one owner. The pad reports (right, up) and the command is (forward, side),
	// so the swizzle is applied after the shaping — the radial band has to see the stick's own frame
	// or an asymmetric zone would rotate with it.
	Cmd.Move += ElysiumInput::GamepadStickToMove(
		ElysiumInput::ShapeStickMove(StickMove, StickTuning));

	// A stick adds to the keyboard rather than replacing it, then the pair is clamped to the unit
	// square the way a digital-only frame already is.
	Cmd.Move += AnalogMove;
	Cmd.Up   += AnalogUp;
	Cmd.Move.X = FMath::Clamp(Cmd.Move.X, -1.0f, 1.0f);
	Cmd.Move.Y = FMath::Clamp(Cmd.Move.Y, -1.0f, 1.0f);
	Cmd.Up     = FMath::Clamp(Cmd.Up, -1.0f, 1.0f);

	// The direction bits restated from the finished vector, so an analog source states them too. A
	// key already set its own bit and re-deriving it is idempotent; a stick set none, and the
	// consumers that read the button field — direction-keyed melee selection above all — would
	// otherwise see a pad as permanently neutral no matter how far it is pushed.
	//
	// It runs on the CLAMPED sum rather than on the stick alone: a frame holding `+forward` and
	// pushing back states one intent, and the vector is where the two were already reconciled.
	Cmd.Buttons |= ElysiumInput::DirectionButtonsFromMove(Cmd.Move);

	// The stick, on the same clamped delta the turn keys just used. Mouse counts are already
	// finished degrees for this frame and are added raw; a stick is a held rate and is not.
	//
	// `ShapeStickLook` is where the pad's dead zone, response curve, filter and ramp all live — a
	// rate in, a rate out — so the pad's whole feel is one function reached from one line. It is
	// deliberately called on **every** frame, including the ones where the stick is centred: the
	// filter has to be given the zero target to decay toward, and the ramp has to be given the idle
	// time to discharge over. Skipping the call on a still frame would freeze both mid-turn.
	Cmd.LookDelta +=
		ElysiumInput::ShapeStickLook(StickLook, StickTuning, DeltaSeconds, StickState) * DeltaSeconds;
	Cmd.LookDelta += AnalogLook * DeltaSeconds;

	// **The response curve applies to the mouse contribution alone**, which is why it is applied
	// here and not to the sum. The turn keys and the stick were added above as rates x delta; a
	// magnitude-keyed curve over the total would silently curve a held `+left` and a stick
	// deflection too, and neither of those is a hand moving a mouse. Shaping the accumulator before
	// it joins the sum is what keeps the three sources separable, and
	// `Elysium.Substrate.UserCmd` asserts it stays that way.
	//
	// At the shipped tuning this is the identity — `look_curve` is 0, the gain is exactly 1.0, and
	// VtMB's `sensitivity` x `m_yaw` path is unchanged. A non-zero `look_curve` is a **stated Feel
	// divergence**: VtMB's mouse path carries no
	// acceleration and no filter, and no `CInput::MouseMove` decompile exists to recover one from.
	Cmd.LookDelta += ElysiumInput::ShapeMouseLook(LookAccum, LookTuning, DeltaSeconds);

	LookAccum = FVector2D::ZeroVector;
	AnalogMove = FVector2D::ZeroVector;
	AnalogLook = FVector2D::ZeroVector;
	// The stick is cleared like every other per-frame value. A centred stick stops producing
	// Triggered events entirely, so a value left standing here would be a turn that never ends.
	StickLook = FVector2D::ZeroVector;
	StickMove = FVector2D::ZeroVector;
	AnalogUp = 0.0f;
	return Cmd;
}

void FElysiumUserCmdBuilder::ClearButtons()
{
	Buttons = 0;
	LookAccum = FVector2D::ZeroVector;
	AnalogMove = FVector2D::ZeroVector;
	AnalogUp = 0.0f;
	// The filter and the ramp go with the latches: input taken away and given back must not resume a
	// turn that was in flight when a screen opened.
	StickLook = FVector2D::ZeroVector;
	StickMove = FVector2D::ZeroVector;
	StickState.Reset();
}

void FElysiumUserCmdBuilder::Reset()
{
	ClearButtons();
	Seq = 0;
}

// FElysiumUserCmdStream

const FElysiumUserCmd* FElysiumUserCmdStream::Next()
{
	if (Cursor >= Cmds.Num())
	{
		return nullptr;
	}
	return &Cmds[Cursor++];
}

FString FElysiumUserCmdStream::ToText() const
{
	FString Out = TEXT("// elysium user command stream: seq dt fwd side up yaw pitch buttons\n");
	for (const FElysiumUserCmd& Cmd : Cmds)
	{
		Out.Append(Cmd.ToLine());
		Out.AppendChar(TEXT('\n'));
	}
	return Out;
}

bool FElysiumUserCmdStream::FromText(const FString& Text, FElysiumUserCmdStream& Out)
{
	Out.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	bool bOk = true;
	for (const FString& Raw : Lines)
	{
		FString Line = Raw;
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("//")))
		{
			continue;
		}
		FElysiumUserCmd Cmd;
		if (FElysiumUserCmd::FromLine(Line, Cmd))
		{
			Out.Cmds.Add(Cmd);
		}
		else
		{
			bOk = false;
		}
	}
	return bOk;
}

bool FElysiumUserCmdStream::SameIntent(const FElysiumUserCmdStream& Other) const
{
	if (Cmds.Num() != Other.Cmds.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Cmds.Num(); ++i)
	{
		if (!Cmds[i].SameIntent(Other.Cmds[i]))
		{
			return false;
		}
	}
	return true;
}
