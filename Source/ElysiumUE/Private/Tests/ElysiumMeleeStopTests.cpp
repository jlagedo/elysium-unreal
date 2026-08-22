// Content-free Substrate automation: `CBasePlayer::PostThink`'s MELEE STOP, driven through the
// producer that performs it (LIFE5).
//
// The rule is a few instructions of retail — every frame from a swing's `w_hold` to the end of its
// clip, with no direction key held, the body's carried motion is discarded — but it only means
// anything as a path: the world's own button field reduced to the file's `IN_*` numbering, the
// forced-sequence record the embodiment reports, and the recovered predicate rebuilt over it.
// Asserting the predicate on values chosen by hand proves none of that, so almost nothing here
// calls it directly — every case runs a frame and reads what came out of the service seam.
//
// The facts asserted come from `CBasePlayer::PostThink` itself; no `docs/vtmb/` section owns this
// block yet, and `player-entity.md`'s recovered `PostThink` listing skips over it:
//  * the release is decided by the PLAYING SEQUENCE's own `w_hold`, not by a constant;
//  * a held direction refuses the stop, and "direction" is exactly the seven bits of `0x79a` —
//    jump counts, the attack button does not;
//  * only `ACT_MELEE_ATTACK` reaches it: the whole-clip melee rows are outside the recovered
//    `CMP ..., 0x4b`, and so is a swing whose ideal activity a reaction overwrote;
//  * it is a per-frame WINDOW and not an edge: the recovered block carries no latch, so every tail
//    frame stops the body again, which is what makes the window self-sustaining.
//
// The cycle values are the shipped katana's own (`katana_combo_D1`, `w_hold` 0.910) beside the
// unauthored 1.000 that 10,597 of the install's 14,012 descriptors state. Transcribed rather than
// loaded: the suite opens no export, builds no body and needs no baked clip.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClipMovement.h"
#include "ElysiumComboChain.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumUserCmd.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumLocomotionSample.h"
#include "Visual/ElysiumActionTables.h"

namespace ElysiumMeleeStopTests
{
static constexpr EAutomationTestFlags GElysiumMeleeStopTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// File-prefixed for the non-unity build, like every other helper block in this module.
	constexpr uint64 GStopAtk   = static_cast<uint64>(EElysiumButton::Attack);
	constexpr uint64 GStopFwd   = static_cast<uint64>(EElysiumButton::Forward);
	constexpr uint64 GStopBack  = static_cast<uint64>(EElysiumButton::Back);
	constexpr uint64 GStopMoveL = static_cast<uint64>(EElysiumButton::MoveLeft);
	constexpr uint64 GStopJump  = static_cast<uint64>(EElysiumButton::Jump);
	constexpr uint64 GStopDuck  = static_cast<uint64>(EElysiumButton::Duck);
	constexpr uint64 GStopTurnL = static_cast<uint64>(EElysiumButton::Left);
	constexpr uint64 GStopTurnR = static_cast<uint64>(EElysiumButton::Right);

	const TCHAR* const GStopMelee = TEXT("ACT_MELEE_ATTACK");
	const TCHAR* const GStopHeavy = TEXT("ACT_MELEE_ATTACK_HEAVY");

	// `katana_combo_D1`'s own release cycle. The three links of the katana's neutral entry chain all
	// state it, and it is what makes the swing's last ~9% a window at all.
	constexpr float GStopHold = 0.910f;

	// A swing standing mid-tail: past its `w_hold`, still inside its clip. The reselection guard
	// keys on the ideal activity and the unfinished sequence, not on the lock.
	FElysiumIdealActivityState MeleeSwingIdeal()
	{
		FElysiumIdealActivityState State;
		State.Activity = GStopMelee;
		State.bHasSequence = true;
		State.Cycle = 0.950f;
		State.HoldCycle = GStopHold;
		return State;
	}

