#pragma once

#include "CoreMinimal.h"
#include "ElysiumLookCurve.h"

// S5 — intent is data (`docs/architecture/runtime-architecture.md` §8.3). One frame of player intent as
// a value: what the player asked for, not which key is currently down. Movement, the camera and the
// command bus all read this, and nothing anywhere polls a key — which is what makes headless play,
// deterministic replay and a rebindable `+speed` gait the same mechanism rather than three.
//
// Plain C++, no UObject and no engine input type, so the whole build-a-command path is asserted
// with no controller, no viewport and no RHI (`Elysium.Substrate.UserCmd`).

// The button set. **The names are Source's `in_buttons.h`; the bit values are ours** — nothing
// crosses an engine or a file boundary except our own recorded command streams, and VtMB's ± verb
// inventory (`docs/vtmb/controls.md`) is 34 pairs, past what Source's own uint32 carries, because its camera
// and look-mode pairs are client-side state rather than user-command bits.
enum class EElysiumButton : uint64
{
	None        = 0,

	// Movement (`+forward` … `+jlook`)
	Forward     = 1ull << 0,
	Back        = 1ull << 1,
	MoveLeft    = 1ull << 2,
	MoveRight   = 1ull << 3,
	MoveUp      = 1ull << 4,
	MoveDown    = 1ull << 5,
	Left        = 1ull << 6,   // keyboard yaw, cl_yawspeed
	Right       = 1ull << 7,
	LookUp      = 1ull << 8,   // keyboard pitch, cl_pitchspeed
	LookDown    = 1ull << 9,
	Speed       = 1ull << 10,  // `+speed` selects the *slow* gait; the run is the default
	Strafe      = 1ull << 11,  // `+strafe` turns the turn keys into strafe while held
	Duck        = 1ull << 12,
	Jump        = 1ull << 13,
	KLook       = 1ull << 14,
	MLook       = 1ull << 15,
	JLook       = 1ull << 16,

	// Combat and items
	Attack      = 1ull << 17,
	Attack2     = 1ull << 18,
	SecondaryAtk= 1ull << 19,  // `+wpn_secondaryatk`
	Reload      = 1ull << 20,
	Use         = 1ull << 21,
	Feed        = 1ull << 22,

	// UI panels held open (`+chareditor`, `+questlog`)
	CharEditor  = 1ull << 23,
	QuestLog    = 1ull << 24,

	// Camera (`docs/vtmb/camera-view-modes.md`)
	CamIn       = 1ull << 25,
	CamOut      = 1ull << 26,
	CamPitchUp  = 1ull << 27,
	CamPitchDown= 1ull << 28,
	CamYawLeft  = 1ull << 29,
	CamYawRight = 1ull << 30,
	CamMouseMove= 1ull << 31,
	CamDistance = 1ull << 32,
	CommanderMouseMove = 1ull << 33,
};

inline uint64 operator|(EElysiumButton A, EElysiumButton B)
{
	return static_cast<uint64>(A) | static_cast<uint64>(B);
}

namespace ElysiumInput
{
	// Diagnostic name for one bit ("Forward"), or "?" for a value that names no single button.
	const TCHAR* ButtonName(EElysiumButton Button);

	// `Buttons` rendered as `Forward|Speed`, or "-" when nothing is down.
	FString DescribeButtons(uint64 Buttons);

	// VtMB's keyboard look rates (`docs/vtmb/source_movement.md` § View / camera). Degrees per second.
	inline constexpr float KeyboardYawSpeed   = 210.0f;   // cl_yawspeed
	inline constexpr float KeyboardPitchSpeed = 225.0f;   // cl_pitchspeed

	// Mouse: `sensitivity` 3 × `m_yaw`/`m_pitch` 0.022 = 0.066 degrees per count. Held here as the
	// shipped defaults on `ElysiumInput::FElysiumLookTuning` (`ElysiumLookCurve.h`), which also owns
	// the response curve over them.

