#include "Misc/AutomationTest.h"

#include "ElysiumAnimationIntent.h"   // the segment -> claim hinge the predicate reads through
#include "ElysiumClipMovement.h"
#include "ElysiumGaitSpeeds.h"   // `m_flMaxspeed`, the ceiling the substituted command is clamped to
#include "ElysiumMoveSolve.h"    // `CheckParameters`' clamp
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumAnimationDriver.h"
#include "Visual/ElysiumBlendGrids.h"

// VtMB's animation-driven movement, asserted as pure rules plus one driver.
//
// Everything here is content-free: the movement records are the shipped baseball bat's own values,
// transcribed, so the sampler is checked against numbers the exporter really wrote rather than
// against a synthetic path that would agree with whatever the implementation does.

static constexpr EAutomationTestFlags GElysiumMeleeMoveTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// File-prefixed for the non-unity build, like every other helper block in this module.
	// `FVector` is double-precision in UE5 and every asserted value here is a float literal, so
	// the component read is narrowed once rather than at sixteen call sites.
	float Cm(double Value) { return static_cast<float>(Value); }

	FElysiumMovementRecord MeleeMoveRow(int32 EndFrame, float V0, float V1, float DirX, float PosX)
	{
		FElysiumMovementRecord Record;
		Record.EndFrame = EndFrame;
		Record.Flags = 4160;
		Record.V0Cm = V0;
		Record.V1Cm = V1;
		Record.YawDegrees = 0.0f;
		Record.Direction = FVector(DirX, 0.0f, 0.0f);
		Record.PositionCm = FVector(PosX, 0.0f, 0.0f);
		return Record;
	}

	// `baseballbat_attack_W1` — 17 frames, 8 records, a straight forward lunge ending at 418.7153 cm.
	// The head of the bat's FORWARD-key chain (`w_open`/`w_close`/`w_hold` 0.5/0.9/0.91, mask
	// `IN_FORWARD`), and the largest authored displacement in the bank: 739 cm/s averaged over the
	// clip against a peak block of 2895 cm/s, where the same body's own `run` cell is 313 cm/s. The
	// neutral chain a standing press selects is much smaller — `_Center1` 107.5 cm, `_Center2`
	// 139.6 cm, and `_attack_heavy_a`, which authors no record at all.
	FElysiumClipMovementPath MeleeMoveW1()
	{
		FElysiumClipMovementPath Path;
		Path.Records =
		{
			MeleeMoveRow(2,    6.5593f,  81.2413f, 1.0f,  43.9003f),
			MeleeMoveRow(4,  192.9930f, 192.9930f, 1.0f, 236.8933f),
			MeleeMoveRow(6,   83.7843f,   0.0000f, 1.0f, 278.7854f),
			MeleeMoveRow(8,   31.2280f,   9.0852f, 1.0f, 298.9420f),
			MeleeMoveRow(10,  22.9512f,   9.1387f, 1.0f, 314.9869f),
			MeleeMoveRow(12,   0.0000f,  98.5355f, 1.0f, 364.2547f),
			MeleeMoveRow(14,  38.7915f,  30.2827f, 1.0f, 398.7918f),
			MeleeMoveRow(16,  30.3338f,   9.5132f, 1.0f, 418.7153f),
		};
		return Path;
	}

	// `baseballbat_attack_med` — 24 frames, 12 records. **It lunges 46.64 cm mid-clip and ends at
	// exactly zero**, which is the whole reason the sampler is piecewise: the final record's position
	// is not "the displacement", and an implementation that read it would make this clip stand still.
	FElysiumClipMovementPath MeleeMoveMed()
	{
		FElysiumClipMovementPath Path;
		Path.Records =
		{
			MeleeMoveRow(2,  19.7533f,  0.5972f, -1.0f, -10.1752f),
			MeleeMoveRow(4,  13.1826f,  0.0000f, -1.0f, -16.7665f),
			MeleeMoveRow(6,   0.0000f, 26.2407f,  1.0f,  -3.6462f),
			MeleeMoveRow(8,  62.0610f, 38.5176f,  1.0f,  46.6431f),
			MeleeMoveRow(10,  6.2638f,  0.0000f, -1.0f,  43.5112f),
			MeleeMoveRow(12, 11.2902f,  2.5623f, -1.0f,  36.5849f),
			MeleeMoveRow(14, 10.9064f,  4.2486f, -1.0f,  29.0075f),
			MeleeMoveRow(16,  8.4296f,  4.7984f, -1.0f,  22.3935f),
			MeleeMoveRow(18,  6.4500f,  0.0000f, -1.0f,  19.1685f),
			MeleeMoveRow(20,  7.2542f,  0.0000f, -1.0f,  15.5414f),
			MeleeMoveRow(22, 11.8172f,  8.0240f, -1.0f,   5.6208f),
			MeleeMoveRow(23,  8.7575f,  2.4840f, -1.0f,   0.0000f),
		};
		return Path;
	}

	// `tremere_Male_Armor_0`'s own ceiling — 208 u/s, the peak cell of its run fan and therefore
	// what `PreThink` publishes as `m_flMaxspeed` while that body is standing on the floor.
	constexpr float MeleeMovePeakCmS = 208.0f * ElysiumMove::U;

	// A 9-cell `move_yaw` fan whose largest cell is `Peak`, shaped like a shipped one: forward is the
	// fastest cell and the strafes fall away from it.
	FElysiumGaitSpeedTable MeleeMoveFan(float Peak)
	{
		FElysiumGaitSpeedTable Table;
		Table.Count = FElysiumGaitSpeedTable::MaxCells;
		Table.AxisMin = -180.0f;
		Table.AxisMax = 180.0f;
		Table.Scale = 1.0f;
		const float Shape[FElysiumGaitSpeedTable::MaxCells] =
			{ 0.55f, 0.62f, 0.74f, 0.88f, 1.0f, 0.88f, 0.74f, 0.62f, 0.55f };
		for (int32 Index = 0; Index < Table.Count; ++Index)
		{
			Table.Cells[Index] = Peak * Shape[Index];
		}
		return Table;
	}

	FElysiumIdealActivityState MeleeMoveIdeal(const TCHAR* Activity, float Cycle, float Hold)
	{
		FElysiumIdealActivityState State;
		State.Activity = Activity;
		State.bHasSequence = true;
		State.Cycle = Cycle;
		State.HoldCycle = Hold;
		return State;
	}

	// A grounded body moving straight ahead at `Speed` cm/s, the same shape the driver suites use.
	FElysiumLocomotionSample MeleeMoveSample(float Speed)
	{
		FElysiumLocomotionSample Sample;
		Sample.bOnGround = true;
		Sample.LocalVelocity = FVector(Speed, 0.0f, 0.0f);
		Sample.WishScale = Speed > 0.0f ? 1.0f : 0.0f;
		Sample.CommandedSpeed = Speed;
		return Sample;
	}

	// A driver standing a player body with a swing claim on its base channel.
	void MeleeMoveArm(FElysiumAnimationDriver& Driver, const TCHAR* Activity, const TCHAR* Label)
	{
		FElysiumAnimationRequest Claim;
		Claim.Source = EElysiumAnimSource::Player;
		Claim.Channel = EElysiumAnimChannel::Base;
		Claim.Priority = EElysiumAnimPriority::Scripted;
		Claim.Label = Label;
		Claim.Activity = Activity;
		Driver.SubmitRequest(Claim);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementLockTest,
	"Elysium.Substrate.MeleeMovementLock", GElysiumMeleeMoveTestFlags)