	FElysiumEntityDefs MakeStopDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__melee_stop_test__");
		return Defs;
	}

	// A live player in an activated world, with the recording embodiment behind it. No weapon, no
	// body and no item table: the stop reads the button field and the forced-sequence record, and
	// nothing else in the substrate is on its path.
	struct FStopFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;

		bool Stand(FAutomationTestBase& Test)
		{
			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeStopDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);
			Player = World->FindPlayer();
			if (!Test.TestNotNull(TEXT("the player exists"), Player))
			{
				return false;
			}
			Services.Calls.Reset();
			Services.StopPlayerBodyCount = 0;
			return true;
		}

		// Where the base channel's forced sequence stands, as the map actor reads it off the driver
		// and hands it down. The predicate is rebuilt from this every frame, so a case advances a
		// swing by moving the cycle rather than by setting a flag.
		FElysiumIdealActivityState Standing;

		void PlaceSwing(const TCHAR* Activity, float Cycle, float HoldCycle = GStopHold)
		{
			Standing = FElysiumIdealActivityState();
			Standing.Activity = Activity;
			Standing.bHasSequence = true;
			Standing.Cycle = Cycle;
			Standing.HoldCycle = HoldCycle;
		}

		// A body standing on nothing: no claim forces an activity, which is every frame outside an
		// action and also what a swing interrupted by a reaction leaves behind. It is also what a
		// headless world hands down, so the two cases are one.
		void PlaceNothing()
		{
			Standing = FElysiumIdealActivityState();
		}

		// One post-move pass, at the point the map actor runs it: the button field is published, then
		// the stop is asked, then the selector would run.
		// The stop reads no clock — the predicate is a pure function of the frame's record and its
		// buttons — so a case is a sequence of placements and presses, not a timeline.
		void Frame(uint64 Buttons)
		{
			World->SetPlayerButtons(Buttons);
			World->UpdatePlayerMeleeMovementStop(Standing);
		}

		int32 Stops() const { return Services.StopPlayerBodyCount; }
	};
}   // namespace

// =====================================================================================
// The tail window, end to end
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementStopWindowTest,
	"Elysium.Substrate.MeleeMovementStop.Window", GElysiumMeleeStopTestFlags)
bool FElysiumMeleeMovementStopWindowTest::RunTest(const FString&)
{
	FStopFixture F;
	if (!F.Stand(*this))
	{
		return false;
	}

	// --- While the lock HOLDS, nothing is stopped --------------------------------------------------
	//
	// Three frames below the sequence's own `w_hold`, including the frame immediately under it. The
	// body is being carried by the clip on all three, and taking its motion away there would kill
	// the lunge the swing exists to produce.
	F.PlaceSwing(GStopMelee, 0.10f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("the first locked frame stops nothing"), F.Stops(), 0);

	F.PlaceSwing(GStopMelee, 0.50f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("mid-swing stops nothing"), F.Stops(), 0);

	F.PlaceSwing(GStopMelee, 0.900f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("the frame under `w_hold` still stops nothing"), F.Stops(), 0);

	// --- The first tail frame ----------------------------------------------------------------------
	//
	// The cycle crosses the sequence's authored `w_hold` and the predicate goes false. `+attack` is
	// still held, and it is invisible here: the recovered mask is the seven DIRECTION bits, so a
	// player pressing nothing but the attack button is a player holding no direction.
	F.PlaceSwing(GStopMelee, 0.920f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("the first tail frame stops the body"), F.Stops(), 1);
	TestTrue(TEXT("...through the embodiment, not by reaching into a mover"),
		F.Services.Saw(TEXT("StopPlayerBody")));

	// --- ...and EVERY tail frame after it ----------------------------------------------------------
	//
	// The recovered block keeps no latch, so it re-runs the whole way to the clip's end. That is what
	// makes the window self-sustaining: a stopped body classifies as idle, the reselection guard
	// refuses that idle, the ideal activity stays `ACT_MELEE_ATTACK`, and the next frame arrives here
	// again. A one-shot would let anything that pushed the body mid-tail keep the motion.
	F.PlaceSwing(GStopMelee, 0.950f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("the next tail frame stops it again"), F.Stops(), 2);

	F.PlaceSwing(GStopMelee, 0.990f);
	F.Frame(0);
	TestEqual(TEXT("...and so does one with every button released"), F.Stops(), 3);

	// The melee rows carry a `cycle < 1.0` guard above the `w_hold` comparison, so the clip's own end
	// is still inside the window rather than past it.
	F.PlaceSwing(GStopMelee, 1.000f);
	F.Frame(0);
	TestEqual(TEXT("...and the clip reaching its end is still a tail frame"), F.Stops(), 4);

	// --- A fresh swing closes the window and opens a new one ---------------------------------------
	F.PlaceSwing(GStopMelee, 0.200f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("a fresh swing locks the body again and stops nothing"), F.Stops(), 4);

	F.PlaceSwing(GStopMelee, 0.930f);
	F.Frame(GStopAtk);
	TestEqual(TEXT("...and its own tail stops the body once more"), F.Stops(), 5);

	return true;
}

// =====================================================================================
// The held-direction refusal, and what counts as a direction
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementStopDirectionTest,
	"Elysium.Substrate.MeleeMovementStop.Direction", GElysiumMeleeStopTestFlags)