	// Unreal's 2D gamepad keys are (right, up). The Source-shaped user command is (forward, right),
	// so the device-neutral intent seam owns the one swizzle between them.
	inline FVector2D GamepadStickToMove(const FVector2D& Stick)
	{
		return FVector2D(Stick.Y, Stick.X);
	}

	// The magnitude at which the finished move vector counts as asking for a direction at all. It is
	// a floor against float dust and nothing more: the **dead zone owns the "is the player asking"
	// decision** and owns it once, upstream in `ShapeStickMove`, so anything that survives it is a
	// deliberate push and states its direction. Raising this would put a second, disagreeing dead
	// zone here — a band where the character visibly strafes while the selector calls the state
	// neutral, which is a pad that moves and cannot reach a directional attack.
	inline constexpr float DirectionActuation = UE_KINDA_SMALL_NUMBER;

	// The eight directions a move vector can state, indexed by 45-degree sector: 0 is forward and the
	// count runs toward +Y, which is `+moveright`. Each sector is CENTRED on its own direction, so a
	// push within ±22.5° of an axis states that axis alone and only a genuine diagonal states two.
	inline constexpr uint64 DirectionSectors[8] = {
		static_cast<uint64>(EElysiumButton::Forward),
		static_cast<uint64>(EElysiumButton::Forward)   | static_cast<uint64>(EElysiumButton::MoveRight),
		static_cast<uint64>(EElysiumButton::MoveRight),
		static_cast<uint64>(EElysiumButton::Back)      | static_cast<uint64>(EElysiumButton::MoveRight),
		static_cast<uint64>(EElysiumButton::Back),
		static_cast<uint64>(EElysiumButton::Back)      | static_cast<uint64>(EElysiumButton::MoveLeft),
		static_cast<uint64>(EElysiumButton::MoveLeft),
		static_cast<uint64>(EElysiumButton::Forward)   | static_cast<uint64>(EElysiumButton::MoveLeft),
	};

	// `DAT_10589050`, the immobilize button mask `CPlayerMove::SetupMove` `0x10186120` applies:
	//
	//     mv->m_nButtons = cmd->buttons;
	//     if (!IsMobile(player)) mv->m_nButtons &= ~DAT_10589050;
	//
	// The dword read out of the PE at that address is **`0x807`** =
	// `IN_ATTACK | IN_JUMP | IN_DUCK | IN_ATTACK2`. Four bits, and the omissions are the point:
	//
	// * **`IN_USE` is not masked.** Every shipped opener that immobilizes (`CBaseTerminal`,
	//   `CPropSign`, `CGameSign`, `CTriggerBombSite`, `CTriggerElectricBugaloo`) immobilizes the
	//   player *for* an interaction he is holding `+use` on, so masking `IN_USE` would make the
	//   interaction unendable. An immobilized player can still press use.
	// * **The direction bits are not masked either.** Retail leaves `IN_FORWARD`/`IN_BACK`/
	//   `IN_MOVELEFT`/`IN_MOVERIGHT` standing and instead zeroes `forwardmove`/`sidemove`/`upmove`
	//   in the same gate (`!(GetFlags() & FL_FROZEN) && IsMobile()`), which is why the port's
	//   `ApplyImmobilize` zeroes the analog pair and leaves those bits alone. `+wpn_secondaryatk`
	//   is a VtMB verb of its own and is **not** `IN_ATTACK2`, so it is not in the mask.
	inline constexpr uint64 ImmobilizeButtonMask =
		static_cast<uint64>(EElysiumButton::Attack)   |   // IN_ATTACK  0x001
		static_cast<uint64>(EElysiumButton::Jump)     |   // IN_JUMP    0x002
		static_cast<uint64>(EElysiumButton::Duck)     |   // IN_DUCK    0x004
		static_cast<uint64>(EElysiumButton::Attack2);     // IN_ATTACK2 0x800