bool FElysiumMeleeMovementLockTest::RunTest(const FString&)
{
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float Tolerance = 0.01f;

	// --- The sampler, against the shipped `_W1` record values --------------------------------------
	{
		const FElysiumClipMovementPath W1 = MeleeMoveW1();
		constexpr int32 Frames = 17;
		constexpr float Span = static_cast<float>(Frames - 1);

		// Every record's own cumulative position is `previous + 0.5 * (v0 + v1)`, exact on every
		// shipped record — which is what says `v0`/`v1` are lengths rather than rates. Checked against
		// the file's own `pos_x_cm`, so a sampler that treated them as speeds fails here first.
		float Running = 0.0f;
		for (const FElysiumMovementRecord& Record : W1.Records)
		{
			Running += 0.5f * (Record.V0Cm + Record.V1Cm) * static_cast<float>(Record.Direction.X);
			TestEqual(FString::Printf(TEXT("the block ending at frame %d totals its own position"),
				Record.EndFrame), Running, Cm(Record.PositionCm.X), Tolerance);
		}

		FVector Position = FVector::ZeroVector;
		float Yaw = 0.0f;
		ElysiumClipMovement::PositionAtFrame(W1, 2.0f, Position, Yaw);
		TestEqual(TEXT("frame 2 is the first record's whole travel"), Cm(Position.X), 43.9003f, Tolerance);
		ElysiumClipMovement::PositionAtFrame(W1, 4.0f, Position, Yaw);
		TestEqual(TEXT("frame 4 is the second record's cumulative position"), Cm(Position.X), 236.8933f,
			Tolerance);
		ElysiumClipMovement::PositionAtFrame(W1, 16.0f, Position, Yaw);
		TestEqual(TEXT("the last frame is the clip's whole lunge"), Cm(Position.X), 418.7153f, Tolerance);
		TestEqual(TEXT("...and no shipped record turns the body"), Yaw, 0.0f, Tolerance);

		// Halfway through the first block: `v0 * f + 0.5 * (v1 - v0) * f * f` at f = 0.5, which is
		// NOT half the block's travel — the ease is why the record carries two coefficients.
		ElysiumClipMovement::PositionAtFrame(W1, 1.0f, Position, Yaw);
		const float Expected = 6.5593f * 0.5f + 0.5f * (81.2413f - 6.5593f) * 0.25f;
		TestEqual(TEXT("mid-block eases rather than interpolating the endpoints"), Cm(Position.X),
			Expected, Tolerance);
		TestTrue(TEXT("...so it is not half the block"), Position.X < 0.5f * 43.9003f);

		FVector Delta = FVector::ZeroVector;
		TestTrue(TEXT("a whole-clip window samples"),
			ElysiumClipMovement::SampleDelta(W1, Frames, 0.0f, 1.0f, Delta));
		TestEqual(TEXT("...and covers the authored lunge"), Cm(Delta.X), 418.7153f, Tolerance);
		TestEqual(TEXT("...with nothing sideways, because Y is not negated twice"), Cm(Delta.Y), 0.0f,
			Tolerance);

		TestTrue(TEXT("a window inside the clip samples"),
			ElysiumClipMovement::SampleDelta(W1, Frames, 2.0f / Span, 4.0f / Span, Delta));
		TestEqual(TEXT("...and covers exactly that block"), Cm(Delta.X), 236.8933f - 43.9003f, Tolerance);

		// The window is a DELTA between two cycles, so consecutive windows sum to the whole.
		FVector First = FVector::ZeroVector;
		FVector Second = FVector::ZeroVector;
		ElysiumClipMovement::SampleDelta(W1, Frames, 0.0f, 0.5f, First);
		ElysiumClipMovement::SampleDelta(W1, Frames, 0.5f, 1.0f, Second);
		TestEqual(TEXT("two consecutive windows sum to the whole clip"), Cm(First.X + Second.X),
			418.7153f, Tolerance);

		// A window that does not advance still SUBSTITUTES — `Studio_AnimMovement`'s only false is
		// `nummovements == 0`, so a zero-width window answers true with a zero delta and `WalkMove`
		// assigns a zero velocity. The dedicated block further down drives the consequence.
		TestTrue(TEXT("a window that does not advance still substitutes"),
			ElysiumClipMovement::SampleDelta(W1, Frames, 0.5f, 0.5f, Delta));
		TestTrue(TEXT("...with a zero delta"), Delta.IsNearlyZero());
		TestFalse(TEXT("a clip with fewer than two frames samples nothing"),
			ElysiumClipMovement::SampleDelta(W1, 1, 0.0f, 1.0f, Delta));
	}

	// --- The out-and-back case, which a final-record implementation gets exactly wrong -------------
	{
		const FElysiumClipMovementPath Med = MeleeMoveMed();
		constexpr int32 Frames = 24;
		constexpr float Span = static_cast<float>(Frames - 1);

		FVector Position = FVector::ZeroVector;
		float Yaw = 0.0f;
		ElysiumClipMovement::PositionAtFrame(Med, 8.0f, Position, Yaw);
		TestEqual(TEXT("`_attack_med` lunges 46.64 cm mid-clip"), Cm(Position.X), 46.6431f, Tolerance);
		ElysiumClipMovement::PositionAtFrame(Med, 4.0f, Position, Yaw);
		TestEqual(TEXT("...after first stepping BACK, direction and all"), Cm(Position.X), -16.7665f,
			Tolerance);

		FVector Delta = FVector::ZeroVector;
		TestTrue(TEXT("the mid-clip window samples"),
			ElysiumClipMovement::SampleDelta(Med, Frames, 0.0f, 8.0f / Span, Delta));
		TestEqual(TEXT("...and carries the body forward"), Cm(Delta.X), 46.6431f, Tolerance);
		TestTrue(TEXT("the whole-clip window samples"),
			ElysiumClipMovement::SampleDelta(Med, Frames, 0.0f, 1.0f, Delta));
		TestEqual(TEXT("...and ends at exactly zero, which is the file's own value"), Cm(Delta.X), 0.0f,
			Tolerance);
		TestEqual(TEXT("the clip's last authored frame is `numframes - 1`"), Med.LastFrame(),
			Frames - 1);
	}

	// --- A clip that authors no records at all -----------------------------------------------------
	{
		const FElysiumClipMovementPath Empty;
		FVector Delta(1.0f, 2.0f, 3.0f);
		TestTrue(TEXT("an empty path is empty"), Empty.IsEmpty());
		TestFalse(TEXT("...and samples nothing"),
			ElysiumClipMovement::SampleDelta(Empty, 17, 0.0f, 1.0f, Delta));
		TestEqual(TEXT("...leaving the delta at zero rather than at whatever it held"), Cm(Delta.X), 0.0f,
			Tolerance);

		// The lock still holds over such a clip: the command is discarded either way, and only the
		// refill is conditional. That is what pins the body in place for `baseballbat_attack_heavy_a`
		// — the third link of the bat's `_Center1` -> `_Center2` -> `_heavy_a` chain, which the
		// sidecar carries no `movement` row for at all, and the reason the third swing of the chain
		// displaces the player by nothing.
		FElysiumAnimMovementLock Lock;
		Lock.bActive = true;
		Lock.FrameCount = 17;
		TestTrue(TEXT("a lock over a clip with no path is still a lock"), Lock.bActive);
		TestFalse(TEXT("...and has nothing to refill the command from"), Lock.HasPath());

		Lock.Path = MakeShared<FElysiumClipMovementPath>(MeleeMoveW1());
		TestTrue(TEXT("a lock over a clip with a path has one"), Lock.HasPath());
		Lock.FrameCount = 1;
		TestFalse(TEXT("...but a clip of one frame has no cycle to map onto"), Lock.HasPath());
	}

	// --- `CheckParameters`' clamp on the substituted command ---------------------------------------
	{
		// A wish above the ceiling is scaled back onto it, along the direction it was commanded in.
		FVector Above(2000.0f, 0.0f, 0.0f);
		TestTrue(TEXT("a substituted wish above the body's peak is clamped"),
			ElysiumMove::ClampCommandSpeed(Above, MeleeMovePeakCmS));
		TestEqual(TEXT("...to exactly the peak"), Cm(Above.Size()), MeleeMovePeakCmS, Tolerance);
		TestEqual(TEXT("...with nothing added sideways"), Cm(Above.Y), 0.0f, Tolerance);

		// One ratio over all three axes, not a re-normalization: an off-axis wish keeps its bearing.
		FVector Diagonal(1200.0f, -900.0f, 300.0f);
		const FVector Bearing = Diagonal.GetSafeNormal();
		TestTrue(TEXT("an off-axis wish clamps too"),
			ElysiumMove::ClampCommandSpeed(Diagonal, MeleeMovePeakCmS));
		TestEqual(TEXT("...to the peak"), Cm(Diagonal.Size()), MeleeMovePeakCmS, Tolerance);
		TestTrue(TEXT("...keeping the bearing it was authored on"),
			Diagonal.GetSafeNormal().Equals(Bearing, 1e-4));

		// **The `up` component is why the 3-vector form is the one reproduced.** Retail's second
		// clamp, in `WalkMove`, forces `z` to zero before it measures, so a wish under the ceiling
		// horizontally and over it in 3D passes that one untouched — and 56 shipped records author a
		// non-zero `pos_z_cm`. `CheckParameters` measures all three and catches it.
		FVector Vertical(0.9f * MeleeMovePeakCmS, 0.0f, 0.9f * MeleeMovePeakCmS);
		TestTrue(TEXT("a wish under the peak in 2D and over it in 3D is under the 2D clamp"),
			FVector2D(Vertical.X, Vertical.Y).Size() < MeleeMovePeakCmS);
		TestTrue(TEXT("...and is still caught by the 3-vector one"),
			ElysiumMove::ClampCommandSpeed(Vertical, MeleeMovePeakCmS));

		// A wish below the ceiling is left exactly as it was — not re-normalized, not rounded.
		FVector Below(300.0f, -120.0f, 40.0f);
		const FVector Untouched = Below;
		TestFalse(TEXT("a wish below the peak does not clamp"),
			ElysiumMove::ClampCommandSpeed(Below, MeleeMovePeakCmS));
		TestTrue(TEXT("...and is not touched at all"), Below == Untouched);
	}

	// --- The ceiling itself: whose it is, and that it is read per step ------------------------------
	{
		FElysiumGaitSpeeds Speeds;
		Speeds.Run = MeleeMoveFan(MeleeMovePeakCmS);
		Speeds.Walk = MeleeMoveFan(0.55f * MeleeMovePeakCmS);
		Speeds.Sneak = MeleeMoveFan(0.35f * MeleeMovePeakCmS);

		FElysiumWishSpeedInput Body;
		Body.bOnGround = true;
		Body.JumpMaxSpeed = ElysiumMove::JumpMaxSpeed;

		TestEqual(TEXT("grounded, the ceiling is the gait tables' own peak"),
			Cm(ElysiumGait::MaxSpeedFrom(Body, Speeds)), MeleeMovePeakCmS, Tolerance);

		// The airborne ceiling is a different, higher number: `PreThink` pins `m_flMaxspeed` to
		// `sv_jump_maxspeed` for the whole of a jump instead of refilling it from the tables, so an
		// air attack is bounded at 350 u/s where the same body's grounded swing is bounded at 208.
		Body.bOnGround = false;
		TestEqual(TEXT("airborne, the ceiling is `sv_jump_maxspeed`"),
			Cm(ElysiumGait::MaxSpeedFrom(Body, Speeds)), Cm(ElysiumMove::JumpMaxSpeed), Tolerance);
		TestTrue(TEXT("...which is the higher of the two, so an air swing lunges further"),
			ElysiumMove::JumpMaxSpeed > MeleeMovePeakCmS);

		// It tracks the LIVE tables rather than a value captured when the lock armed: `PreThink`'s
		// gait fill runs ahead of the lock predicate and is not gated by it, so a swing thrown while
		// the tables change is bounded by whatever they say on each step.
		Body.bOnGround = true;
		Speeds.Run = MeleeMoveFan(313.0f);
		TestEqual(TEXT("a changed gait table changes the ceiling on the spot"),
			Cm(ElysiumGait::MaxSpeedFrom(Body, Speeds)), 313.0f, Tolerance);
		Speeds.Run = MeleeMoveFan(MeleeMovePeakCmS);

		// A body nothing has pushed a fan onto has no ceiling either, and zero is the value rather
		// than the absence of one: `m_flMaxspeed` is written under the same `peak > 0` condition the
		// cells are and is zeroed by the same `Spawn`. The clamp then erases a substituted lunge on
		// such a body, which is consistent — none of its gaits can command a speed to begin with.
		const FElysiumGaitSpeeds NoFan;
		TestEqual(TEXT("a body with no fan has no ceiling"),
			Cm(ElysiumGait::MaxSpeedFrom(Body, NoFan)), 0.0f, Tolerance);
		FVector ErasedLunge(400.0f, 0.0f, 0.0f);
		TestTrue(TEXT("...so a substituted lunge clamps against it"),
			ElysiumMove::ClampCommandSpeed(ErasedLunge, ElysiumGait::MaxSpeedFrom(Body, NoFan)));
		TestTrue(TEXT("...to a stop, rather than to a non-finite ratio"),
			ErasedLunge.IsZero());

		// **The no-op assertion.** An ordinary player-driven wish IS a gait cell, and no cell of any
		// gait can exceed the peak over all of them — at any direction, any gait, any deflection. So
		// the clamp is invisible to ordinary movement wherever it is placed, and only the
		// substituted command can ever reach it.
		const float Ceiling = ElysiumGait::MaxSpeedFrom(Body, Speeds);
		float Worst = 0.0f;
		for (const bool bDucked : { false, true })
		{
			for (const bool bWalkKey : { false, true })
			{
				for (int32 Degrees = -180; Degrees <= 180; Degrees += 5)
				{
					for (const float Deflection : { 0.25f, 0.5f, 1.0f })
					{
						FElysiumWishSpeedInput Cell = Body;
						Cell.bDucked = bDucked;
						Cell.bWalkKey = bWalkKey;
						Cell.WishYawDegrees = static_cast<float>(Degrees);
						Cell.Scale = Deflection;
						Worst = FMath::Max(Worst, ElysiumGait::WishSpeedFrom(Cell, Speeds));
					}
				}
			}
		}
		TestTrue(FString::Printf(
			TEXT("no player-driven cell reaches the ceiling (worst %.2f against %.2f)"), Worst,
			Ceiling), Worst <= Ceiling);

		// And the clamp really does pass the fastest of them through: the forward run cell IS the
		// peak, so it sits exactly on the ceiling and is still not scaled.
		FElysiumWishSpeedInput Forward = Body;
		Forward.Scale = 1.0f;
		FVector FastestWish(ElysiumGait::WishSpeedFrom(Forward, Speeds), 0.0f, 0.0f);
		const FVector BeforeClamp = FastestWish;
		TestEqual(TEXT("the forward run cell is the peak"), Cm(FastestWish.X), Ceiling, Tolerance);
		TestFalse(TEXT("...and ordinary movement's fastest wish does not clamp"),
			ElysiumMove::ClampCommandSpeed(FastestWish, Ceiling));
		TestTrue(TEXT("...so an ordinary command is unaffected"), FastestWish == BeforeClamp);
	}

	// --- What the cap does to the swing: `_W1` at 66 Hz against that body's peak --------------------
	{
		const FElysiumClipMovementPath W1 = MeleeMoveW1();
		constexpr int32 Frames = 17;
		// The clip's authored rate. 17 frames at 30 fps is 0.5333 s, which is what the cycle advances
		// across.
		constexpr float ClipFps = 30.0f;
		constexpr float Hold = 0.91f;
		constexpr float StepDt = 1.0f / 66.0f;

		// `m_flPlaybackRate` — `0.7 + 0.03 * rank`, so a better character swings faster and the
		// clip's authored blocks arrive in fewer, larger steps.
		const auto Travel = [&](float PlaybackRate, float& OutUnclamped, float& OutClamped)
		{
			const float CycleRate = (ClipFps / static_cast<float>(Frames - 1)) * PlaybackRate;
			float Cycle = 0.0f;
			OutUnclamped = 0.0f;
			OutClamped = 0.0f;
			while (Cycle < Hold)
			{
				const float Next = FMath::Min(Hold, Cycle + CycleRate * StepDt);
				FVector Delta = FVector::ZeroVector;
				if (!ElysiumClipMovement::SampleDelta(W1, Frames, Cycle, Next, Delta))
				{
					break;
				}
				OutUnclamped += Cm(Delta.Size());
				// What `SetupMove` builds and `CheckParameters` then bounds: the window's authored
				// displacement as a world velocity over the step, clamped, integrated back over the
				// same step.
				FVector Wish = Delta / StepDt;
				ElysiumMove::ClampCommandSpeed(Wish, MeleeMovePeakCmS);
				OutClamped += Cm((Wish * StepDt).Size());
				Cycle = Next;
			}
		};

		// **Path length, not displacement**: each step is applied in the player's then-current
		// facing, which is the mouse, so a curving clip covers more path than its authored net. The
		// realized travel is shorter again once the sweep clips it; the ratio is what this measures.
		float Unclamped = 0.0f;
		float Clamped = 0.0f;
		Travel(0.7f, Unclamped, Clamped);
		TestEqual(TEXT("`_W1` integrates 406.5 cm of path over the lock window"), Unclamped, 406.5f,
			0.5f);
		TestEqual(TEXT("...and the cap turns a ~4 m charge into a ~2.4 m step at rank 0"), Clamped,
			239.7f, 0.5f);

		// A faster playback rate reaches `w_hold` in fewer steps, and every step is capped the same,
		// so the swing covers LESS ground rather than more.
		float FastUnclamped = 0.0f;
		float FastClamped = 0.0f;
		Travel(0.85f, FastUnclamped, FastClamped);
		TestEqual(TEXT("the same clip authors the same path at rank 5"), FastUnclamped, 406.5f, 0.5f);
		TestEqual(TEXT("...and the cap makes a faster swing travel less, not more"), FastClamped,
			217.9f, 0.5f);

		TestTrue(TEXT("the clamped lunge is still a lunge, not a stop"), Clamped > 200.0f);
		TestTrue(TEXT("...and is well short of the authored charge"), Clamped < 0.7f * Unclamped);
	}

	// --- The predicate: the release cycle, the 1.0 default, and the four melee rows ---------------
	{
		using namespace ElysiumClipMovement;
		TestEqual(TEXT("the ordinary attack releases at its own `w_hold`"),
			static_cast<int32>(AnimDrivenArmFor(TEXT("ACT_MELEE_ATTACK"))),
			static_cast<int32>(EElysiumAnimDrivenArm::UntilHoldCycle));
		for (const TCHAR* const Whole : { TEXT("ACT_MELEE_AIR_ATTACK"),
			TEXT("ACT_MELEE_ATTACK_2COMBO"), TEXT("ACT_MELEE_ATTACK_HEAVY") })
		{
			TestEqual(FString::Printf(TEXT("%s is driven for its whole clip"), Whole),
				static_cast<int32>(AnimDrivenArmFor(Whole)),
				static_cast<int32>(EElysiumAnimDrivenArm::WholeClip));
		}
		// The four families this rung deliberately does not own read as not driven rather than being
		// absent from the vocabulary — see `EElysiumAnimDrivenArm`.
		for (const TCHAR* const Deferred : { TEXT("ACT_BLOCK"), TEXT("ACT_BLOCK_HEAVY"),
			TEXT("ACT_BLOCKED_REACTION_LEFT"), TEXT("ACT_KNOCKBACK_SMALL_HIGH_BACK"),
			TEXT("ACT_VOMIT_INTO"), TEXT("ACT_FEEDING_ENGAGE_FAILURE"), TEXT("ACT_IDLE"),
			TEXT("ACT_WALK"), TEXT("") })
		{
			TestEqual(FString::Printf(TEXT("'%s' is not an implemented row"), Deferred),
				static_cast<int32>(AnimDrivenArmFor(Deferred)),
				static_cast<int32>(EElysiumAnimDrivenArm::None));
		}

		// `baseballbat_attack_W1` authors 0.91, which is the release cycle AND the combo hand-off.
		TestTrue(TEXT("the swing is driven from its first frame"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.0f, 0.91f)));
		TestTrue(TEXT("...and still is one frame below `w_hold`"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.9099f, 0.91f)));
		TestFalse(TEXT("...and releases exactly AT `w_hold`"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.91f, 0.91f)));
		TestFalse(TEXT("...and stays released for the tail"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.95f, 0.91f)));

		// `baseballbat_attack_jump` authors 0.80, so it releases earlier — the number is per sequence
		// and is never assumed.
		TestTrue(TEXT("a 0.80 hold is still driven at 0.79"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.79f, 0.80f)));
		TestFalse(TEXT("...and released at 0.80"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.80f, 0.80f)));

		// A sequence that authors no combo block authors no `w_hold` and takes 1.0, which is what
		// makes a finisher lock for its whole clip.
		TestTrue(TEXT("an unstated `w_hold` defaults to 1.0 and holds to the end"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.999f, 1.0f)));
		TestFalse(TEXT("...and the cycle guard ends it at 1.0"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 1.0f, 1.0f)));
		const FElysiumIdealActivityState Fresh;
		TestEqual(TEXT("the unstated hold really is 1.0"), Fresh.HoldCycle, 1.0f);

		// The whole-clip arm ignores `w_hold` entirely: the heavy is uninterruptible to the end even
		// where its descriptor states a lower hold.
		TestTrue(TEXT("the heavy is driven past a stated 0.5 hold"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK_HEAVY"), 0.9f, 0.5f)));
		TestFalse(TEXT("...and still ends with its clip"),
			IsAnimationDriven(MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK_HEAVY"), 1.0f, 0.5f)));

		// No sequence on the channel is no lock, whatever the activity says. A headless body and a
		// body whose clip was cut short answer here.
		FElysiumIdealActivityState NoClip = MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.3f, 0.91f);
		NoClip.bHasSequence = false;
		TestFalse(TEXT("a forced activity with no sequence behind it drives nothing"),
			IsAnimationDriven(NoClip));
	}

	// --- No latch: the predicate follows the ideal activity in the same frame ---------------------
	{
		using namespace ElysiumClipMovement;
		FElysiumIdealActivityState State = MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.3f, 0.91f);
		TestTrue(TEXT("mid-swing, the body is animation-driven"), IsAnimationDriven(State));
		// A reaction overwrites the ideal activity. Nothing else moves — same clip, same cycle — and
		// the lock is gone on this frame, with no arm/disarm pair to get out of step.
		State.Activity = TEXT("ACT_KNOCKBACK_SMALL_HIGH_BACK");
		TestFalse(TEXT("...and an overwritten ideal releases it in the same frame"),
			IsAnimationDriven(State));
		State.Activity = TEXT("ACT_MELEE_ATTACK");
		TestTrue(TEXT("...and putting it back re-derives the lock, because nothing was remembered"),
			IsAnimationDriven(State));
	}

	// --- Phase 2's refuse set, joined to the recovered table ---------------------------------------
	{
		using namespace ElysiumClipMovement;
		const ElysiumActionTables::FPlayerActionTuning& Tuning = ElysiumActionTables::PlayerTuning();
		TestEqual(TEXT("the recovered hold ideal is the ordinary melee attack"),
			FString(Tuning.MeleeHoldIdeal), FString(TEXT("ACT_MELEE_ATTACK")));
		TestEqual(TEXT("...and it refuses two activities"), Tuning.MeleeHoldRefuseCount, 2);

		const FElysiumIdealActivityState Tail =
			MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK"), 0.95f, 0.91f);
		TestFalse(TEXT("the tail is no longer animation-driven"), IsAnimationDriven(Tail));
		TestTrue(TEXT("...but it refuses an idle"), RefusesReselection(Tail, TEXT("ACT_IDLE")));
		TestTrue(TEXT("...and an aim"), RefusesReselection(Tail, TEXT("ACT_AIM")));
		TestFalse(TEXT("...and applies a walk, which cuts the clip"),
			RefusesReselection(Tail, TEXT("ACT_WALK")));
		TestFalse(TEXT("...and a run"), RefusesReselection(Tail, TEXT("ACT_RUN")));
		TestFalse(TEXT("...and a crouch"), RefusesReselection(Tail, TEXT("ACT_CROUCH")));

		FElysiumIdealActivityState Finished = Tail;
		Finished.Cycle = 1.0f;
		TestFalse(TEXT("a finished swing refuses nothing"),
			RefusesReselection(Finished, TEXT("ACT_IDLE")));
		FElysiumIdealActivityState Heavy = MeleeMoveIdeal(TEXT("ACT_MELEE_ATTACK_HEAVY"), 0.95f, 1.0f);
		TestFalse(TEXT("the refuse set is the ordinary attack's alone"),
			RefusesReselection(Heavy, TEXT("ACT_IDLE")));
	}

	// --- The driver: the forced activity is published, and the guard sits on the selection pass ---
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("pc_body");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;

		// A settled walking body first, so the record has a gait in it to be displaced.
		Driver.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		const FString Gait = Driver.Selection.RequestedActivity;
		TestTrue(TEXT("a walking player publishes a gait"), !Gait.IsEmpty());
		TestFalse(TEXT("...and is not animation-driven"), Driver.bAnimationDriven);

		MeleeMoveArm(Driver, TEXT("ACT_MELEE_ATTACK"), TEXT("baseballbat_attack_W1"));
		Driver.BaseClipCycle.bPlaying = true;
		Driver.BaseClipCycle.Cycle = 0.2f;
		Driver.BaseClipCycle.HoldCycle = 0.91f;
		Driver.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		TestTrue(TEXT("the swing makes the body animation-driven"), Driver.bAnimationDriven);
		TestEqual(TEXT("...and the published ideal activity is the swing, not the gait underneath"),
			Driver.Selection.RequestedActivity, FString(TEXT("ACT_MELEE_ATTACK")));
		TestFalse(TEXT("...and the swing still owns the base pose"),
			Driver.Selection.bBasePoseOwned);

		// The air form is published the same way, which is what makes the airborne fork's own
		// self-latch reachable at all.
		FElysiumAnimationDriver Air;
		Air.Stem = TEXT("pc_body");
		Air.Source = EElysiumAnimSource::Player;
		Air.BodyKind = EElysiumAnimBodyKind::Player;
		MeleeMoveArm(Air, TEXT("ACT_MELEE_AIR_ATTACK"), TEXT("BaseballBat_air"));
		Air.BaseClipCycle.bPlaying = true;
		Air.BaseClipCycle.Cycle = 0.4f;
		Air.BaseClipCycle.HoldCycle = 0.91f;
		Air.Tick(Dt, MeleeMoveSample(0.0f), nullptr, nullptr);
		TestEqual(TEXT("an air swing publishes the air activity"),
			Air.Selection.RequestedActivity, FString(TEXT("ACT_MELEE_AIR_ATTACK")));
		TestTrue(TEXT("...and is driven for the whole clip, past the stated hold"),
			Air.bAnimationDriven);
	}

	// --- Phase 2's cancel, through the driver ------------------------------------------------------
	{
		// Standing still: the tail plays out, because the ladder answers a refused activity.
		FElysiumAnimationDriver Standing;
		Standing.Stem = TEXT("pc_body");
		Standing.Source = EElysiumAnimSource::Player;
		Standing.BodyKind = EElysiumAnimBodyKind::Player;
		MeleeMoveArm(Standing, TEXT("ACT_MELEE_ATTACK"), TEXT("baseballbat_attack_W1"));
		Standing.BaseClipCycle.bPlaying = true;
		Standing.BaseClipCycle.Cycle = 0.95f;
		Standing.BaseClipCycle.HoldCycle = 0.91f;
		Standing.Tick(Dt, MeleeMoveSample(0.0f), nullptr, nullptr);
		TestFalse(TEXT("past `w_hold` the body has its movement back"), Standing.bAnimationDriven);
		TestEqual(TEXT("...but a standing reselection is refused, so the swing stands"),
			Standing.Selection.RequestedActivity, FString(TEXT("ACT_MELEE_ATTACK")));
		TestNotNull(TEXT("...and the claim is still held"),
			Standing.ActiveRequest(EElysiumAnimChannel::Base));
		TestFalse(TEXT("...so the swing keeps the base pose"), Standing.Selection.bBasePoseOwned);

		// Moving: the ladder answers a gait, which IS applied and cuts the last of the clip.
		FElysiumAnimationDriver Walking;
		Walking.Stem = TEXT("pc_body");
		Walking.Source = EElysiumAnimSource::Player;
		Walking.BodyKind = EElysiumAnimBodyKind::Player;
		MeleeMoveArm(Walking, TEXT("ACT_MELEE_ATTACK"), TEXT("baseballbat_attack_W1"));
		Walking.BaseClipCycle.bPlaying = true;
		Walking.BaseClipCycle.Cycle = 0.95f;
		Walking.BaseClipCycle.HoldCycle = 0.91f;
		Walking.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		TestFalse(TEXT("a moving body past `w_hold` is not animation-driven either"),
			Walking.bAnimationDriven);
		TestNotEqual(TEXT("...and the reselection is applied, not refused"),
			Walking.Selection.RequestedActivity, FString(TEXT("ACT_MELEE_ATTACK")));
		TestNull(TEXT("...which gives the base channel back"),
			Walking.ActiveRequest(EElysiumAnimChannel::Base));
		TestTrue(TEXT("...so the gait takes the pose and the swing's tail is cut"),
			Walking.Selection.bBasePoseOwned);

		// Below `w_hold` the same moving body cannot cancel anything: the selection pass never runs.
		FElysiumAnimationDriver Locked;
		Locked.Stem = TEXT("pc_body");
		Locked.Source = EElysiumAnimSource::Player;
		Locked.BodyKind = EElysiumAnimBodyKind::Player;
		MeleeMoveArm(Locked, TEXT("ACT_MELEE_ATTACK"), TEXT("baseballbat_attack_W1"));
		Locked.BaseClipCycle.bPlaying = true;
		Locked.BaseClipCycle.Cycle = 0.5f;
		Locked.BaseClipCycle.HoldCycle = 0.91f;
		Locked.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		TestTrue(TEXT("below `w_hold` a moving body is animation-driven"), Locked.bAnimationDriven);
		TestNotNull(TEXT("...and nothing can take the channel by reselecting"),
			Locked.ActiveRequest(EElysiumAnimChannel::Base));
	}

	// --- A cast body takes none of it ---------------------------------------------------------------
	{
		FElysiumAnimationDriver Npc;
		Npc.Stem = TEXT("gangbanger_a");
		Npc.Source = EElysiumAnimSource::Npc;
		Npc.BodyKind = EElysiumAnimBodyKind::Cast;
		MeleeMoveArm(Npc, TEXT("ACT_MELEE_ATTACK"), TEXT("baseballbat_attack_W1"));
		Npc.BaseClipCycle.bPlaying = true;
		Npc.BaseClipCycle.Cycle = 0.2f;
		Npc.BaseClipCycle.HoldCycle = 0.91f;
		Npc.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		// `SetupMove` is `CPlayerMove`'s. The cast reaches the same authored records through a
		// completely separate seam (`CAI_BaseNPC::AutoMovement` -> `CAI_Motor`), so handing a cast
		// body this rule would also hand it the player's uninterruptibility, which retail does not.
		TestFalse(TEXT("a cast body is never animation-driven by this rule"), Npc.bAnimationDriven);
	}

	// --- The hinge: the forced ideal activity from the segment to the predicate --------------------
	//
	// `ElysiumAnimIntent::ClaimForSegment` is the only route by which a producer's forced activity
	// reaches the base claim, and the claim is the only route by which it reaches the predicate.
	// Delete either half and the clip still plays, the channel is still held, and the lock, the
	// reselection guard and the air self-latch all go quietly dead — so this drives the value across
	// both seams rather than asserting the assignment.
	{
		FElysiumClipSegment Segment;
		Segment.ClipName = TEXT("baseballbat_attack_W1");
		Segment.bLoop = false;
		Segment.Source = EElysiumAnimSource::Player;
		Segment.Priority = EElysiumAnimPriority::Scripted;
		Segment.Activity = TEXT("ACT_MELEE_ATTACK");

		const FElysiumAnimationRequest Claim =
			ElysiumAnimIntent::ClaimForSegment(Segment, /*PlayLengthSeconds=*/0.5f);
		TestEqual(TEXT("a one-shot segment claims for exactly its play"), Claim.HoldSeconds, 0.5f,
			Tolerance);

		// The hold is WALL-CLOCK, so a slowed play holds the channel longer. A claim taken from the
		// authored length instead would expire at cycle 0.70 of a rank-0 swing and release the
		// forced ideal activity — and the movement lock with it — a third of the clip early.
		FElysiumClipSegment Slowed = Segment;
		Slowed.PlaybackRate = 0.70f;
		TestEqual(TEXT("a swing at rank 0 holds the channel for its clip over the rate"),
			ElysiumAnimIntent::ClaimForSegment(Slowed, 0.5f).HoldSeconds, 0.5f / 0.7f, Tolerance);

		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;
		Driver.SubmitRequest(Claim);
		Driver.BaseClipCycle.bPlaying = true;
		Driver.BaseClipCycle.Cycle = 0.2f;
		Driver.BaseClipCycle.HoldCycle = 0.91f;
		Driver.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		TestEqual(TEXT("the segment's forced activity reaches the published ideal"),
			Driver.IdealActivity.Activity, FString(TEXT("ACT_MELEE_ATTACK")));
		TestTrue(TEXT("...and it is what makes the body animation-driven"), Driver.bAnimationDriven);
		TestTrue(TEXT("...and movement-locked with it"), Driver.bMovementLocked);
		TestTrue(TEXT("...and what the reselection guard refuses an idle against"),
			ElysiumClipMovement::RefusesReselection(Driver.IdealActivity, TEXT("ACT_IDLE")));

		// The same segment with no stated activity is the ordinary clip every non-action producer
		// arms: it holds the channel and claims to be nothing, so none of the three mechanisms fires.
		FElysiumClipSegment Plain = Segment;
		Plain.Activity.Reset();
		FElysiumAnimationDriver Quiet;
		Quiet.Stem = TEXT("male_pc");
		Quiet.Source = EElysiumAnimSource::Player;
		Quiet.BodyKind = EElysiumAnimBodyKind::Player;
		Quiet.SubmitRequest(ElysiumAnimIntent::ClaimForSegment(Plain, 0.5f));
		Quiet.BaseClipCycle = Driver.BaseClipCycle;
		Quiet.Tick(Dt, MeleeMoveSample(140.0f), nullptr, nullptr);
		TestFalse(TEXT("a segment claiming no activity drives nothing"), Quiet.bAnimationDriven);
		TestFalse(TEXT("...and locks no movement"), Quiet.bMovementLocked);
	}

	// --- The two arms of the predicate: `vt+0x670` against `vt+0x674` ------------------------------
	{
		using namespace ElysiumClipMovement;
		// `ACT_LAND_HARD` is `vt+0x674`'s one extra line. It matches no row of `AnimDrivenArmFor`, so
		// the movement lock holds over a landing while the jump refusal does not — which is the
		// entire divergence between the two virtuals.
		FElysiumIdealActivityState Landing;
		Landing.Activity = TEXT("ACT_LAND_HARD");
		Landing.bHasSequence = true;
		Landing.Cycle = 0.4f;
		TestFalse(TEXT("a hard landing refuses no jump"), IsAnimationDriven(Landing));
		TestTrue(TEXT("...but its movement is the animation's"), IsMovementLocked(Landing));

		// On every row this rung implements the two answers are the same, which is why the split is
		// structural rather than behavioural today.
		for (const TCHAR* const Melee : { TEXT("ACT_MELEE_ATTACK"), TEXT("ACT_MELEE_AIR_ATTACK"),
			TEXT("ACT_MELEE_ATTACK_2COMBO"), TEXT("ACT_MELEE_ATTACK_HEAVY") })
		{
			const FElysiumIdealActivityState Swing = MeleeMoveIdeal(Melee, 0.3f, 0.91f);
			TestTrue(FString::Printf(TEXT("%s answers both arms alike"), Melee),
				IsMovementLocked(Swing) == IsAnimationDriven(Swing));
		}
		const FElysiumIdealActivityState Walking = MeleeMoveIdeal(TEXT("ACT_WALK"), 0.3f, 1.0f);
		TestFalse(TEXT("an ordinary gait locks nothing"), IsMovementLocked(Walking));
	}

	// --- A window that does not advance substitutes a STOP, not an absence -------------------------
	{
		const FElysiumClipMovementPath W1 = MeleeMoveW1();
		FVector Delta(1.0, 2.0, 3.0);
		TestTrue(TEXT("a zero-width window still substitutes"),
			ElysiumClipMovement::SampleDelta(W1, 17, 0.4f, 0.4f, Delta));
		TestTrue(TEXT("...with a zero delta, which assigns a zero velocity"), Delta.IsNearlyZero());
		// `Studio_AnimMovement`'s only false is `nummovements == 0`: nothing refills the discarded
		// command and the body coasts down under ordinary friction instead.
		FElysiumClipMovementPath Empty;
		TestFalse(TEXT("a clip authoring no record does not substitute at all"),
			ElysiumClipMovement::SampleDelta(Empty, 17, 0.0f, 0.5f, Delta));
	}

	// --- A fan whose scale cannot produce a speed is not a fan -------------------------------------
	{
		FElysiumGaitSpeedTable Zeroed = MeleeMoveFan(MeleeMovePeakCmS);
		Zeroed.Scale = 0.0f;
		TestFalse(TEXT("a zero gait scale is not a usable fan"), Zeroed.IsValid());
		TestEqual(TEXT("...and would otherwise publish a zero ceiling"), Zeroed.Peak(), 0.0f);
		FElysiumGaitSpeedTable Negative = MeleeMoveFan(MeleeMovePeakCmS);
		Negative.Scale = -1.0f;
		TestFalse(TEXT("a negative gait scale is not one either"), Negative.IsValid());
	}

	// --- The sidecar's two absences, which a reader must not collapse ------------------------------
	{
		FElysiumBlendTable Stated;
		FString Error;
		const bool bParsed = Stated.LoadJsonText(TEXT(R"json({
			"stem": "character_shared_male_baseball",
			"pose_parameters": [],
			"movement_fields": ["end_frame","flags","v0_cm","v1_cm","yaw_deg",
			                    "dir_x","dir_y","dir_z","pos_x_cm","pos_y_cm","pos_z_cm"],
			"movement": {
				"baseballbat_attack_W1": [
					[2, 4160, 6.5593, 81.2413, 0.0, 1.0, 0.0, 0.0, 43.9003, 0.0, 0.0],
					[16, 4160, 30.3338, 9.5132, 0.0, 1.0, 0.0, 0.0, 418.7153, 0.0, 0.0]
				]
			}
		})json"), Error);
		TestTrue(FString::Printf(TEXT("a movement-only sidecar parses (%s)"), *Error), bParsed);
		TestTrue(TEXT("...and states that it was looked in"), Stated.bMovementStated);
		const FElysiumClipMovementPath* Path = Stated.FindMovement(TEXT("baseballbat_attack_W1"));
		if (TestNotNull(TEXT("...and carries the clip's path"), Path))
		{
			TestEqual(TEXT("...with its rows in file order"), Path->Records.Num(), 2);
			TestEqual(TEXT("...read verbatim, X forward"), Cm(Path->Records[0].PositionCm.X), 43.9003f,
				Tolerance);
			TestEqual(TEXT("...and Y not negated a second time"), Cm(Path->Records[0].PositionCm.Y), 0.0f,
				Tolerance);
			TestEqual(TEXT("...and the last frame is the clip's own"), Path->LastFrame(), 16);
		}
		TestNull(TEXT("a label the stated file omits authors no movement"),
			Stated.FindMovement(TEXT("baseballbat_attack_heavy_a")));

		// The OTHER absence: a file written before anything read the movement array. It says nothing
		// about any clip, and a consumer reports it rather than behaving as though the clip authored
		// no records.
		FElysiumBlendTable Silent;
		const bool bSilentParsed = Silent.LoadJsonText(TEXT(R"json({
			"stem": "old_export",
			"pose_parameters": [],
			"events": { "walk": [[0.5, 3047, 0, 0]] },
			"event_options": [""]
		})json"), Error);
		TestTrue(TEXT("a sidecar predating the column still parses"), bSilentParsed);
		TestFalse(TEXT("...and does not claim to have been looked in"), Silent.bMovementStated);
		TestNull(TEXT("...and carries no path for any label"), Silent.FindMovement(TEXT("walk")));
		TestFalse(TEXT("...and names no schema this reader could not address"),
			Silent.bMovementSchemaUnreadable);

		// The THIRD case, and the one that was being reported as the second: a file that states a
		// schema this reader cannot address. It loads — the grid beside it is perfectly good — so the
		// only thing that can report it is a flag the caller reads on the success path.
		FElysiumBlendTable Unreadable;
		const bool bUnreadableParsed = Unreadable.LoadJsonText(TEXT(R"json({
			"stem": "future_export",
			"pose_parameters": [],
			"movement_fields": ["end_frame","flags","v0_cm","v1_cm","yaw_deg","dir","pos"],
			"movement": { "baseballbat_attack_W1": [[2, 4160, 6.5593, 81.2413, 0.0, 1.0, 0.0]] },
			"events": { "walk": [[0.5, 3047, 0, 0]] },
			"event_options": [""]
		})json"), Error);
		TestTrue(TEXT("a sidecar this reader cannot address still loads its other blocks"),
			bUnreadableParsed);
		TestFalse(TEXT("...and does not claim the movement array was read"),
			Unreadable.bMovementStated);
		TestTrue(TEXT("...but says WHY, which is the opposite remedy from a stale export"),
			Unreadable.bMovementSchemaUnreadable);
	}

	return true;
}