bool FElysiumMeleeMovementStopDirectionTest::RunTest(const FString&)
{
	// Every bit of retail's `0x79a`, one release each, plus the two nearest buttons that are NOT in
	// it. The refusal is what keeps a player who is still walking out of the swing's tail — retail
	// hands that case to the gait ladder deliberately — so a mask that were merely "any button held"
	// would stop a body the player is steering.
	struct FCase
	{
		uint64 Buttons;
		bool bExpectStop;
		const TCHAR* Why;
	};
	const FCase Cases[] =
	{
		{ 0,                        true,  TEXT("nothing held") },
		{ GStopAtk,                 true,  TEXT("only the attack button") },
		{ GStopDuck,                true,  TEXT("only duck, which is outside 0x79a") },
		{ GStopFwd,                 false, TEXT("forward held") },
		{ GStopBack,                false, TEXT("back held") },
		{ GStopMoveL,               false, TEXT("strafe held") },
		{ GStopJump,                false, TEXT("jump held, which IS one of the seven bits") },
		{ GStopTurnL,               false, TEXT("the TURN key, which is a direction bit too") },
		{ GStopTurnR,               false, TEXT("...and its opposite") },
		{ GStopAtk | GStopFwd,      false, TEXT("attacking while walking forward") },
	};

	for (const FCase& Case : Cases)
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f);
		F.Frame(Case.Buttons);
		F.PlaceSwing(GStopMelee, 0.930f);
		F.Frame(Case.Buttons);
		TestEqual(FString::Printf(TEXT("release with %s"), Case.Why), F.Stops(),
			Case.bExpectStop ? 1 : 0);
	}

	// The refusal is decided FRESH each frame, not spent. A player who was walking as the lock
	// released and lets go a frame later is, on that frame, a player in a swing's tail holding no
	// direction — so the stop applies then, exactly as the recovered block re-tests it.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f);
		F.Frame(GStopFwd);
		F.PlaceSwing(GStopMelee, 0.930f);
		F.Frame(GStopFwd);
		TestEqual(TEXT("a release under a held direction stops nothing"), F.Stops(), 0);

		F.PlaceSwing(GStopMelee, 0.960f);
		F.Frame(0);
		TestEqual(TEXT("...and letting go a frame later stops it on that frame"), F.Stops(), 1);
	}

	return true;
}

// =====================================================================================
// Which swings reach it at all, and what decides the release
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementStopScopeTest,
	"Elysium.Substrate.MeleeMovementStop.Scope", GElysiumMeleeStopTestFlags)
bool FElysiumMeleeMovementStopScopeTest::RunTest(const FString&)
{
	// --- The release cycle is the SEQUENCE's, not a constant ---------------------------------------
	//
	// Two swings, the same frames, different authored `w_hold`. 10,597 of the shipped descriptors
	// state exactly 1.000 — a heavy finisher locks for its whole clip — and a stop keyed on anything
	// but the file's own value would fire on both alike.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		F.PlaceSwing(GStopMelee, 0.930f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		TestEqual(TEXT("a swing authoring no combo block is still locked at cycle 0.93"),
			F.Stops(), 0);

		F.PlaceSwing(GStopMelee, 0.999f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		TestEqual(TEXT("...and at 0.999"), F.Stops(), 0);

		// The clip's own end is where that swing's lock finally goes: the melee rows carry a
		// `m_flCycle < 1.0` guard of their own, above the `w_hold` comparison.
		F.PlaceSwing(GStopMelee, 1.000f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		TestEqual(TEXT("...and the clip ending is its release"), F.Stops(), 1);
	}

	// --- Only `ACT_MELEE_ATTACK` -------------------------------------------------------------------
	//
	// The recovered check is `CMP dword ptr [player + 0xff0], 0x4b`. The heavy row is driven for its
	// whole clip and its lock releases at the end of it — but it is not activity 75, so the stop is
	// not its.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopHeavy, 0.500f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		F.PlaceSwing(GStopHeavy, 1.000f, /*HoldCycle*/ 1.000f);
		F.Frame(0);
		TestEqual(TEXT("a heavy swing's release is not activity 75 and stops nothing"),
			F.Stops(), 0);
	}

	// --- A swing a reaction took over ---------------------------------------------------------------
	//
	// The forced activity is what the base-channel claim says, and a claim that has been replaced or
	// given back says nothing. The lock goes in the same frame, and it goes without a stop: the ideal
	// activity is no longer the swing's, exactly as retail's comparison finds.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f);
		F.Frame(0);
		F.PlaceNothing();
		F.Frame(0);
		TestEqual(TEXT("a swing whose claim was taken away stops nothing"), F.Stops(), 0);
	}

	// --- A world with no forced-sequence record -----------------------------------------------------
	//
	// A backdrop map, an animation pass that has never run, or a headless world: all of them hand
	// down a cleared record, and a cleared record names no activity.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f);
		F.Frame(0);
		F.PlaceNothing();
		F.Frame(0);
		TestEqual(TEXT("a cleared record stops nothing"), F.Stops(), 0);

		// The window is a property of the frame, not of a remembered swing: once a swing is standing
		// again inside its tail, the stop resumes.
		F.PlaceSwing(GStopMelee, 0.930f);
		F.Frame(0);
		TestEqual(TEXT("...and it resumes the moment a swing stands again"), F.Stops(), 1);
	}

	// --- A player whose `PostThink` is not running ---------------------------------------------------
	//
	// Retail skips the whole live `PostThink` body — and this block with it — while the player is
	// locked. A cutscene taking the body mid-swing therefore ends the swing's release rather than
	// banking it for whenever control comes back.
	{
		FStopFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.PlaceSwing(GStopMelee, 0.500f);
		F.Frame(0);
		F.Player->SetImmobilized(true);
		F.PlaceSwing(GStopMelee, 0.930f);
		F.Frame(0);
		TestEqual(TEXT("an immobilized player's whole PostThink is skipped, this block with it"),
			F.Stops(), 0);

		F.Player->SetImmobilized(false);
		F.PlaceSwing(GStopMelee, 0.960f);
		F.Frame(0);
		TestEqual(TEXT("...and control returning inside the tail resumes it"), F.Stops(), 1);
	}

	return true;
}