	// The direction bits a move vector states, for the consumers that read a BUTTON FIELD rather than
	// the vector — chiefly direction-keyed melee selection, which compares each candidate sequence's
	// authored mask against the held bits (`ElysiumCombo::SelectionMask`).
	//
	// A keyboard sets those bits itself and a stick never does, so without this the pad publishes a
	// permanently neutral selection state and no directional attack is reachable on it at any
	// deflection. The vector is the device-neutral statement of the same intent, so it is derived
	// from rather than duplicated: one owner, and a `+forward` and a pushed stick are
	// indistinguishable by the time either reaches the substrate.
	//
	// **The direction is the vector's ANGLE, not a test on each component**, and that is what makes
	// the two surfaces state the same thing. A key is binary and a thumb is not: a stick almost never
	// rests on an axis, so per-component thresholds turn an ordinary push toward left into
	// `MoveLeft|Forward` — two bits a keyboard player would only ever hold on purpose — and
	// `ElysiumCombo::RankStateMask` ranks the forward/back pair above the strafe pair, so the forward
	// attack wins a press the player read as left. Quantizing to the sector reproduces the keyboard
	// exactly: `+forward` alone is sector 0, `+forward` with `+moveleft` is sector 7, and every
	// cardinal key state round-trips through this function unchanged.
	inline uint64 DirectionButtonsFromMove(const FVector2D& Move)
	{
		if (Move.SizeSquared() <= static_cast<double>(DirectionActuation) * DirectionActuation)
		{
			return 0;
		}
		// Sector width is 45°, and rounding rather than flooring is what centres each sector on its
		// own direction. The double modulo is for the negative half-turn: `Atan2` answers (−π, π].
		constexpr double SectorRadians = UE_DOUBLE_PI / 4.0;
		const double Angle = FMath::Atan2(Move.Y, Move.X);
		const int32 Sector = ((FMath::RoundToInt32(Angle / SectorRadians) % 8) + 8) % 8;
		return DirectionSectors[Sector];
	}
}

// One frame of intent.
struct FElysiumUserCmd
{
	// Monotonic per local player, from 1. A replay that produces the same sequence numbers over the
	// same frames is the identity a recorded stream is checked against.
	uint32 Seq = 0;

	// The frame this intent covers. Clock time, not wall time — a time-scaled or single-stepped
	// frame produces the delta the substrate saw (S1).
	float DeltaSeconds = 0.0f;

	// X = forward (`+forward` − `+back`), Y = side (`+moveright` − `+moveleft`), each −1..1. Already
	// deadzoned and per-device modified by the time it lands here.
	FVector2D Move = FVector2D::ZeroVector;

	// Source's `upmove`: `+moveup` − `+movedown` (ladders, swimming, and the noclip fly).
	float Up = 0.0f;

	// Degrees **this frame**: X = yaw, Y = pitch (positive = up). Mouse counts and the keyboard look
	// pairs are already folded together and scaled — a consumer applies it, it does not scale it.
	FVector2D LookDelta = FVector2D::ZeroVector;

	// EElysiumButton bits.
	uint64 Buttons = 0;

	bool IsDown(EElysiumButton Button) const { return (Buttons & static_cast<uint64>(Button)) != 0; }

	// True when the button is down here and was not in Prev — the press edge a `Once`-shaped
	// consumer (jump, the noclip toggle) acts on.
	bool JustPressed(EElysiumButton Button, const FElysiumUserCmd& Prev) const
	{
		return IsDown(Button) && !Prev.IsDown(Button);
	}
	bool JustReleased(EElysiumButton Button, const FElysiumUserCmd& Prev) const
	{
		return !IsDown(Button) && Prev.IsDown(Button);
	}

	// Intent only — Seq and DeltaSeconds are excluded, because a replay checks that the *same input*
	// reproduces, not that it landed on the same frame index.
	bool SameIntent(const FElysiumUserCmd& Other) const
	{
		return Move.Equals(Other.Move, 1e-4)
			&& FMath::IsNearlyEqual(Up, Other.Up, 1e-4f)
			&& LookDelta.Equals(Other.LookDelta, 1e-4)
			&& Buttons == Other.Buttons;
	}

	void ClearMovement()
	{
		Move = FVector2D::ZeroVector;
		Up = 0.0f;
		Buttons &= ~(
			static_cast<uint64>(EElysiumButton::Forward) |
			static_cast<uint64>(EElysiumButton::Back) |
			static_cast<uint64>(EElysiumButton::MoveLeft) |
			static_cast<uint64>(EElysiumButton::MoveRight) |
			static_cast<uint64>(EElysiumButton::MoveUp) |
			static_cast<uint64>(EElysiumButton::MoveDown) |
			static_cast<uint64>(EElysiumButton::Jump) |
			static_cast<uint64>(EElysiumButton::Duck) |
			static_cast<uint64>(EElysiumButton::Speed) |
			static_cast<uint64>(EElysiumButton::Strafe)
		);
	}

	// `CPlayerMove::SetupMove` `0x10186120`'s immobilize pair, and the whole of what it does to a
	// command: `mv->m_nButtons &= ~0x807` and `forward = side = up = 0`. It is deliberately not
	// `ClearMovement` — the two clear disjoint halves of the command and mean opposite things.
	// `ClearMovement` drops the direction bits and keeps the combat ones; retail's immobilize drops
	// the combat ones (`attack`, `jump`, `duck`, `attack2`) and keeps the direction bits, because
	// zeroing the analog pair has already made them inert and `IN_USE` has to survive
	// (`ElysiumInput::ImmobilizeButtonMask`).
	void ApplyImmobilize()
	{
		Move = FVector2D::ZeroVector;
		Up = 0.0f;
		Buttons &= ~ElysiumInput::ImmobilizeButtonMask;
	}

	FString Describe() const;

	// One line of a recorded stream: `seq dt fwd side up yaw pitch buttons`. Plain text so a stream
	// is diffable and hand-editable, which is what makes it usable as a beat-script input.
	FString ToLine() const;
	static bool FromLine(const FString& Line, FElysiumUserCmd& Out);
};

// Turns button latches and accumulated analog input into one command per frame.
//
// Latches, not polls: VtMB's `+speed` is a press/release pair, and a key held across an input-scope
// change must not bleed into whatever comes next — `ClearButtons` is what the arbiter calls, which
// is the same thing Enhanced Input's `bIgnoreAllPressedKeysUntilRelease` does.
struct FElysiumUserCmdBuilder
{
	// A `+cmd` / `-cmd` pair. Idempotent: two keys bound to one verb both press and the release of
	// the first does not lift the latch, matching VtMB's one-key-owns-the-press rule (`docs/vtmb/controls.md`).
	void SetButton(EElysiumButton Button, bool bDown);
	void SetButtonBits(uint64 Bits, bool bDown);

	bool IsDown(EElysiumButton Button) const { return (Buttons & static_cast<uint64>(Button)) != 0; }
	uint64 ButtonBits() const { return Buttons; }

	// Mouse counts for this frame, in degrees (already scaled by sensitivity × m_yaw/m_pitch).
	void AddLook(float YawDegrees, float PitchDegrees);

	// The response curve applied to those counts at Build time. Held rather than looked up so the
	// build stays free of the console; the router refreshes it from the cvar store each frame.
	void SetLookTuning(const ElysiumInput::FElysiumLookTuning& InTuning) { LookTuning = InTuning; }
	const ElysiumInput::FElysiumLookTuning& GetLookTuning() const { return LookTuning; }

	// Analog move for this frame, −1..1 per axis, replacing whatever a stick contributed. Keyboard
	// buttons are OR-ed on top at Build time, so a pad and a keyboard can drive the same frame.
	// **Already shaped** — the raw-stick door is `SetStickMove`.
	void SetAnalogMove(const FVector2D& InMove) { AnalogMove = InMove; }
	void SetAnalogUp(float InUp) { AnalogUp = InUp; }