// =====================================================================================
// The rule itself, at the boundaries the frames above cannot land exactly on
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementStopRuleTest,
	"Elysium.Substrate.MeleeMovementStop.Rule", GElysiumMeleeStopTestFlags)
bool FElysiumMeleeMovementStopRuleTest::RunTest(const FString&)
{
	using namespace ElysiumClipMovement;

	auto Swing = [](float Cycle, float Hold) -> FElysiumIdealActivityState
	{
		FElysiumIdealActivityState State;
		State.Activity = GStopMelee;
		State.bHasSequence = true;
		State.Cycle = Cycle;
		State.HoldCycle = Hold;
		return State;
	};

	// The comparison is strict on the LOCK side (`m_flCycle < w_hold`), so the cycle standing exactly
	// on `w_hold` is already inside the tail. One frame either side of the katana's own 0.910.
	TestTrue(TEXT("the cycle exactly at `w_hold` is a tail frame"),
		StopsMeleeTailMotion(Swing(GStopHold, GStopHold), 0));
	TestFalse(TEXT("...and the cycle just under it is not"),
		StopsMeleeTailMotion(Swing(0.909f, GStopHold), 0));

	// No history is consulted. The predicate is a pure function of this frame's record and this
	// frame's buttons, which is what makes it a window rather than an edge.
	TestTrue(TEXT("a released cycle stops whether or not a lock preceded it"),
		StopsMeleeTailMotion(Swing(0.950f, GStopHold), 0));

	// The mask is consulted whole. `ElysiumCombo::SelectionMask` is the file's own numbering, so a
	// bit outside it cannot refuse the stop however high it is set.
	TestFalse(TEXT("any of the seven direction bits refuses"),
		StopsMeleeTailMotion(Swing(0.950f, GStopHold), ElysiumCombo::InMoveRight));
	TestTrue(TEXT("...and everything outside them is invisible"),
		StopsMeleeTailMotion(Swing(0.950f, GStopHold), ~ElysiumCombo::SelectionMask));

	// A body with no sequence standing on the channel is not swinging at all, whatever its claim is
	// labelled. This is the arming frame of every swing and of every chain link — the claim is
	// published a pass before the montage reaches the channel — so a rule that read the claim alone
	// would stop the body on the frame its lunge is supposed to start.
	FElysiumIdealActivityState Bodiless = Swing(0.500f, GStopHold);
	Bodiless.bHasSequence = false;
	TestFalse(TEXT("a claim with no clip under it is not a tail frame"),
		StopsMeleeTailMotion(Bodiless, 0));
	TestFalse(TEXT("...and it never held the lock either"), IsAnimationDriven(Bodiless));

	// The same body PAST the hold cycle. Without the guard this is the reachable misfire: the
	// activity matches, `IsAnimationDriven` answers false for want of a sequence rather than for
	// want of a lock, and the stop fires every frame the claim stands.
	FElysiumIdealActivityState BodilessPastHold = Swing(0.950f, GStopHold);
	BodilessPastHold.bHasSequence = false;
	TestFalse(TEXT("a claim with no clip is not a tail frame past the hold either"),
		StopsMeleeTailMotion(BodilessPastHold, 0));

	return true;
}