	// Analog look for this frame as a RATE in degrees per second, replacing whatever a stick
	// contributed. A rate rather than a finished delta so Build multiplies it by the same clamped,
	// dilated delta the keyboard turn keys get: a stick is a held direction like `+left`, not a
	// mouse count, and pre-multiplying it upstream would let a hitch or a time dilation reach the
	// command stream through the pad alone. **Already shaped** — the raw-stick door is `SetStickLook`.
	void SetAnalogLook(const FVector2D& InLookRate) { AnalogLook = InLookRate; }

	// The raw stick, exactly as the device reported it, in the pad's own (right, up) frame. The
	// shaping runs at Build time rather than here for the same reason the mouse curve does: the dead
	// zone, the response curve and the filter all need the frame's *clamped* delta, and a router
	// callback fires from the input stack with no delta it is allowed to trust. `SetStickMove` takes
	// the pad's frame too and Build owns the one swizzle into the command's (forward, right).
	void SetStickLook(const FVector2D& InDeflection) { StickLook = InDeflection; }
	void SetStickMove(const FVector2D& InDeflection) { StickMove = InDeflection; }

	// The stick tuning, refreshed from the cvar store each frame by the router — the same shape as
	// `SetLookTuning`, and for the same reason: the build stays free of the console.
	void SetStickTuning(const ElysiumInput::FElysiumStickTuning& InTuning) { StickTuning = InTuning; }
	const ElysiumInput::FElysiumStickTuning& GetStickTuning() const { return StickTuning; }

	// Compose the frame and advance Seq. Consumes the look accumulator and the analog values; the
	// button latches persist, because a held key is held.
	FElysiumUserCmd Build(float DeltaSeconds);

	// Drop every latch and every accumulator without producing a command. Called when input is taken
	// away (a screen opens, the world is torn down) so nothing survives into the next context.
	void ClearButtons();

	// Reset the sequence counter too — a fresh local player, or the start of a replay.
	void Reset();

	uint32 LastSeq() const { return Seq; }

private:
	uint64 Buttons = 0;
	ElysiumInput::FElysiumLookTuning LookTuning;
	ElysiumInput::FElysiumStickTuning StickTuning;
	ElysiumInput::FElysiumStickState StickState;
	FVector2D LookAccum = FVector2D::ZeroVector;
	FVector2D AnalogMove = FVector2D::ZeroVector;
	FVector2D AnalogLook = FVector2D::ZeroVector;
	FVector2D StickLook = FVector2D::ZeroVector;
	FVector2D StickMove = FVector2D::ZeroVector;
	float AnalogUp = 0.0f;
	uint32 Seq = 0;
};

// A recorded run of commands. Recording and replaying the same stream is the acceptance test for
// S5 — the whole point of intent being a value — and it is what a play-tier driver feeds the game
// with when there is no input device at all.
struct FElysiumUserCmdStream
{
	TArray<FElysiumUserCmd> Cmds;

	void Record(const FElysiumUserCmd& Cmd) { Cmds.Add(Cmd); }
	void Reset() { Cmds.Reset(); Cursor = 0; }
	void Rewind() { Cursor = 0; }

	int32 Num() const { return Cmds.Num(); }
	bool IsExhausted() const { return Cursor >= Cmds.Num(); }

	// The next recorded command, or null at the end. Advances the cursor.
	const FElysiumUserCmd* Next();

	// One command per line, blank lines and `//` comments skipped on the way back in.
	FString ToText() const;
	static bool FromText(const FString& Text, FElysiumUserCmdStream& Out);

	// True when both streams carry the same intent in the same order. Frame timing is excluded, so
	// a replay driven at a different frame rate still compares equal.
	bool SameIntent(const FElysiumUserCmdStream& Other) const;

private:
	int32 Cursor = 0;
};