// =====================================================================================
// The payoff: what stopping the body actually buys the swing
// =====================================================================================
//
// Every suite above proves the DECISION — that the stop is asked for on the right frames. This one
// proves it is worth asking: it walks the real gait ladder with the two states the stop sits
// between, and hands each answer to the real reselection guard. Together they are the whole
// mechanism, and the defect the stop exists to fix is the second row of the table below.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeMovementStopPayoffTest,
	"Elysium.Substrate.MeleeMovementStop.Payoff", GElysiumMeleeStopTestFlags)
bool FElysiumMeleeMovementStopPayoffTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	// A body mid-lunge, in the shape `PublishLocomotionSample` writes: real speed along the body's
	// own forward, and the two yaw terms derived from it. This is what a swing leaves on the record
	// when its clip has been driving the mover.
	FElysiumLocomotionSample Lunging;
	Lunging.LocalVelocity = FVector(185.0, 0.0, 0.0);
	Lunging.MoveYawVelocity = 0.0f;
	Lunging.MoveYawPose = 0.0f;
	Lunging.bOnGround = true;

	// The same sample after the stop's own rule — the function `StopBody` calls, asserted here
	// rather than assumed.
	FElysiumLocomotionSample Stopped = Lunging;
	ElysiumLocomotion::ClearMotion(Stopped);
	TestTrue(TEXT("clearing the sample's motion leaves it standing"),
		FMath::IsNearlyZero(Stopped.Speed2D()));
	TestTrue(TEXT("...and a lunging one is not"), Lunging.Speed2D() > 1.0f);
	TestTrue(TEXT("...while the command it carries is left alone"),
		Stopped.bOnGround == Lunging.bOnGround);

	// The ladder read off a REAL sample rather than a hand-written predicate: the only knob is which
	// of the two samples above the query is answering from.
	auto LadderAnswer = [](const FElysiumLocomotionSample& Sample) -> FString
	{
		const FPlayerRule* Row = SelectRule(PlayerGaitLadder(),
			[&Sample](EPlayerPredicate Predicate, int32) -> bool
			{
				switch (Predicate)
				{
				case EPlayerPredicate::Always:             return true;
				case EPlayerPredicate::BelowMoveThreshold: return Sample.Speed2D() <= 1.0f;
				case EPlayerPredicate::AboveGaitThreshold: return Sample.Speed2D() > 1.0f;
				default:                                   return false;
				}
			});
		// The ladder's last row is unconditional, so a null here is a broken table rather than a
		// legitimate "no answer".
		return (Row != nullptr && Row->Activity != nullptr) ? FString(Row->Activity) : FString();
	};

	const FString Standing = LadderAnswer(Stopped);
	const FString Moving = LadderAnswer(Lunging);

	TestFalse(TEXT("the ladder answers a standing body"), Standing.IsEmpty());
	TestFalse(TEXT("...and a moving one"), Moving.IsEmpty());
	TestNotEqual(TEXT("...and they are not the same answer"), Standing, Moving);

	// The stop's whole purpose: the answer a STOPPED body draws is one the guard refuses, so the
	// swing keeps its tail. `RefusesReselection` is the same function the driver calls.
	TestTrue(TEXT("a stopped body's ladder answer is refused, so the swing's tail survives"),
		ElysiumClipMovement::RefusesReselection(MeleeSwingIdeal(), Standing));

	// And the defect, stated as a passing assertion: a MOVING body draws an answer the guard lets
	// through, which applies over the swing and cuts its tail. This is faithful — retail's selector
	// refuses only `ACT_IDLE` and `ACT_AIM` — and it is exactly why the carried lunge must not still
	// be on the body when the ladder runs.
	TestFalse(TEXT("a moving body's answer is NOT refused, which is what truncates a swing"),
		ElysiumClipMovement::RefusesReselection(MeleeSwingIdeal(), Moving));

	// The guard is keyed on the swing still being unfinished; a finished one refuses nothing, so the
	// stop cannot pin a body past the clip it belongs to.
	FElysiumIdealActivityState Finished = MeleeSwingIdeal();
	Finished.Cycle = 1.0f;
	TestFalse(TEXT("a finished swing refuses nothing"),
		ElysiumClipMovement::RefusesReselection(Finished, Standing));

	return true;
}

}   // namespace ElysiumMeleeStopTests

#endif   // WITH_DEV_AUTOMATION_TESTS
