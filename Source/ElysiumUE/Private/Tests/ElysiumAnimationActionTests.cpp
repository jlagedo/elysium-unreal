#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"               // what a map stands each cast body as
#include "ElysiumGaitSpeeds.h"               // the per-direction speed table (CCC7)
#include "ElysiumMoveSolve.h"                // the sv_*scale constants and the unit factor
#include "ElysiumEntityWorld.h"              // the release-on-scene-stop fixture drives a real scene
#include "ElysiumVariant.h"
#include "Substrate/ElysiumSceneData.h"      // ElysiumScene::RegisterInline / ClearCache
#include "Tests/ElysiumTestServices.h"       // FElysiumRecordingServices — the embodiment witness
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimationDriver.h"
#include "Visual/ElysiumAnimationResolve.h"

#include "AlphaBlend.h"                      // the transition curve S2 pins the blend stack to
#include "Animation/AnimSequence.h"          // a bare sequence stands in for what a resolve loads
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// CCC4 — the intent, the resolver and the selection record.
//
// The whole rung is asserted here because the whole rung is content-free: the classifier reads the
// body sample, the resolver reads a clip vocabulary and a blend table, and neither needs a world, a
// mesh or a file. What the Content tier below adds is the one thing a fixture cannot prove — that
// the *exported* corpus agrees with the recovered bank ownership, so the player and the cast really
// do resolve the same label to different banks.

static constexpr EAutomationTestFlags GElysiumAnimationTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A grounded, standing body moving straight ahead at `Speed` cm/s.
	FElysiumLocomotionSample Moving(float Speed)
	{
		FElysiumLocomotionSample S;
		S.bOnGround = true;
		S.LocalVelocity = FVector(Speed, 0.0f, 0.0f);
		S.WishScale = Speed > 0.0f ? 1.0f : 0.0f;
		return S;
	}

	// Advance the latch one frame and classify what came out, which is the order the driver runs in.
	EElysiumAnimActivityCode Step(FElysiumJumpLatch& Latch, const FElysiumLocomotionSample& Sample,
		const FElysiumGaitReference& Gait, float DeltaSeconds = 1.0f / 60.0f)
	{
		Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Sample, DeltaSeconds, Gait);
		return ElysiumAnimIntent::Classify(Sample, Latch, Gait);
	}

	int32 AsInt(EElysiumAnimActivityCode Code) { return static_cast<int32>(Code); }
	int32 AsInt(EElysiumAirPhase Phase) { return static_cast<int32>(Phase); }
}

// =====================================================================================
// Step 2 and step 3 — the classifier, the jump latch and the translation pass. Every threshold here
// is a fraction of an injected speed authority rather than a number, because `CCC7` may halve the
// gait; the assertion that says so runs the identical sample against two references and requires two
// different answers.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationIntentTest,
	"Elysium.Substrate.AnimationIntent", GElysiumAnimationTestFlags)
bool FElysiumAnimationIntentTest::RunTest(const FString&)
{
	using namespace ElysiumAnimIntent;

	const FElysiumGaitReference Gait;

	// --- The grounded gait ----------------------------------------------------------------------
	{
		FElysiumJumpLatch Latch;
		TestEqual(TEXT("a still standing body idles"),
			AsInt(Step(Latch, Moving(0.0f), Gait)), AsInt(EElysiumAnimActivityCode::Idle));

		// A body asking for nothing is standing still, and the wish yaw at rest is a placeholder
		// rather than a measurement. Reading it would report "walking forward" out of a body at rest.
		FElysiumLocomotionSample Placeholder = Moving(0.0f);
		Placeholder.MoveYawWish = 90.0f;
		Placeholder.WishScale = 0.0f;
		FElysiumJumpLatch Fresh;
		TestEqual(TEXT("a zero wish scale does not read as walking"),
			AsInt(Step(Fresh, Placeholder, Gait)), AsInt(EElysiumAnimActivityCode::Idle));
	}
	{
		// **The relaxed forms are what the classifier emits.** Retail's own trace shows walk and run
		// entering translation as the relaxed variants, and a player body carries no sequence for
		// either — which is exactly what makes the translation pass load-bearing.
		FElysiumJumpLatch Latch;
		TestEqual(TEXT("just past the still threshold is the relaxed walk, not ACT_WALK"),
			AsInt(Step(Latch, Moving(Gait.StillSpeed() + 1.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));

		TestEqual(TEXT("well past the split is the relaxed run"),
			AsInt(Step(Latch, Moving(Gait.RunSpeedCmPerSecond), Gait)),
			AsInt(EElysiumAnimActivityCode::RunRelaxed));
	}

	// --- The CCC7 assertion: no absolute speed is baked in ---------------------------------------
	{
		// The identical sample, classified against two speed authorities. If any threshold in the
		// classifier were a number rather than a fraction, both would answer the same.
		const FElysiumLocomotionSample Same = Moving(500.0f);

		FElysiumJumpLatch A;
		const EElysiumAnimActivityCode Shipped = Step(A, Same, Gait);

		FElysiumGaitReference Doubled = Gait;
		Doubled.WalkSpeedCmPerSecond = Gait.WalkSpeedCmPerSecond * 2.0f;
		Doubled.RunSpeedCmPerSecond = Gait.RunSpeedCmPerSecond * 2.0f;
		FElysiumJumpLatch B;
		const EElysiumAnimActivityCode Halved = Step(B, Same, Doubled);

		TestEqual(TEXT("500 cm/s runs against the shipped authority"),
			AsInt(Shipped), AsInt(EElysiumAnimActivityCode::RunRelaxed));
		TestEqual(TEXT("and the same 500 cm/s walks once the authority doubles"),
			AsInt(Halved), AsInt(EElysiumAnimActivityCode::WalkRelaxed));
	}

	// --- The gait split holds no memory, which is retail's behaviour --------------------------------
	{
		// Retail recomputes every operand each call and holds nothing; the shipped margin is therefore
		// zero, and a body that drops below the split walks on the very next frame.
		FElysiumJumpLatch Latch;
		Step(Latch, Moving(Gait.RunSpeedCmPerSecond), Gait);
		TestTrue(TEXT("the run is latched"), Latch.bLastGaitWasRun);

		const float Split = Gait.RunSplitSpeed();
		TestEqual(TEXT("the shipped margin is zero"), Gait.HysteresisSpeed(), 0.0f);
		TestEqual(TEXT("so a hair below the split walks immediately"),
			AsInt(Step(Latch, Moving(Split - 1.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
		TestEqual(TEXT("and a hair above it runs again, with no band to cross"),
			AsInt(Step(Latch, Moving(Split + 1.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::RunRelaxed));
	}

	// --- The margin still works when it is asked for -------------------------------------------------
	{
		// It survives as a dial rather than being deleted, because the claim that the commanded term
		// below removed the need for it should be falsifiable rather than assumed.
		FElysiumGaitReference Damped = Gait;
		Damped.HysteresisFraction = 0.10f;

		FElysiumJumpLatch Latch;
		Step(Latch, Moving(Damped.RunSpeedCmPerSecond), Damped);
		const float Split = Damped.RunSplitSpeed();
		const float Margin = Damped.HysteresisSpeed();
		TestTrue(TEXT("a margin that was asked for is non-zero"), Margin > 0.0f);

		TestEqual(TEXT("below the split but inside the margin, the run holds"),
			AsInt(Step(Latch, Moving(Split - Margin * 0.5f), Damped)),
			AsInt(EElysiumAnimActivityCode::RunRelaxed));
		TestEqual(TEXT("past the margin it walks, and only then"),
			AsInt(Step(Latch, Moving(Split - Margin * 2.0f), Damped)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
		TestEqual(TEXT("and it stays walking as it slows further"),
			AsInt(Step(Latch, Moving(Split - Margin * 3.0f), Damped)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
	}

	// --- The commanded term: retail snaps to the run on the first frame of full input ----------------
	{
		// `speed2D > T || cmdMoveMag > T`. A body just off the mark is barely moving while already
		// commanding the full gait, and retail selects the run there — a realized-speed-only test
		// ramps into it instead, which is a visible walk-then-run at the start of every sprint.
		//
		// The disjunction is in the **split** and not in the still test: retail's idle/moving cut
		// reads realized speed alone, so a body at a standstill is idle however hard it is being
		// pushed. That is why this sample is moving at all.
		FElysiumJumpLatch Latch;
		FElysiumLocomotionSample Starting = Moving(Gait.StillSpeed() + 1.0f);
		Starting.CommandedSpeed = Gait.RunSplitSpeed() + 1.0f;
		TestEqual(TEXT("a body just off the mark at full command runs on frame one"),
			AsInt(Step(Latch, Starting, Gait)), AsInt(EElysiumAnimActivityCode::RunRelaxed));

		// The same frame without the command is the ramp retail does not have.
		FElysiumJumpLatch Slow;
		TestEqual(TEXT("...and walks without it, which is the ramp the term removes"),
			AsInt(Step(Slow, Moving(Gait.StillSpeed() + 1.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));

		// And the realized term is what still decides once the command is released.
		FElysiumLocomotionSample Coasting = Moving(Gait.RunSplitSpeed() + 1.0f);
		Coasting.CommandedSpeed = 0.0f;
		TestEqual(TEXT("a coasting body is judged on the speed it actually has"),
			AsInt(Step(Latch, Coasting, Gait)), AsInt(EElysiumAnimActivityCode::RunRelaxed));

		// Neither term reaching the split is a walk, however the other is spelled.
		FElysiumLocomotionSample Strolling = Moving(Gait.StillSpeed() + 1.0f);
		Strolling.CommandedSpeed = Gait.StillSpeed() + 1.0f;
		TestEqual(TEXT("and neither term past the split is a walk"),
			AsInt(Step(Latch, Strolling, Gait)), AsInt(EElysiumAnimActivityCode::WalkRelaxed));
	}

	// --- The stance, which is four values and one branch --------------------------------------------
	{
		// The two ramps are the graph's concern. A body held in `Rising` under a low ceiling is as
		// ducked as one that settled, and reporting it as standing would stand the body up in the
		// pose while the hull stayed low.
		const EElysiumStance Ducked[] =
			{ EElysiumStance::Lowering, EElysiumStance::Ducked, EElysiumStance::Rising };
		for (EElysiumStance Stance : Ducked)
		{
			FElysiumJumpLatch Still;
			FElysiumLocomotionSample S = Moving(0.0f);
			S.Stance = Stance;
			TestEqual(TEXT("every non-standing stance crouches when still"),
				AsInt(Step(Still, S, Gait)), AsInt(EElysiumAnimActivityCode::Crouch));

			FElysiumJumpLatch Walking;
			FElysiumLocomotionSample M = Moving(Gait.WalkSpeedCmPerSecond / 3.0f);
			M.Stance = Stance;
			TestEqual(TEXT("and sneaks when moving"),
				AsInt(Step(Walking, M, Gait)), AsInt(EElysiumAnimActivityCode::Sneak));
		}
	}

	// --- The jump latch, one assertion per transition ------------------------------------------------
	{
		// The ordinary jump: the press edge selects ACT_LEAP while the body is still nominally
		// grounded, exactly as the controlled corpus records it.
		FElysiumJumpLatch Latch;
		FElysiumLocomotionSample Press = Moving(0.0f);
		Press.JumpHoldRemaining = 0.2f;
		TestEqual(TEXT("the press edge leaps, before the body has left the ground"),
			AsInt(Step(Latch, Press, Gait)), AsInt(EElysiumAnimActivityCode::Leap));

		FElysiumLocomotionSample Rising = Moving(0.0f);
		Rising.bOnGround = false;
		Rising.LocalVelocity.Z = 400.0f;
		Rising.JumpHoldRemaining = 0.1f;
		TestEqual(TEXT("and holds the leap while the push is still working"),
			AsInt(Step(Latch, Rising, Gait)), AsInt(EElysiumAnimActivityCode::Leap));

		FElysiumLocomotionSample Apex = Moving(0.0f);
		Apex.bOnGround = false;
		Apex.LocalVelocity.Z = -10.0f;
		TestEqual(TEXT("the window closes and the sign turns, so it falls"),
			AsInt(Step(Latch, Apex, Gait)), AsInt(EElysiumAnimActivityCode::Falling));

		TestEqual(TEXT("it does not select the authored ascend or descend clips"),
			AsInt(Latch.Phase), AsInt(EElysiumAirPhase::Falling));

		TestEqual(TEXT("touching down while still lands"),
			AsInt(Step(Latch, Moving(0.0f), Gait)), AsInt(EElysiumAnimActivityCode::Land));

		// The land is not held forever: something has to leave it, and until the graph can report a
		// finished one-shot that something is the provisional hold.
		FElysiumJumpLatch Held = Latch;
		Held.PhaseSeconds = Held.LandHoldSeconds;
		Step(Held, Moving(0.0f), Gait);
		TestEqual(TEXT("and the hold expires back to the gait"),
			AsInt(Held.Phase), AsInt(EElysiumAirPhase::Grounded));
	}
	{
		// **Walking off a ledge is not a jump.** The two are identical in the sample's own state,
		// which is the gap the latch exists to close.
		FElysiumJumpLatch Latch;
		Step(Latch, Moving(Gait.WalkSpeedCmPerSecond), Gait);

		FElysiumLocomotionSample Off = Moving(Gait.WalkSpeedCmPerSecond);
		Off.bOnGround = false;
		Off.LocalVelocity.Z = -20.0f;
		TestEqual(TEXT("leaving the ground with no press falls rather than leaping"),
			AsInt(Step(Latch, Off, Gait)), AsInt(EElysiumAnimActivityCode::Falling));
	}
	{
		// Every NPC reaches this path: their movement component's jump is an impulse, so the hold
		// window is permanently zero and the press edge never fires. They must still reach falling
		// and landing from the ground transitions alone.
		FElysiumJumpLatch Latch;
		FElysiumLocomotionSample Air = Moving(0.0f);
		Air.bOnGround = false;
		Air.LocalVelocity.Z = 500.0f;
		Air.JumpHoldRemaining = 0.0f;

		Step(Latch, Moving(0.0f), Gait);
		TestEqual(TEXT("a body with no hold window never leaps"),
			AsInt(Step(Latch, Air, Gait)), AsInt(EElysiumAnimActivityCode::Falling));
		TestEqual(TEXT("but it still lands"),
			AsInt(Step(Latch, Moving(0.0f), Gait)), AsInt(EElysiumAnimActivityCode::Land));
	}
	{
		// The recovered phase-8 three-way: still lands, ducked asks for the crouched land, and moving
		// takes its gait rather than landing at all.
		FElysiumJumpLatch Latch;
		Latch.Phase = EElysiumAirPhase::Landing;
		FElysiumLocomotionSample Ducked = Moving(0.0f);
		Ducked.Stance = EElysiumStance::Ducked;
		TestEqual(TEXT("a ducked landing asks for the crouched land"),
			AsInt(ElysiumAnimIntent::Classify(Ducked, Latch, Gait)),
			AsInt(EElysiumAnimActivityCode::LandCrouch));

		TestEqual(TEXT("a moving landing falls through to the gait"),
			AsInt(ElysiumAnimIntent::Classify(Moving(Gait.RunSpeedCmPerSecond), Latch, Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
	}
	{
		// --- The air phases belong to the producer that commands jumps ---------------------------
		//
		// LIFE3: retail's ground/air classifier is the player chain's — the cast's air activities are
		// requested by scripted tasks (ManBat's fall, the Asian Vampire's jump) and there is no
		// generic NPC producer of ACT_FALLING in the shipped binary. So the SAME airborne sample has
		// to answer two different ways depending on who published it; one answer for both would mean
		// the rule is not being applied at all, which is what this pairing exists to catch.
		using ElysiumAnimIntent::AdvanceJumpLatch;

		FElysiumLocomotionSample Airborne = Moving(Gait.WalkSpeedCmPerSecond);
		Airborne.bOnGround = false;

		FElysiumJumpLatch Player;
		Player = AdvanceJumpLatch(Player, Airborne, 1.0f / 60.0f, Gait,
			EElysiumOneShotState::Unknown, /*bCommandsJumps=*/true);
		TestEqual(TEXT("a player body walking off a ledge falls"),
			AsInt(Player.Phase), AsInt(EElysiumAirPhase::Falling));
		TestEqual(TEXT("...and classifies as ACT_FALLING"),
			AsInt(ElysiumAnimIntent::Classify(Airborne, Player, Gait)),
			AsInt(EElysiumAnimActivityCode::Falling));

		FElysiumJumpLatch Cast;
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			Cast = AdvanceJumpLatch(Cast, Airborne, 1.0f / 60.0f, Gait,
				EElysiumOneShotState::Unknown, /*bCommandsJumps=*/false);
		}
		TestEqual(TEXT("the identical sample leaves a cast body grounded"),
			AsInt(Cast.Phase), AsInt(EElysiumAirPhase::Grounded));
		TestEqual(TEXT("...and it keeps its gait rather than selecting a fall"),
			AsInt(ElysiumAnimIntent::Classify(Airborne, Cast, Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));

		// The half of the latch that is NOT the air phase still has to advance, or the cast could
		// never select a run at all.
		FElysiumLocomotionSample Sprinting = Moving(Gait.RunSpeedCmPerSecond);
		Sprinting.bOnGround = false;
		FElysiumJumpLatch Running;
		Running = AdvanceJumpLatch(Running, Sprinting, 1.0f / 60.0f, Gait,
			EElysiumOneShotState::Unknown, /*bCommandsJumps=*/false);
		TestTrue(TEXT("the gait memory still advances for a producer with no jump command"),
			Running.bLastGaitWasRun);
		TestEqual(TEXT("...so a fast cast body still reaches the relaxed run"),
			AsInt(ElysiumAnimIntent::Classify(Sprinting, Running, Gait)),
			AsInt(EElysiumAnimActivityCode::RunRelaxed));
	}
	{
		// --- The landing ends when its CLIP ends, and the timer is only the fallback -------------
		using ElysiumAnimIntent::AdvanceJumpLatch;
		using EOne = EElysiumOneShotState;

		auto Landed = []
		{
			FElysiumJumpLatch L;
			L.Phase = EElysiumAirPhase::Landing;
			return L;
		};
		const FElysiumLocomotionSample Still = Moving(0.0f);
		const float Step60 = 1.0f / 60.0f;

		// `Playing` outranks the stopwatch entirely. A landing clip longer than `LandHoldSeconds`
		// would otherwise be cut off mid-pose by a timer racing the graph — which is the whole
		// reason the report exists.
		{
			FElysiumJumpLatch L = Landed();
			for (int32 Frame = 0; Frame < 120; ++Frame)   // two seconds, far past the 0.35 s fallback
			{
				L = AdvanceJumpLatch(L, Still, Step60, Gait, EOne::Playing);
			}
			TestEqual(TEXT("a landing whose clip is still playing does not time out"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Landing));
			L = AdvanceJumpLatch(L, Still, Step60, Gait, EOne::Complete);
			TestEqual(TEXT("and it ends on the frame the clip reports complete"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Grounded));
		}

		// `Complete` ends it immediately, well inside the fallback window — so a clip shorter than
		// the timer is not held past its own end either.
		{
			FElysiumJumpLatch L = AdvanceJumpLatch(Landed(), Still, Step60, Gait, EOne::Complete);
			TestEqual(TEXT("a completed landing clip ends the phase at once"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Grounded));
		}

		// `Unknown` is the body with no pose layer — the gym stands one, and it still has to stand
		// up. **This is the path `LandHoldSeconds` exists for and it must not regress.**
		{
			FElysiumJumpLatch L = Landed();
			L = AdvanceJumpLatch(L, Still, Step60, Gait, EOne::Unknown);
			TestEqual(TEXT("with no report the landing holds while the timer runs"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Landing));
			for (int32 Frame = 0; Frame < 60 && L.Phase == EElysiumAirPhase::Landing; ++Frame)
			{
				L = AdvanceJumpLatch(L, Still, Step60, Gait, EOne::Unknown);
			}
			TestEqual(TEXT("and the timer still ends it"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Grounded));
		}

		// The default argument IS the fallback, so every existing caller keeps the timer path
		// without naming it — which is what stops this change reaching the gym.
		{
			FElysiumJumpLatch L = Landed();
			for (int32 Frame = 0; Frame < 60 && L.Phase == EElysiumAirPhase::Landing; ++Frame)
			{
				L = AdvanceJumpLatch(L, Still, Step60, Gait);
			}
			TestEqual(TEXT("a caller passing no report gets the timer"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Grounded));
		}

		// A moving landing outranks every one of them: retail's phase 8 takes the gait outright.
		{
			FElysiumJumpLatch L = AdvanceJumpLatch(Landed(), Moving(Gait.RunSpeedCmPerSecond),
				Step60, Gait, EOne::Playing);
			TestEqual(TEXT("a moving landing leaves even while its clip plays"),
				AsInt(L.Phase), AsInt(EElysiumAirPhase::Grounded));
		}
	}
	{
		// Water branches before everything else, because the move itself branches at Waist.
		FElysiumJumpLatch Latch;
		FElysiumLocomotionSample Deep = Moving(200.0f);
		Deep.Water = EElysiumWaterLevel::Waist;
		TestEqual(TEXT("a moving body in deep water swims"),
			AsInt(Step(Latch, Deep, Gait)), AsInt(EElysiumAnimActivityCode::Swim));

		Deep.LocalVelocity = FVector::ZeroVector;
		TestEqual(TEXT("and treads water when it stops"),
			AsInt(ElysiumAnimIntent::Classify(Deep, Latch, Gait)),
			AsInt(EElysiumAnimActivityCode::Treadwater));

		FElysiumLocomotionSample Shallow = Moving(200.0f);
		Shallow.Water = EElysiumWaterLevel::Feet;
		TestEqual(TEXT("ankle-deep is still walking"),
			AsInt(ElysiumAnimIntent::Classify(Shallow, Latch, Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
	}

	// --- CCC10 — weapon grip, which is not melee-versus-ranged ---------------------------------------
	{
		// The property under test: a resolver keyed on "is this melee" gets `bushhook` and
		// `sledgehammer` wrong, because their upper-body mask is the SAME 49-bone gate every firearm
		// and every aim grid uses (`docs/vtmb/animation_and_movers.md` A.4).
		TestEqual(TEXT("bushhook is two-handed, not one-handed melee"),
			static_cast<int32>(WeaponGrip(TEXT("bushhook"))),
			static_cast<int32>(EElysiumWeaponGrip::TwoHanded));
		TestEqual(TEXT("sledgehammer is two-handed too"),
			static_cast<int32>(WeaponGrip(TEXT("sledgehammer"))),
			static_cast<int32>(EElysiumWeaponGrip::TwoHanded));

		const TCHAR* OneHanded[] =
			{ TEXT("baseballbat"), TEXT("katana"), TEXT("knife"), TEXT("stake"), TEXT("tireiron") };
		for (const TCHAR* WeaponTag : OneHanded)
		{
			TestEqual(FString::Printf(TEXT("%s takes the right-arm mask"), WeaponTag),
				static_cast<int32>(WeaponGrip(WeaponTag)),
				static_cast<int32>(EElysiumWeaponGrip::OneHanded));
		}

		TestEqual(TEXT("a firearm defaults to the same two-handed mask the aim grids use"),
			static_cast<int32>(WeaponGrip(TEXT("glock"))),
			static_cast<int32>(EElysiumWeaponGrip::TwoHanded));
		TestEqual(TEXT("and an unlisted tag takes the same default, never a guess"),
			static_cast<int32>(WeaponGrip(TEXT("nonexistent_weapon"))),
			static_cast<int32>(EElysiumWeaponGrip::TwoHanded));
		TestEqual(TEXT("the lookup is case-insensitive"),
			static_cast<int32>(WeaponGrip(TEXT("BUSHHOOK"))),
			static_cast<int32>(EElysiumWeaponGrip::TwoHanded));
	}

	// --- The activity naming, which is one table read both ways ---------------------------------------
	{
		TestEqual(TEXT("the code names the activity"),
			FString(ActivityName(EElysiumAnimActivityCode::Run)), FString(TEXT("ACT_RUN")));
		TestEqual(TEXT("and the activity names the code"),
			AsInt(ActivityCode(TEXT("ACT_LAND_CROUCH"))),
			AsInt(EElysiumAnimActivityCode::LandCrouch));
		TestEqual(TEXT("the lookup is case-insensitive, like every vocabulary key"),
			AsInt(ActivityCode(TEXT("act_sneak"))), AsInt(EElysiumAnimActivityCode::Sneak));
		// Everything outside the slice reads Unknown, which is a true statement rather than a hole:
		// the registry has 4,460 entries and this projection covers eleven of them.
		TestEqual(TEXT("an activity outside the slice is Unknown"),
			AsInt(ActivityCode(TEXT("ACT_RELOAD"))), AsInt(EElysiumAnimActivityCode::Unknown));
	}

	// --- The intent the driver hands the resolver ------------------------------------------------------
	{
		FElysiumJumpLatch Latch;
		const FElysiumLocomotionSample Sample = Moving(Gait.RunSpeedCmPerSecond);
		Latch = AdvanceJumpLatch(Latch, Sample, 1.0f / 60.0f, Gait);

		const FElysiumAnimationIntent Intent = BuildLocomotionIntent(Sample, Latch, Gait,
			EElysiumAnimSource::Player, EElysiumAnimBodyKind::Player,
			TEXT("tremere_Male_Armor_0"), FElysiumEntityHandle(7, 1), 3);

		TestTrue(TEXT("an activity request names no label"), Intent.IsWellFormed());
		TestEqual(TEXT("it carries the classified activity"), Intent.Activity,
			FString(TEXT("ACT_RUN_RELAXED")));
		TestEqual(TEXT("and the whole sample, so the record can say what was classified"),
			Intent.Body.Speed2D(), Sample.Speed2D());
		TestEqual(TEXT("the variant rides through, because the pick has to be repeatable"),
			Intent.Variant, 3);
		TestTrue(TEXT("a gait loops"), Intent.bLoop);

		// A one-shot is a property of the request, not of the clip's looping flag: `crouch` is a
		// non-looping "into" pose that the body nonetheless holds.
		FElysiumJumpLatch Landing;
		Landing.Phase = EElysiumAirPhase::Landing;
		const FElysiumAnimationIntent Land = BuildLocomotionIntent(Moving(0.0f), Landing, Gait,
			EElysiumAnimSource::Player, EElysiumAnimBodyKind::Player,
			TEXT("tremere_Male_Armor_0"), FElysiumEntityHandle(7, 1), 0);
		TestEqual(TEXT("the land is a one-shot"), Land.bLoop, false);

		FElysiumJumpLatch Grounded;
		FElysiumLocomotionSample Crouching = Moving(0.0f);
		Crouching.Stance = EElysiumStance::Ducked;
		const FElysiumAnimationIntent Crouch = BuildLocomotionIntent(Crouching, Grounded, Gait,
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast, TEXT("regular_cop"),
			FElysiumEntityHandle(), 0);
		TestTrue(TEXT("a held crouch is not"), Crouch.bLoop);
		TestEqual(TEXT("and the cast comes through the same builder"),
			static_cast<int32>(Crouch.Source), static_cast<int32>(EElysiumAnimSource::Npc));
		TestEqual(TEXT("carrying the body kind whose chain it will translate through"),
			static_cast<int32>(Crouch.BodyKind), static_cast<int32>(EElysiumAnimBodyKind::Cast));

		// The same body sample, asked for by the cast: the relaxed forms are the player selector's
		// own output, and the two rows that undo them are a `CBasePlayer` virtual no cast body
		// reaches. A cast gait request is therefore the plain activity, or nothing downstream would
		// ever turn it back into one.
		const FElysiumAnimationIntent CastRun = BuildLocomotionIntent(Sample, Latch, Gait,
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast, TEXT("regular_cop"),
			FElysiumEntityHandle(), 0);
		TestEqual(TEXT("a cast body asks for the plain run"), CastRun.Activity,
			FString(TEXT("ACT_RUN")));

		// --- `aim_pitch`: the view's vertical, negated into the grid's own sign and clamped ------
		// The grid's pitch axis is positive-DOWN (its low end carries the `_aim_UC` cells), so an
		// Unreal view looking UP has to arrive negative. The clamp is the grid's -45..45 span; the
		// shot is unaffected by it, which is why the bound lives here and not on the trace.
		auto PitchIntent = [&](float ViewPitch)
		{
			FElysiumLocomotionSample Looking = Moving(0.0f);
			Looking.ViewPitch = ViewPitch;
			return BuildLocomotionIntent(Looking, Grounded, Gait, EElysiumAnimSource::Player,
				EElysiumAnimBodyKind::Player, TEXT("tremere_Male_Armor_0"),
				FElysiumEntityHandle(7, 1), 0).AimPitch;
		};
		TestEqual(TEXT("a level view aims level"), PitchIntent(0.0f), 0.0f);
		TestEqual(TEXT("looking UP 30 deg reaches the grid's negative end"),
			PitchIntent(30.0f), -30.0f);
		TestEqual(TEXT("looking DOWN 30 deg reaches its positive end"),
			PitchIntent(-30.0f), 30.0f);
		TestEqual(TEXT("a steeper look up saturates at the grid's own span"),
			PitchIntent(80.0f), -45.0f);
		TestEqual(TEXT("...and down likewise"), PitchIntent(-80.0f), 45.0f);
		// The wound form a controller rotation arrives in is the same angle, so it must resolve to
		// the same cell rather than saturating on the way past 180.
		TestEqual(TEXT("an unnormalized 330 deg is the same 30 deg down"),
			PitchIntent(330.0f), 30.0f);
		// A cast body's producer never states a view pitch, so its grids keep the centre column.
		TestEqual(TEXT("a cast sample that states none stays centred"),
			BuildLocomotionIntent(Moving(0.0f), Grounded, Gait, EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Cast, TEXT("regular_cop"), FElysiumEntityHandle(), 0).AimPitch,
			0.0f);
	}

	return true;
}

// =====================================================================================
// Steps 4, 5 and 6 — the resolver, against a catalog built on the stack.
//
// The fixture is two banks and two bodies, because that is the smallest thing that can fail the way
// this rung exists to prevent: `run` is one label, and it reaches a PC-only bank on a player body and
// the shared cast bank on an NPC. A resolver keyed on the label alone passes the first assertion here
// and fails the second, which is exactly the bug that would otherwise ship as "the player runs a bit
// oddly".
// =====================================================================================

namespace
{
	constexpr const TCHAR* PcBank = TEXT("runotherspc_pcidles_allsequences");
	constexpr const TCHAR* CastBank = TEXT("move_and_ranged");
	constexpr const TCHAR* MiscBank = TEXT("misc");

	FElysiumNpcClip MakeClip(const TCHAR* Owner, const TCHAR* Activity, int32 Weight, int32 Flags,
		int32 Frames = 30, float Fps = 30.0f, float Fade = 0.2f)
	{
		FElysiumNpcClip Clip;
		Clip.Owner = Owner;
		Clip.Activity = Activity;
		Clip.Weight = Weight;
		Clip.Flags = Flags;
		Clip.Frames = Frames;
		Clip.Fps = Fps;
		Clip.Fade = Fade;
		return Clip;
	}

	// A 9x1 `move_yaw` fan. Cell 0 is the -180 backpedal and cell 8 is +180, which is why the two
	// share one clip — and why the animation the LABEL bakes from is the backpedal rather than the
	// forward stride.
	FElysiumBlendGrid MakeFan(const TCHAR* Label, const FString& Prefix, float StrideAt180,
		float StrideAt0)
	{
		FElysiumBlendGrid Grid;
		Grid.Label = Label;
		Grid.GroupSize[0] = 9;
		Grid.GroupSize[1] = 1;
		Grid.ParamIndex[0] = 0;
		Grid.ParamIndex[1] = INDEX_NONE;
		Grid.ParamStart[0] = -180.0f;
		Grid.ParamEnd[0] = 180.0f;

		static const int32 Degrees[9] = { 180, 225, 270, 315, 0, 45, 90, 135, 180 };
		for (int32 Index = 0; Index < 9; ++Index)
		{
			FElysiumBlendCell Cell;
			Cell.Axis[0] = Index;
			Cell.Axis[1] = 0;
			Cell.Clip = FString::Printf(TEXT("%s_%d"), *Prefix, Degrees[Index]);
			Cell.Motion.CycleSeconds = 0.6f;
			Cell.Motion.GroundSpeedCmPerSecond = (Index == 4) ? StrideAt0 : StrideAt180;
			Grid.Cells.Add(Cell);
		}
		return Grid;
	}

	FElysiumPoseParamDesc MoveYawParam()
	{
		FElysiumPoseParamDesc Desc;
		Desc.Name = TEXT("move_yaw");
		Desc.Start = -180.0f;
		Desc.End = 180.0f;
		Desc.Loop = 360.0f;
		return Desc;
	}

	// The producer and the body are stated apart, because the resolver forks on the second and not
	// the first — a fixture that could only spell them together could not express the case this
	// suite is about.
	FElysiumAnimationIntent ActivityIntent(const TCHAR* Stem, const TCHAR* Activity,
		EElysiumAnimSource Source = EElysiumAnimSource::Player,
		EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Player)
	{
		FElysiumAnimationIntent Intent;
		Intent.Stem = Stem;
		Intent.Activity = Activity;
		Intent.Source = Source;
		Intent.BodyKind = BodyKind;
		Intent.Route = EElysiumAnimRoute::Activity;
		return Intent;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationResolveTest,
	"Elysium.Substrate.AnimationResolve", GElysiumAnimationTestFlags)
bool FElysiumAnimationResolveTest::RunTest(const FString&)
{
	// --- The two bodies -------------------------------------------------------------------------
	FElysiumNpcClipSet Pc;
	Pc.Stem = TEXT("pc_body");
	Pc.Clips.Add(TEXT("run"), MakeClip(PcBank, TEXT("ACT_RUN"), 30, 0x1, 19, 30.0f));
	Pc.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
	Pc.Clips.Add(TEXT("sneak"), MakeClip(CastBank, TEXT("ACT_SNEAK"), 30, 0x1, 66));
	// `crouch` is a 61-frame non-looping "into" pose — the flag is 0, and nothing in the vocabulary
	// holds an unarmed crouch idle.
	Pc.Clips.Add(TEXT("crouch"), MakeClip(CastBank, TEXT("ACT_CROUCH"), 30, 0x0, 61));
	Pc.Clips.Add(TEXT("idle01"), MakeClip(MiscBank, TEXT("ACT_IDLE"), 30, 0x1, 30, 30.0f, 0.3f));
	Pc.Clips.Add(TEXT("fidget01"), MakeClip(MiscBank, TEXT("ACT_IDLE"), 1, 0x0, 60, 30.0f, 0.3f));
	Pc.Clips.Add(TEXT("fidget02"), MakeClip(MiscBank, TEXT("ACT_IDLE"), 1, 0x0, 60, 30.0f, 0.3f));
	Pc.Clips.Add(TEXT("leap"), MakeClip(MiscBank, TEXT("ACT_LEAP"), 30, 0x0));
	Pc.Clips.Add(TEXT("falling"), MakeClip(MiscBank, TEXT("ACT_FALLING"), 30, 0x1));
	Pc.Clips.Add(TEXT("land"), MakeClip(MiscBank, TEXT("ACT_LAND"), 30, 0x0));
	// An exact label a scripted sequence names, carrying no activity at all.
	Pc.Clips.Add(TEXT("Jump2"), MakeClip(MiscBank, TEXT(""), 0, 0x0));
	// An additive layer: flags 0x14, no activity, and never selectable as a base pose.
	Pc.Clips.Add(TEXT("pistol_aim_layer"), MakeClip(CastBank, TEXT(""), 0, 0x14));
	// The weapon-layer translation target: an ordinary masked sequence, reached only through the
	// rename rule's `ACT_RANGE_ATTACK_LAYER_GLOCK`, never through the untranslated
	// `ACT_RANGE_ATTACK1_LAYER`.
	Pc.Clips.Add(TEXT("glock_attack_layer"),
		MakeClip(CastBank, TEXT("ACT_RANGE_ATTACK_LAYER_GLOCK"), 30, 0x0));
	// **The shipped corpus's own glock gait**, and the reason the ladder's order is load-bearing:
	// `glock_relaxed_walk` carries the misspelled literal `AWS_WALK_RELAXED_GLOCK`, so rung 1 of the
	// glock ladder names an activity no body answers and rung 2's shared `_PISTOL` set is what
	// actually poses a glock-armed relaxed walk. The fixture reproduces exactly that shape.
	Pc.Clips.Add(TEXT("glock_relaxed_walk"),
		MakeClip(CastBank, TEXT("AWS_WALK_RELAXED_GLOCK"), 30, 0x1, 46));
	Pc.Clips.Add(TEXT("pistol_relaxed_walk"),
		MakeClip(CastBank, TEXT("ACT_WALK_RELAXED_PISTOL"), 30, 0x1, 46));
	// The one `required`-flagged base in both glock blocks is `ACT_RANGE_ATTACK1`, which the rename
	// rule turns into `ACT_RANGE_ATTACK_<family>`. This body carries only the shared form — a shipped
	// body carries the glock one and answers at rung 1 — so the flagged rung-1 row misses here and
	// the walk has to keep going.
	Pc.Clips.Add(TEXT("pistol_attack"),
		MakeClip(CastBank, TEXT("ACT_RANGE_ATTACK_PISTOL"), 30, 0x0, 22));
	// The combat-ready stand (LIFE4). The glock ladder's block-0 exception names `ACT_AIM_GLOCK`
	// and this body carries only the shared pistol form, so a combat-ready stand answers at rung
	// 2, the same shape as the relaxed walk above.
	Pc.Clips.Add(TEXT("pistol_ready"), MakeClip(CastBank, TEXT("ACT_AIM_PISTOL"), 30, 0x1));
	// **No `land_crouch`.** The controlled corpus records the one ducked ACT_LAND_CROUCH request
	// returning -1 on a validated player body, so the fixture must not invent one.

	FElysiumNpcClipSet Cast;
	Cast.Stem = TEXT("cast_body");
	Cast.Clips.Add(TEXT("run"), MakeClip(CastBank, TEXT("ACT_RUN"), 30, 0x1));
	Cast.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
	Cast.Clips.Add(TEXT("Stance_Normal_Idle_1"),
		MakeClip(TEXT("stances"), TEXT("ACT_DISPOSITION"), 30, 0x1));

	// --- The two banks, which disagree ------------------------------------------------------------
	FElysiumBlendTable PcTable;
	PcTable.Stem = PcBank;
	PcTable.PoseParams.Add(MoveYawParam());
	PcTable.Grids.Add(TEXT("run"), MakeFan(TEXT("run"), TEXT("run"), 522.0f, 478.7f));

	FElysiumBlendTable CastTable;
	CastTable.Stem = CastBank;
	CastTable.PoseParams.Add(MoveYawParam());
	// The cast's cells are spelled `npc_run_*` and it backpedals at 313.2 where the PC bank runs at
	// 522.0 — the two agree exactly on the 0 degree cell and nowhere else.
	CastTable.Grids.Add(TEXT("run"), MakeFan(TEXT("run"), TEXT("npc_run"), 313.2f, 478.7f));
	CastTable.Grids.Add(TEXT("walk"), MakeFan(TEXT("walk"), TEXT("walk"), 200.0f, 254.0f));
	// One host declaring its additive first, so the declaration order is a claim and not an accident.
	FElysiumAutoLayerBinding Layers;
	Layers.Clips = { TEXT("pistol_aim_layer"), TEXT("pistol_aim_overlay") };
	CastTable.AutoLayers.Add(TEXT("walk"), Layers);

	auto Tables = [&PcTable, &CastTable](const FString& Owner) -> const FElysiumBlendTable*
	{
		if (Owner.Equals(PcBank, ESearchCase::IgnoreCase)) { return &PcTable; }
		if (Owner.Equals(CastBank, ESearchCase::IgnoreCase)) { return &CastTable; }
		return nullptr;
	};

	FElysiumAnimationCatalog PcCatalog;
	PcCatalog.Clips = &Pc;
	PcCatalog.BlendTableFor = Tables;

	FElysiumAnimationCatalog CastCatalog;
	CastCatalog.Clips = &Cast;
	CastCatalog.BlendTableFor = Tables;

	FElysiumAnimationSelection Player;
	FElysiumAnimationSelection Npc;

	// --- The acceptance clause: one label, two banks ------------------------------------------------
	ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_RUN")), PcCatalog, Player);
	ElysiumAnimResolve::Resolve(
		ActivityIntent(TEXT("cast_body"), TEXT("ACT_RUN"), EElysiumAnimSource::Npc,
			EElysiumAnimBodyKind::Cast),
		CastCatalog, Npc);

	TestEqual(TEXT("the player resolves the label `run`"), Player.SequenceLabel, FString(TEXT("run")));
	TestEqual(TEXT("and the cast resolves the same label"), Npc.SequenceLabel, FString(TEXT("run")));
	TestEqual(TEXT("but the player's run comes off the PC-only bank"), Player.OwnerStem,
		FString(PcBank));
	TestEqual(TEXT("and the cast's off the shared bank"), Npc.OwnerStem, FString(CastBank));
	TestEqual(TEXT("so a resolver keyed on the label alone would hand the player the cast's gait"),
		Player.OwnerStem == Npc.OwnerStem, false);

	// The two fans are not aliased: different clips, and different strides everywhere but 0 degrees.
	TestEqual(TEXT("the player's forward cell"), Player.AnimationName, FString(TEXT("run_0")));
	TestEqual(TEXT("the cast's forward cell is spelled differently"), Npc.AnimationName,
		FString(TEXT("npc_run_0")));
	TestEqual(TEXT("and both banks agree at 0 degrees"), Player.GroundSpeedCmPerSecond,
		Npc.GroundSpeedCmPerSecond);

	// --- The base cell trap: anim[0][0] is the backpedal ----------------------------------------------
	{
		// Resolving at the neutral pose must land on cell 4, not on the grid's declaration-order base.
		// Getting this wrong makes a body walk forward while playing its backward stride.
		TestEqual(TEXT("the neutral pose selects the forward cell, not the base cell"),
			Player.GroundSpeedCmPerSecond, 478.7f);

		FElysiumAnimationIntent Backpedal = ActivityIntent(TEXT("pc_body"), TEXT("ACT_RUN"));
		Backpedal.Body.LocalVelocity = FVector(-100.0f, 0.0f, 0.0f);
		// The pose parameter, which is what steers a fan — its unfiltered input is set with it so the
		// fixture is a body that has been backpedalling rather than one caught mid-turn.
		Backpedal.Body.MoveYawVelocity = 180.0f;
		Backpedal.Body.MoveYawPose = 180.0f;
		FElysiumAnimationSelection Back;
		ElysiumAnimResolve::Resolve(Backpedal, PcCatalog, Back);
		TestEqual(TEXT("and a backpedalling body reaches the 180 degree cell"), Back.AnimationName,
			FString(TEXT("run_180")));
		TestEqual(TEXT("which is the cell the two banks disagree most about"),
			Back.GroundSpeedCmPerSecond, 522.0f);
	}

	// --- Step 5's asset shape and the published parameters ----------------------------------------------
	TestEqual(TEXT("a movement fan resolves to a blend space"),
		static_cast<int32>(Player.AssetKind), static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));
	TestEqual(TEXT("with one axis"), Player.Axes, 1);
	TestEqual(TEXT("bound to the parameter the sidecar declares"), Player.AxisName[0],
		FString(TEXT("move_yaw")));
	TestEqual(TEXT("the authored fade travels with the selection"), Player.FadeSeconds, 0.2f);
	TestTrue(TEXT("and the looping flag"), Player.bLooping);
	TestEqual(TEXT("the logical request stays un-translated"), Player.RequestedActivity,
		FString(TEXT("ACT_RUN")));
	{
		FElysiumAnimationSelection Idle;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_IDLE")), PcCatalog,
			Idle);
		TestEqual(TEXT("a plain label resolves to a sequence"), static_cast<int32>(Idle.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::Sequence));
		TestEqual(TEXT("owned by the bank the DAG named"), Idle.OwnerStem, FString(MiscBank));
		TestEqual(TEXT("carrying the 0.3 second fade its bank authored"), Idle.FadeSeconds, 0.3f);
		TestEqual(TEXT("and three candidates carried the activity"), Idle.Candidates, 3);

		FElysiumAnimationSelection Crouch;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_CROUCH")), PcCatalog,
			Crouch);
		TestEqual(TEXT("the crouch is a non-looping into-pose"), Crouch.bLooping, false);
	}

	// --- A grid is playable in every locomotion state ----------------------------------------------
	{
		FElysiumNpcClipSet IdleGridBody;
		IdleGridBody.Stem = TEXT("pc_body");
		IdleGridBody.Clips.Add(TEXT("idle"), MakeClip(MiscBank, TEXT("ACT_IDLE"), 30, 0x1));
		IdleGridBody.Clips.Add(TEXT("land"), MakeClip(MiscBank, TEXT("ACT_LAND"), 30, 0x0));

		FElysiumBlendTable MiscTable;
		MiscTable.Stem = MiscBank;
		MiscTable.PoseParams.Add(MoveYawParam());
		MiscTable.Grids.Add(TEXT("idle"), MakeFan(TEXT("idle"), TEXT("idle"), 0.0f, 0.0f));
		MiscTable.Grids.Add(TEXT("land"), MakeFan(TEXT("land"), TEXT("land"), 0.0f, 0.0f));

		auto IdleTables = [&MiscTable](const FString& Owner) -> const FElysiumBlendTable*
		{
			return Owner.Equals(MiscBank, ESearchCase::IgnoreCase) ? &MiscTable : nullptr;
		};
		FElysiumAnimationCatalog IdleCatalog;
		IdleCatalog.Clips = &IdleGridBody;
		IdleCatalog.BlendTableFor = IdleTables;

		FElysiumAnimationSelection IdleGrid;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_IDLE")), IdleCatalog,
			IdleGrid);
		TestEqual(TEXT("a grid whose activity routes to Idle resolves"),
			static_cast<int32>(IdleGrid.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("as a blend space"), static_cast<int32>(IdleGrid.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));

		FElysiumAnimationSelection LandGrid;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_LAND")), IdleCatalog,
			LandGrid);
		TestEqual(TEXT("a grid whose activity routes to Land resolves the same way"),
			static_cast<int32>(LandGrid.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("Land's grid is a blend space"), static_cast<int32>(LandGrid.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));

		// An exact-label grid carries no activity, so the record's state is Idle — which can play it.
		FElysiumAnimationIntent ExactGrid;
		ExactGrid.Stem = TEXT("pc_body");
		ExactGrid.SequenceLabel = TEXT("idle");
		ExactGrid.Route = EElysiumAnimRoute::ExactLabel;
		FElysiumAnimationSelection Exact;
		ElysiumAnimResolve::Resolve(ExactGrid, IdleCatalog, Exact);
		TestEqual(TEXT("an exact-label grid routes to Idle and resolves"),
			static_cast<int32>(Exact.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("as a blend space"), static_cast<int32>(Exact.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));
	}

	// --- The named missing-sequence fallback -----------------------------------------------------------
	{
		// The one ducked state-8 request the controlled corpus records returns -1 on a validated
		// player body. What must come back is a record that NAMES the miss — not an invented clip and
		// not a silent substitution of ACT_LAND.
		FElysiumAnimationSelection Miss;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_LAND_CROUCH")),
			PcCatalog, Miss);
		TestEqual(TEXT("ACT_LAND_CROUCH resolves nothing on a player body"),
			static_cast<int32>(Miss.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MissingSequence));
		TestEqual(TEXT("and no asset is substituted"), static_cast<int32>(Miss.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::None));
		TestTrue(TEXT("the detail names the activity that missed"),
			Miss.Detail.Contains(TEXT("ACT_LAND_CROUCH")));
		TestTrue(TEXT("and the body it missed on"), Miss.Detail.Contains(TEXT("pc_body")));
		TestEqual(TEXT("the request is still on the record"), Miss.RequestedActivity,
			FString(TEXT("ACT_LAND_CROUCH")));
	}

	// --- A flagged row that misses does not stop the walk -------------------------------------------------
	{
		// `CBaseCombatWeapon::ActivityOverride` never reads a row's third dword, so the 201 flagged
		// rows and the 9,013 optional ones take the same availability path. `ACT_RANGE_ATTACK1` is
		// the one flagged base in both of the glock's blocks, and the fixture body is built to miss
		// its first rung: if the bit gated anything, that miss would end the walk instead of falling
		// through to the shared set at rung 2.
		FElysiumAnimationIntent Shot = ActivityIntent(TEXT("pc_body"), TEXT("ACT_RANGE_ATTACK1"));
		Shot.WeaponClassname = TEXT("item_w_glock_17c");
		const ElysiumAnimResolve::FElysiumTranslationResult Fired =
			ElysiumAnimResolve::TranslateActivity(Shot, PcCatalog);
		TestEqual(TEXT("the flagged rung-1 row misses and the walk continues"), Fired.WeaponRung, 2);
		TestEqual(TEXT("landing on the shared class set"), Fired.Resolved,
			FString(TEXT("ACT_RANGE_ATTACK_PISTOL")));
		TestTrue(TEXT("and the answering row's authored bit is reported, never acted on"),
			Fired.bRequired);
	}

	// --- The cast's availability probe, which the player has no equivalent of -----------------------------
	{
		// `CNPC_VStalker`'s pre-translation rewrites `ACT_WALK` to `ACT_COMBATMOVE` unconditionally
		// and returns. A body that carries no combat move still walks, because rung 4 of
		// `CAI_BaseNPC`'s availability probe is the original logical request — and reaching it is the
		// difference between a stalker walking and a stalker standing still.
		FElysiumNpcClipSet Stalker;
		Stalker.Stem = TEXT("stalker_body");
		Stalker.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
		FElysiumAnimationCatalog StalkerCatalog;
		StalkerCatalog.Clips = &Stalker;
		StalkerCatalog.BlendTableFor = Tables;

		FElysiumAnimationIntent Prowl = ActivityIntent(TEXT("stalker_body"), TEXT("ACT_WALK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		Prowl.ActorClassname = TEXT("npc_VStalker");
		const ElysiumAnimResolve::FElysiumTranslationResult Walked =
			ElysiumAnimResolve::TranslateActivity(Prowl, StalkerCatalog);
		TestEqual(TEXT("the class's own pre-translation runs"), Walked.PreTranslation,
			FString(TEXT("ACT_COMBATMOVE")));
		TestEqual(TEXT("nothing it named is playable, so the original request answers"),
			Walked.AvailabilityRung, 4);
		TestEqual(TEXT("as ACT_WALK"), Walked.Resolved, FString(TEXT("ACT_WALK")));

		FElysiumAnimationSelection Fell;
		ElysiumAnimResolve::Resolve(Prowl, StalkerCatalog, Fell);
		TestEqual(TEXT("which the record names as the availability fallback it is"),
			static_cast<int32>(Fell.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::TranslatedFallback));
		TestEqual(TEXT("and it plays the sequence that body actually carries"), Fell.SequenceLabel,
			FString(TEXT("walk")));
		// Which is the whole point of reaching rung 4, and it holds only if the bind reads the asset
		// half rather than the outcome: the outcome here is `TranslatedFallback`, so a load gated on
		// `IsResolved` leaves this stalker holding whatever it last posed while the record above
		// names the clip it is supposed to be walking on.
		TestFalse(TEXT("the outcome is not Resolved"), Fell.IsResolved());
		TestTrue(TEXT("and the record still names a base asset for the bind to load"),
			Fell.NamesBaseAsset());

		// A player asking the same thing has no such probe at all: the two committed player rows are
		// the whole of its actor translation, and a miss after them is a named miss.
		FElysiumAnimationIntent AsPlayer = Prowl;
		AsPlayer.Source = EElysiumAnimSource::Player;
		AsPlayer.BodyKind = EElysiumAnimBodyKind::Player;
		const ElysiumAnimResolve::FElysiumTranslationResult PlayerWalk =
			ElysiumAnimResolve::TranslateActivity(AsPlayer, StalkerCatalog);
		TestEqual(TEXT("the player runs no pre-translation"), PlayerWalk.PreTranslation, FString());
		TestEqual(TEXT("and no availability probe"), PlayerWalk.AvailabilityRung, 0);
	}

	// --- The chain forks on the BODY, not on the producer (LIFE5) ------------------------------------
	{
		// Retail discriminates on the receiver's own class: `CAI_BaseNPC` descendants run the cast
		// chain and `CBasePlayer` runs its one pass. `Source` is who asked, and an NPC's damage
		// reaction and its patrol address the same body — so a resolver forked on the producer would
		// hand a wounded combatant the player's translator mid-fight.
		FElysiumNpcClipSet Turner;
		Turner.Stem = TEXT("turner_body");
		// The one form this body carries is the ALERT turn, which is exactly what the class body
		// (`ClassTranslate_Human`) normalizes away — so the probe has to walk past its own first
		// rungs to pose anything at all.
		Turner.Clips.Add(TEXT("turn_left_alert"),
			MakeClip(CastBank, TEXT("ACT_TURN_LEFT_ALERT"), 30, 0x0));
		FElysiumAnimationCatalog TurnerCatalog;
		TurnerCatalog.Clips = &Turner;
		TurnerCatalog.BlendTableFor = Tables;

		FElysiumAnimationIntent Hurt = ActivityIntent(TEXT("turner_body"), TEXT("ACT_TURN_LEFT"),
			EElysiumAnimSource::Damage, EElysiumAnimBodyKind::Cast);
		Hurt.ActorClassname = TEXT("npc_VHumanCombatant");
		Hurt.WeaponClassname = TEXT("item_w_glock_17c");
		Hurt.ActorState = EElysiumNpcState::Alert;

		const ElysiumAnimResolve::FElysiumTranslationResult Reaction =
			ElysiumAnimResolve::TranslateActivity(Hurt, TurnerCatalog);
		TestEqual(TEXT("a Damage request on a cast body still pre-translates through +0x5dc"),
			Reaction.PreTranslation, FString(TEXT("ACT_TURN_LEFT_ALERT")));
		TestTrue(TEXT("...alternates class and weapon rather than passing once"),
			Reaction.Iterations > 1);
		TestEqual(TEXT("...remembers the class body's own answer"), Reaction.ClassActivity,
			FString(TEXT("ACT_TURN_LEFT")));
		TestEqual(TEXT("...and walks the four-way probe past the two rungs this body cannot play"),
			Reaction.AvailabilityRung, 3);
		TestEqual(TEXT("...landing on the alert form it does carry"), Reaction.Resolved,
			FString(TEXT("ACT_TURN_LEFT_ALERT")));

		FElysiumAnimationSelection Reacted;
		ElysiumAnimResolve::Resolve(Hurt, TurnerCatalog, Reacted);
		TestEqual(TEXT("and the record names the clip the probe reached"), Reacted.SequenceLabel,
			FString(TEXT("turn_left_alert")));
		TestEqual(TEXT("beside the chain that reached it"), static_cast<int32>(Reacted.BodyKind),
			static_cast<int32>(EElysiumAnimBodyKind::Cast));
		TestEqual(TEXT("without losing who asked"), static_cast<int32>(Reacted.Source),
			static_cast<int32>(EElysiumAnimSource::Damage));

		// The same producer, the same request, the same body vocabulary — only the body kind moves.
		FElysiumAnimationIntent HurtPlayer = Hurt;
		HurtPlayer.BodyKind = EElysiumAnimBodyKind::Player;
		const ElysiumAnimResolve::FElysiumTranslationResult PlayerReaction =
			ElysiumAnimResolve::TranslateActivity(HurtPlayer, TurnerCatalog);
		TestEqual(TEXT("the same Damage request on a player body runs no pre-translation"),
			PlayerReaction.PreTranslation, FString());
		TestEqual(TEXT("...no class answer"), PlayerReaction.ClassActivity, FString());
		TestEqual(TEXT("...and no probe"), PlayerReaction.AvailabilityRung, 0);

		// Every source on a player body takes that one pass. The producer rides along; it decides
		// nothing about the chain.
		const EElysiumAnimSource EverySource[] = { EElysiumAnimSource::Player,
			EElysiumAnimSource::Npc, EElysiumAnimSource::Scene, EElysiumAnimSource::Damage,
			EElysiumAnimSource::Interaction, EElysiumAnimSource::Debug };
		for (const EElysiumAnimSource Asked : EverySource)
		{
			FElysiumAnimationIntent OnPlayer = HurtPlayer;
			OnPlayer.Source = Asked;
			const ElysiumAnimResolve::FElysiumTranslationResult Once =
				ElysiumAnimResolve::TranslateActivity(OnPlayer, TurnerCatalog);
			TestEqual(FString::Printf(TEXT("'%s' on a player body is one pass"),
				ElysiumAnimIntent::SourceName(Asked)), Once.Iterations, 1);
			TestEqual(FString::Printf(TEXT("'%s' on a player body runs no probe"),
				ElysiumAnimIntent::SourceName(Asked)), Once.AvailabilityRung, 0);

			// And the same source on the cast body reaches the probe, whichever one it is.
			FElysiumAnimationIntent OnCast = Hurt;
			OnCast.Source = Asked;
			TestEqual(FString::Printf(TEXT("'%s' on a cast body reaches the probe"),
				ElysiumAnimIntent::SourceName(Asked)),
				ElysiumAnimResolve::TranslateActivity(OnCast, TurnerCatalog).AvailabilityRung, 3);
		}

		// The fallback ladder follows the same fork. This body carries only ACT_WALK, so a cast
		// request nothing answers reaches the sequence-zero rung whoever asked, and a player body is
		// handed the named miss it always was.
		FElysiumNpcClipSet Bare;
		Bare.Stem = TEXT("bare_walker");
		Bare.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
		FElysiumAnimationCatalog BareCatalog;
		BareCatalog.Clips = &Bare;
		BareCatalog.BlendTableFor = Tables;

		FElysiumAnimationSelection CastLadder;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("bare_walker"), TEXT("ACT_COWER"), EElysiumAnimSource::Damage,
				EElysiumAnimBodyKind::Cast),
			BareCatalog, CastLadder);
		TestEqual(TEXT("a Damage miss on a cast body walks the ladder to sequence zero"),
			static_cast<int32>(CastLadder.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::SequenceZero));

		FElysiumAnimationSelection PlayerLadder;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("bare_walker"), TEXT("ACT_COWER"), EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Player),
			BareCatalog, PlayerLadder);
		TestEqual(TEXT("and an Npc-sourced miss on a player body takes no ladder at all"),
			static_cast<int32>(PlayerLadder.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MissingSequence));
	}

	// --- The cast's fallback ladder, in the recovered order ---------------------------------------------
	{
		// The ladder is `CAI_BaseNPC`'s, not the player's — which is why the player miss above is a
		// named miss and this one is not.
		FElysiumNpcClipSet WalkOnly;
		WalkOnly.Stem = TEXT("walker");
		WalkOnly.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
		FElysiumAnimationCatalog WalkCatalog;
		WalkCatalog.Clips = &WalkOnly;
		WalkCatalog.BlendTableFor = Tables;

		FElysiumAnimationSelection Retried;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("walker"), TEXT("ACT_RUN"), EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Cast),
			WalkCatalog, Retried);
		TestEqual(TEXT("a missing run retries the walk"), static_cast<int32>(Retried.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::RunToWalk));
		TestEqual(TEXT("and lands on the walk's label"), Retried.SequenceLabel,
			FString(TEXT("walk")));
		TestEqual(TEXT("while the logical request stays what was asked for"),
			Retried.RequestedActivity, FString(TEXT("ACT_RUN")));

		FElysiumAnimationSelection Disposed;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("cast_body"), TEXT("ACT_COWER"), EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Cast),
			CastCatalog, Disposed);
		TestEqual(TEXT("a remaining miss retries the whole request as a disposition"),
			static_cast<int32>(Disposed.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Disposition));
		TestEqual(TEXT("landing on the stance idle"), Disposed.SequenceLabel,
			FString(TEXT("Stance_Normal_Idle_1")));

		FElysiumAnimationSelection Zero;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("walker"), TEXT("ACT_COWER"), EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Cast),
			WalkCatalog, Zero);
		TestEqual(TEXT("and with no disposition either, the hard sequence-zero fallback"),
			static_cast<int32>(Zero.Outcome), static_cast<int32>(EElysiumAnimOutcome::SequenceZero));
		TestEqual(TEXT("which the character export cannot name"), Zero.RawSequenceIndex, 0);
		TestEqual(TEXT("so nothing is substituted"), static_cast<int32>(Zero.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::None));
	}

	// --- Weighted selection is repeatable ---------------------------------------------------------------
	{
		FElysiumAnimationSelection First;
		FElysiumAnimationSelection Second;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_IDLE")), PcCatalog,
			First);
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_IDLE")), PcCatalog,
			Second);
		TestEqual(TEXT("the same stem and variant pick the same clip"), First.SequenceLabel,
			Second.SequenceLabel);
		// And the pick matches the seed the subsystem's own weighted chooser uses, which is what makes
		// one implementation rather than two.
		TestEqual(TEXT("and it is the seed the catalog's chooser uses"), First.SequenceLabel,
			ElysiumAnimResolve::PickWeighted(Pc, TEXT("ACT_IDLE"), 0).Label);
	}

	// --- The route census: one schema, four routes -------------------------------------------------------
	{
		// An exact label bypasses steps 2 and 3 entirely, and still resolves the bank.
		FElysiumAnimationIntent Exact;
		Exact.Stem = TEXT("pc_body");
		Exact.SequenceLabel = TEXT("Jump2");
		Exact.Route = EElysiumAnimRoute::ExactLabel;
		FElysiumAnimationSelection Scripted;
		ElysiumAnimResolve::Resolve(Exact, PcCatalog, Scripted);
		TestEqual(TEXT("an exact label resolves"), static_cast<int32>(Scripted.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("and still names its owning bank"), Scripted.OwnerStem, FString(MiscBank));
		TestTrue(TEXT("with no activity resolution having run"),
			Scripted.RequestedActivity.IsEmpty() && Scripted.ResolvedActivity.IsEmpty());

		// One of the five real scripted-label misses. An ACT_* token arriving here is NOT sent through
		// the activity resolver, which is the whole reason this route is separate.
		Exact.SequenceLabel = TEXT("ACT_COWER");
		FElysiumAnimationSelection SeqZero;
		ElysiumAnimResolve::Resolve(Exact, PcCatalog, SeqZero);
		TestEqual(TEXT("a scripted-label miss takes retail's sequence-zero path"),
			static_cast<int32>(SeqZero.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::ScriptedSequenceZero));
		TestEqual(TEXT("setting sequence 0"), SeqZero.RawSequenceIndex, 0);
		TestTrue(TEXT("and the ACT_ token was never resolved as an activity"),
			SeqZero.ResolvedActivity.IsEmpty());

		// A gesture whose label is absent from the target's whole vocabulary animates nothing. Both
		// gesture calls the shipped corpus makes do exactly this.
		FElysiumAnimationIntent Gesture;
		Gesture.Stem = TEXT("pc_body");
		Gesture.SequenceLabel = TEXT("SM_Huddle");
		Gesture.Route = EElysiumAnimRoute::Gesture;
		Gesture.Channel = EElysiumAnimChannel::Gesture;
		FElysiumAnimationSelection NoOp;
		ElysiumAnimResolve::Resolve(Gesture, PcCatalog, NoOp);
		TestEqual(TEXT("an absent gesture is a no-op, not a fallback"),
			static_cast<int32>(NoOp.Outcome), static_cast<int32>(EElysiumAnimOutcome::GestureNoOp));
		TestEqual(TEXT("nothing plays"), static_cast<int32>(NoOp.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::None));
		TestTrue(TEXT("and nothing else on the record moved"), NoOp.SequenceLabel.IsEmpty()
			&& NoOp.OwnerStem.IsEmpty() && NoOp.RawSequenceIndex == INDEX_NONE);

		// A prop's SetAnimation wire. Declaration order is semantic here and the sidecar carries it,
		// so this is the one route whose record can name an exact raw sequence index.
		FElysiumAnimatedPropEntry Prop;
		Prop.Stem = TEXT("cl_gun_cabinet");
		FElysiumPropClip Rest;
		Rest.Name = TEXT("idle");
		Rest.Index = 0;
		Rest.Flags = 1;
		FElysiumPropClip Open;
		Open.Name = TEXT("showguns");
		Open.Index = 3;
		Prop.Clips.Add(Rest);
		Prop.Clips.Add(Open);

		FElysiumAnimationCatalog PropCatalog;
		PropCatalog.PropClips = &Prop;

		FElysiumAnimationIntent Wire;
		Wire.Stem = TEXT("cl_gun_cabinet");
		Wire.SequenceLabel = TEXT("showguns");
		Wire.Route = EElysiumAnimRoute::SetAnimation;
		Wire.Source = EElysiumAnimSource::Interaction;
		FElysiumAnimationSelection Fired;
		ElysiumAnimResolve::Resolve(Wire, PropCatalog, Fired);
		TestEqual(TEXT("a SetAnimation wire resolves on the prop"),
			static_cast<int32>(Fired.Outcome), static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("and names the exact raw sequence index"), Fired.RawSequenceIndex, 3);
		TestEqual(TEXT("owned by the prop itself, with no bank indirection"), Fired.OwnerStem,
			FString(TEXT("cl_gun_cabinet")));
	}

	// --- A masked sequence is never a base pose -------------------------------------------------------
	{
		FElysiumAnimationIntent Additive;
		Additive.Stem = TEXT("pc_body");
		Additive.SequenceLabel = TEXT("pistol_aim_layer");
		Additive.Route = EElysiumAnimRoute::ExactLabel;
		Additive.Channel = EElysiumAnimChannel::Base;
		FElysiumAnimationSelection Refused;
		ElysiumAnimResolve::Resolve(Additive, PcCatalog, Refused);
		TestEqual(TEXT("an additive asked for on the base channel is refused"),
			static_cast<int32>(Refused.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MaskedRejected));
		TestTrue(TEXT("and it is reported as masked rather than merely missing"), Refused.bMasked);
		TestEqual(TEXT("with no asset handed back"), static_cast<int32>(Refused.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::None));
	}

	// --- ...and neither is a bone-masked partial-body layer, for a DIFFERENT reason ----------------
	{
		// **The refusal itself cannot be reached from here, and this block does not pretend to
		// exercise it.** The pure resolver sees only what `FElysiumNpcClip` carries — the raw studio
		// sequence bits — while a per-bone mask is `UElysiumAnimLayerMask` metadata the bake writes
		// onto the `UAnimSequence`, readable only once the asset has loaded. Both guards that consume
		// it (`UElysiumAnimSubsystem::ResolveAnimation` for the locomotion resolve,
		// `UElysiumAnimSubsystem::ResolveClip` for the montage/claim door every named clip arrives on)
		// therefore stand behind a baked mount and a skeletal mesh, and neither exists in a
		// content-free tier. What follows asserts the two halves that ARE reachable — the channel rule
		// both guards share, and the outcome their readouts render — and nothing more.
		//
		// The channel rule first, over every channel rather than the two that matter today: it is the
		// single predicate both doors ask, and a body handed a masked clip as its base pose collapses
		// — the masked bones decode to a zero quaternion and a zero position, so the pelvis and legs
		// go and the arms stand vertical.
		TestFalse(TEXT("a masked clip may never be posed on the base channel"),
			ElysiumAnimIntent::MaskedClipPlayableOn(EElysiumAnimChannel::Base));
		TestTrue(TEXT("...and may be composed on every layer channel, which is what the mask is for"),
			ElysiumAnimIntent::MaskedClipPlayableOn(EElysiumAnimChannel::UpperBody)
			&& ElysiumAnimIntent::MaskedClipPlayableOn(EElysiumAnimChannel::FullBody)
			&& ElysiumAnimIntent::MaskedClipPlayableOn(EElysiumAnimChannel::Additive)
			&& ElysiumAnimIntent::MaskedClipPlayableOn(EElysiumAnimChannel::Gesture));
		// A claim inherits the channel from its segment, so a producer that states none is asking for
		// the base pose — which is exactly the case the guard exists for, and the default every
		// `scripted_sequence`, `SetAnimation`, green-room stand and NPC idle takes.
		TestFalse(TEXT("a claim built from a channel-less segment asks for the refused channel"),
			ElysiumAnimIntent::MaskedClipPlayableOn(
				ElysiumAnimIntent::ClaimForSegment(FElysiumClipSegment(TEXT("smoke"), false),
					/*PlayLengthSeconds=*/1.0f).Channel));

		// And the readout half: the two illegal base clips are distinct values rendering distinct
		// words, so an additive that reached the base pose and a masked layer that did can never be
		// read as one cause.
		TestNotEqual(TEXT("a masked-layer base refusal is not the additive one"),
			static_cast<int32>(EElysiumAnimOutcome::LayerMaskRejected),
			static_cast<int32>(EElysiumAnimOutcome::MaskedRejected));
		const FString LayerWords(
			ElysiumAnimIntent::OutcomeName(EElysiumAnimOutcome::LayerMaskRejected));
		TestNotEqual(TEXT("...and the two render as different words"), LayerWords,
			FString(ElysiumAnimIntent::OutcomeName(EElysiumAnimOutcome::MaskedRejected)));
		// The renderer's `default:` arm answers `NoVocabulary`, so a value nobody mapped would read on
		// Cog and the MCP surface as a body with no clips at all rather than as a refusal.
		TestNotEqual(TEXT("...and it is mapped rather than falling through to the default arm"),
			LayerWords,
			FString(ElysiumAnimIntent::OutcomeName(EElysiumAnimOutcome::NoVocabulary)));
	}

	// --- The layer binding keeps its declaration order ---------------------------------------------------
	{
		// An overlay declared after an additive overwrites the bones it owns, so a consumer that
		// sorted, deduped or assumed overlay-first would compose the host differently from retail.
		FElysiumAnimationSelection Walk;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("cast_body"), TEXT("ACT_WALK"), EElysiumAnimSource::Npc,
				EElysiumAnimBodyKind::Cast),
			CastCatalog, Walk);
		if (TestEqual(TEXT("the host's two layers travel with the selection"), Walk.LayerLabels.Num(),
			2))
		{
			TestEqual(TEXT("the additive stays first, because that is what the model declared"),
				Walk.LayerLabels[0], FString(TEXT("pistol_aim_layer")));
			TestEqual(TEXT("and the overlay second"), Walk.LayerLabels[1],
				FString(TEXT("pistol_aim_overlay")));
		}
		// A cast request the body itself answers reads availability rung 1 off the record (LIFE4).
		TestEqual(TEXT("a playable cast request answers at availability rung 1"),
			Walk.AvailabilityRung, 1);
	}

	// --- A body with no vocabulary at all ------------------------------------------------------------------
	{
		// The gym stands a body with no map, no entity world and no exported model. That is a state
		// rather than an error, and the record has to say so.
		FElysiumAnimationCatalog Empty;
		FElysiumAnimationSelection Nothing;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT(""), TEXT("ACT_IDLE")), Empty, Nothing);
		TestEqual(TEXT("no vocabulary is reported, not guessed around"),
			static_cast<int32>(Nothing.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::NoVocabulary));
		TestEqual(TEXT("and the request is still on the record"), Nothing.RequestedActivity,
			FString(TEXT("ACT_IDLE")));
	}

	// --- Step 6 publishes whatever the selection turned out to be -------------------------------------------
	{
		FElysiumAnimationIntent Strafing = ActivityIntent(TEXT("pc_body"), TEXT("ACT_RUN"));
		Strafing.Body.LocalVelocity = FVector(0.0f, 400.0f, 0.0f);
		Strafing.Body.MoveYawVelocity = 90.0f;
		Strafing.Body.MoveYawPose = 90.0f;
		FElysiumAnimationSelection Sideways;
		ElysiumAnimResolve::Resolve(Strafing, PcCatalog, Sideways);
		TestEqual(TEXT("the published speed is the body's own"), Sideways.Speed, 400.0f);
		TestEqual(TEXT("the published move_yaw steers the fan"), Sideways.AxisValue[0], 90.0f);
		TestEqual(TEXT("which lands on the right-strafe cell"), Sideways.AnimationName,
			FString(TEXT("run_90")));
	}

	// --- Step 3, over the committed tables ----------------------------------------------------------
	{
		using namespace ElysiumAnimResolve;

		// **The player's two rows**, which are what make an unarmed relaxed gait resolve at all: the
		// fixture carries no `ACT_WALK_RELAXED` sequence, exactly as a shipped player body does not.
		TestEqual(TEXT("the fixture carries no relaxed walk of its own"),
			Pc.ByActivity(TEXT("ACT_WALK_RELAXED")).Num(), 0);

		FElysiumAnimationIntent Relaxed = ActivityIntent(TEXT("pc_body"), TEXT("ACT_WALK_RELAXED"));
		FElysiumTranslationResult Bare = TranslateActivity(Relaxed, PcCatalog);
		TestEqual(TEXT("so `CBasePlayer::NPC_TranslateActivity` turns it into ACT_WALK"), Bare.Resolved,
			FString(TEXT("ACT_WALK")));
		TestEqual(TEXT("with no weapon the first and last weapon answers agree by construction"),
			Bare.FirstWeaponActivity, Bare.WeaponActivity);
		TestEqual(TEXT("and the empty table translated nothing"), Bare.WeaponRung, 0);
		TestTrue(TEXT("the player walks no availability probe"), Bare.AvailabilityRung == 0);
		TestTrue(TEXT("and has no pre-translation before the weapon hook"),
			Bare.PreTranslation.IsEmpty());

		FElysiumAnimationSelection BareSelection;
		ElysiumAnimResolve::Resolve(Relaxed, PcCatalog, BareSelection);
		TestEqual(TEXT("which does resolve"), BareSelection.SequenceLabel, FString(TEXT("walk")));

		// **The availability ladder, which is the whole reason the rows are not materialised.** The
		// glock ladder's first rung names `ACT_WALK_RELAXED_GLOCK`; the body carries the clip but
		// under the misspelled `AWS_` literal, so no rung-1 candidate answers and the shared pistol
		// set at rung 2 is what poses a glock-armed relaxed walk.
		FElysiumAnimationIntent Armed = Relaxed;
		Armed.WeaponClassname = TEXT("item_w_glock_17c");
		const FElysiumTranslationResult Glock = TranslateActivity(Armed, PcCatalog);
		TestEqual(TEXT("the glock's relaxed walk resolves through the ladder's second rung"),
			Glock.WeaponRung, 2);
		TestEqual(TEXT("naming the shared pistol activity"), Glock.Resolved,
			FString(TEXT("ACT_WALK_RELAXED_PISTOL")));

		FElysiumAnimationSelection ArmedSelection;
		ElysiumAnimResolve::Resolve(Armed, PcCatalog, ArmedSelection);
		TestEqual(TEXT("and the body plays the pistol clip, not the glock one"),
			ArmedSelection.SequenceLabel, FString(TEXT("pistol_relaxed_walk")));
		TestEqual(TEXT("resolved outright, because the weapon table already probed availability"),
			static_cast<int32>(ArmedSelection.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Resolved));
		// The hops the walk took reach the RECORD (LIFE4), so a readout shows which rung fired.
		TestEqual(TEXT("the rung that fired is on the record"), ArmedSelection.WeaponRung, 2);
		TestEqual(TEXT("...with no class answer on the player"), ArmedSelection.ClassActivity,
			FString());
		TestEqual(TEXT("...and no availability probe on the player"),
			ArmedSelection.AvailabilityRung, 0);

		// **The combat-ready stand, per weapon** (LIFE4): the gait ladder's `ACT_AIM` request
		// reaches the weapon table like any other base — the glock's block-0 exception names
		// `ACT_AIM_GLOCK`, which this body cannot play, so block 1's `ACT_AIM_PISTOL` stands a
		// combat-ready glock. No Combat special case anywhere in the chain.
		FElysiumAnimationIntent Ready = ActivityIntent(TEXT("pc_body"), TEXT("ACT_AIM"));
		Ready.WeaponClassname = TEXT("item_w_glock_17c");
		FElysiumAnimationSelection ReadySelection;
		ElysiumAnimResolve::Resolve(Ready, PcCatalog, ReadySelection);
		TestEqual(TEXT("a combat-ready glock stand resolves the shared pistol ready"),
			ReadySelection.ResolvedActivity, FString(TEXT("ACT_AIM_PISTOL")));
		TestEqual(TEXT("...through the ladder's second rung, readable off the record"),
			ReadySelection.WeaponRung, 2);
		TestEqual(TEXT("...standing the pistol ready sequence"), ReadySelection.SequenceLabel,
			FString(TEXT("pistol_ready")));
		// An unarmed combat-ready stand has no table and the fixture carries no bare `ACT_AIM`
		// sequence, exactly as a shipped player body does not: the miss is named, not guessed.
		FElysiumAnimationSelection BareReady;
		ElysiumAnimResolve::Resolve(ActivityIntent(TEXT("pc_body"), TEXT("ACT_AIM")), PcCatalog,
			BareReady);
		TestEqual(TEXT("an unarmed ACT_AIM is a named miss"),
			static_cast<int32>(BareReady.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MissingSequence));

		// A ladder whose every rung names something the body cannot play leaves the base alone, which
		// is the same answer retail's own empty table gives.
		FElysiumAnimationIntent Katana = ActivityIntent(TEXT("pc_body"), TEXT("ACT_RUN_RELAXED"));
		Katana.WeaponClassname = TEXT("item_w_katana");
		const FElysiumTranslationResult Melee = TranslateActivity(Katana, PcCatalog);
		TestEqual(TEXT("no katana rung is playable here"), Melee.WeaponRung, 0);
		TestEqual(TEXT("so the player rows answer the untranslated request"), Melee.Resolved,
			FString(TEXT("ACT_RUN")));

		// A weapon classname the ledger does not carry is not silently mapped to a cousin table.
		FElysiumAnimationIntent Unknown = Relaxed;
		Unknown.WeaponClassname = TEXT("item_w_sw_m64");
		TestEqual(TEXT("an unrecovered weapon classname translates nothing"),
			TranslateActivity(Unknown, PcCatalog).Resolved, FString(TEXT("ACT_WALK")));

		// **The cast's chain is not the player's.** `ACT_WALK_RELAXED` on a cast body reaches no
		// player row, so it survives to the availability probe and is answered at rung 4 — the
		// original request — only if the body carries it. This one does not, and the miss is named.
		FElysiumAnimationIntent CastRelaxed = ActivityIntent(TEXT("cast_body"),
			TEXT("ACT_WALK_RELAXED"), EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		const FElysiumTranslationResult CastWalk = TranslateActivity(CastRelaxed, CastCatalog);
		TestEqual(TEXT("no class body is reached without an actor classname"),
			CastWalk.PreTranslation, FString(TEXT("ACT_WALK_RELAXED")));
		TestEqual(TEXT("and nothing the ladder named is playable"), CastWalk.AvailabilityRung, 0);
		TestEqual(TEXT("so the record names what the translation produced"), CastWalk.Resolved,
			FString(TEXT("ACT_WALK_RELAXED")));

		// The recovered last resort is keyed on the ORIGINAL request. `ACT_SNEAK` is not on the cast
		// fixture and is not `ACT_RUN`, so it stays a miss; `ACT_RUN` would have taken `ACT_WALK`.
		FElysiumAnimationIntent CastSneak = ActivityIntent(TEXT("cast_body"), TEXT("ACT_SNEAK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		const FElysiumTranslationResult Sneak = TranslateActivity(CastSneak, CastCatalog);
		TestFalse(TEXT("a missing sneak does not take the run fallback"), Sneak.bRunToWalk);

		// A caller whose contract predates the ladder gets the miss, not a substitution.
		FElysiumAnimationIntent NoLadder = CastRelaxed;
		NoLadder.Activity = TEXT("ACT_RUN");
		NoLadder.bAllowFallbackLadder = false;
		Cast.Clips.Remove(TEXT("run"));
		TestFalse(TEXT("with the ladder refused, a missing run does not become a walk"),
			TranslateActivity(NoLadder, CastCatalog).bRunToWalk);
		NoLadder.bAllowFallbackLadder = true;
		const FElysiumTranslationResult RunFallback = TranslateActivity(NoLadder, CastCatalog);
		TestTrue(TEXT("and with it allowed, the recovered ACT_RUN fallback takes ACT_WALK"),
			RunFallback.bRunToWalk);
		TestEqual(TEXT("naming ACT_WALK"), RunFallback.Resolved, FString(TEXT("ACT_WALK")));
		Cast.Clips.Add(TEXT("run"), MakeClip(CastBank, TEXT("ACT_RUN"), 30, 0x1));
	}

	// --- The armed/alert branch, over the slice the decode confirms ---------------------------------
	{
		// `CNPC_VHuman`'s pre-translation chooses between the body's alert set and its relaxed one.
		// The fixture body carries the shared pistol relaxed walk and no plain `ACT_WALK`, so which
		// arm fired is readable straight off the resolved activity.
		FElysiumNpcClipSet Guard;
		Guard.Stem = TEXT("guard_body");
		Guard.Clips.Add(TEXT("pistol_relaxed_walk"),
			MakeClip(CastBank, TEXT("ACT_WALK_RELAXED_PISTOL"), 30, 0x1, 46));
		Guard.Clips.Add(TEXT("glock_walk"), MakeClip(CastBank, TEXT("ACT_WALK_GLOCK"), 30, 0x1, 46));
		FElysiumAnimationCatalog GuardCatalog;
		GuardCatalog.Clips = &Guard;
		GuardCatalog.BlendTableFor = Tables;

		FElysiumAnimationIntent Armed = ActivityIntent(TEXT("guard_body"), TEXT("ACT_WALK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		Armed.ActorClassname = TEXT("npc_VHumanCombatant");
		Armed.WeaponClassname = TEXT("item_w_glock_17c");

		// Idle: every override the tree reads above the state is a flag nothing here can set, so the
		// walk falls to the tail and the body stands in its relaxed set.
		Armed.ActorState = EElysiumNpcState::Idle;
		const ElysiumAnimResolve::FElysiumTranslationResult Relaxed =
			ElysiumAnimResolve::TranslateActivity(Armed, GuardCatalog);
		TestEqual(TEXT("an idle armed body walks relaxed"), Relaxed.Resolved,
			FString(TEXT("ACT_WALK_RELAXED_PISTOL")));

		// Alert: `m_NPCState == NPC_STATE_ALERT` sets the flag unconditionally, so the same request
		// resolves the weapon's own combat-stance walk instead.
		Armed.ActorState = EElysiumNpcState::Alert;
		const ElysiumAnimResolve::FElysiumTranslationResult Alerted =
			ElysiumAnimResolve::TranslateActivity(Armed, GuardCatalog);
		TestEqual(TEXT("the same body alert walks in its weapon's stance"), Alerted.Resolved,
			FString(TEXT("ACT_WALK_GLOCK")));

		// The gait alone cannot prove the ALERT arm fired — the armed/alert rows rewrite turns and
		// idle, never walk, so an unanswered branch reaches the same walk by doing nothing. A turn
		// is what separates them: only the armed/alert arm has an `_ALERT` form.
		FElysiumAnimationIntent Turning = Armed;
		Turning.Activity = TEXT("ACT_TURN_LEFT");
		TestEqual(TEXT("an alert body turns in its alert form"),
			ElysiumAnimResolve::TranslateActivity(Turning, GuardCatalog).PreTranslation,
			FString(TEXT("ACT_TURN_LEFT_ALERT")));
		Turning.ActorState = EElysiumNpcState::Idle;
		TestEqual(TEXT("and an idle one turns ordinarily"),
			ElysiumAnimResolve::TranslateActivity(Turning, GuardCatalog).PreTranslation,
			FString(TEXT("ACT_TURN_LEFT")));

		// **Combat answers neither arm, and that is the recorded absence.** The combat rung ends in
		// a ConVar whose default no static read of the image recovers, so the request stays
		// untranslated rather than being guessed either way.
		Armed.ActorState = EElysiumNpcState::Combat;
		const ElysiumAnimResolve::FElysiumTranslationResult Fighting =
			ElysiumAnimResolve::TranslateActivity(Armed, GuardCatalog);
		TestEqual(TEXT("a body in combat takes neither arm"), Fighting.PreTranslation,
			FString(TEXT("ACT_WALK")));

		// Empty hands take the tree's own early-out, before any state is read.
		FElysiumAnimationIntent Bare = Armed;
		Bare.WeaponClassname.Reset();
		Bare.ActorState = EElysiumNpcState::Alert;
		TestEqual(TEXT("an unarmed body is never armed/alert, whatever its state"),
			ElysiumAnimResolve::TranslateActivity(Bare, GuardCatalog).PreTranslation,
			FString(TEXT("ACT_WALK_RELAXED")));

		// **The availability probe is part of the translation, not an opt-in.** That unarmed rewrite
		// runs whatever the body can play, so a cast body carrying only the plain `ACT_WALK` — which is
		// most of the shipped cast — depends on rung 4 of the probe, the original request, to travel at
		// all. A producer that resolved with the probe off would get the named miss and its own stated
		// fallback where retail poses the walk the body actually authors.
		FElysiumNpcClipSet Plain;
		Plain.Stem = TEXT("plain_body");
		Plain.Clips.Add(TEXT("walk"), MakeClip(CastBank, TEXT("ACT_WALK"), 30, 0x1));
		FElysiumAnimationCatalog PlainCatalog;
		PlainCatalog.Clips = &Plain;
		PlainCatalog.BlendTableFor = Tables;

		FElysiumAnimationIntent PlainWalk = ActivityIntent(TEXT("plain_body"), TEXT("ACT_WALK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		PlainWalk.ActorClassname = TEXT("npc_VHumanCombatant");
		const ElysiumAnimResolve::FElysiumTranslationResult PlainTranslation =
			ElysiumAnimResolve::TranslateActivity(PlainWalk, PlainCatalog);
		TestEqual(TEXT("the unarmed rewrite names a relaxed walk this body cannot play"),
			PlainTranslation.PreTranslation, FString(TEXT("ACT_WALK_RELAXED")));
		TestEqual(TEXT("so the probe answers at rung 4, the original request"),
			PlainTranslation.AvailabilityRung, 4);
		TestEqual(TEXT("...which is the plain walk"), PlainTranslation.Resolved,
			FString(TEXT("ACT_WALK")));

		FElysiumAnimationSelection PlainClip;
		ElysiumAnimResolve::Resolve(PlainWalk, PlainCatalog, PlainClip);
		TestEqual(TEXT("and the body poses the walk it authors"), PlainClip.SequenceLabel,
			FString(TEXT("walk")));
		TestEqual(TEXT("...recorded as the rung that answered rather than as a clean resolve"),
			static_cast<int32>(PlainClip.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::TranslatedFallback));

		// LIFE5 — the same classification taken all the way to a CLIP. What a producer plays is the
		// translated label, never the raw request's own weighted pick, so a seam that dropped the
		// classname, the weapon or the state would pose a different body's walk with nothing said.
		Armed.ActorState = EElysiumNpcState::Alert;
		FElysiumAnimationSelection AlertClip;
		ElysiumAnimResolve::Resolve(Armed, GuardCatalog, AlertClip);
		TestEqual(TEXT("an alert armed body plays its weapon's own walk"),
			AlertClip.SequenceLabel, FString(TEXT("glock_walk")));

		Armed.ActorState = EElysiumNpcState::Idle;
		FElysiumAnimationSelection RelaxedClip;
		ElysiumAnimResolve::Resolve(Armed, GuardCatalog, RelaxedClip);
		TestEqual(TEXT("and the same request, same body, relaxed plays the shared pistol walk"),
			RelaxedClip.SequenceLabel, FString(TEXT("pistol_relaxed_walk")));

		// State alone is not what made the difference: with no classification stated at all the class
		// body never runs, `ACT_WALK` reaches a vocabulary carrying no untranslated walk, and the
		// answer is the named miss rather than either of the two above.
		FElysiumAnimationIntent Unclassified = ActivityIntent(TEXT("guard_body"), TEXT("ACT_WALK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		Unclassified.bAllowFallbackLadder = false;
		FElysiumAnimationSelection NoClassification;
		ElysiumAnimResolve::Resolve(Unclassified, GuardCatalog, NoClassification);
		TestTrue(TEXT("a request stating no classification resolves no clip at all"),
			NoClassification.SequenceLabel.IsEmpty());
		TestFalse(TEXT("...and says so rather than posing something"), NoClassification.IsResolved());
	}

	// --- CCC10 — the activity-keyed upper-body path, reached by weapon translation -------------------
	{
		// The bake-time bound path is asserted above (the "layer binding keeps its declaration
		// order" block); this is the OTHER producer — an intent naming the ordinary player attack
		// activity, translated per weapon family, never a host the bake wired by itself.
		FElysiumAnimationIntent Attack;
		Attack.Stem = TEXT("pc_body");
		Attack.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
		Attack.WeaponClassname = TEXT("item_w_glock_17c");
		Attack.Route = EElysiumAnimRoute::Activity;
		Attack.Channel = EElysiumAnimChannel::UpperBody;
		Attack.Source = EElysiumAnimSource::Debug;
		Attack.BodyKind = EElysiumAnimBodyKind::Player;

		FElysiumAnimationSelection Fired;
		ElysiumAnimResolve::Resolve(Attack, PcCatalog, Fired);
		TestEqual(TEXT("the ordinary attack activity translates per weapon family"),
			Fired.WeaponActivity, FString(TEXT("ACT_RANGE_ATTACK_LAYER_GLOCK")));
		TestEqual(TEXT("and resolves the family's own sequence"), Fired.SequenceLabel,
			FString(TEXT("glock_attack_layer")));
		TestEqual(TEXT("resolved, not one of the fallback rungs"),
			static_cast<int32>(Fired.Outcome), static_cast<int32>(EElysiumAnimOutcome::Resolved));

		// An unarmed body — or any family this rung has not seeded — has no override row, exactly
		// like retail's own empty table, and the untranslated activity is simply not in the
		// vocabulary.
		FElysiumAnimationIntent Unarmed = Attack;
		Unarmed.WeaponClassname.Reset();
		FElysiumAnimationSelection Missed;
		ElysiumAnimResolve::Resolve(Unarmed, PcCatalog, Missed);
		TestEqual(TEXT("with no weapon the activity passes through untranslated"),
			Missed.WeaponActivity, FString(TEXT("ACT_RANGE_ATTACK1_LAYER")));
		TestEqual(TEXT("and misses, because the fixture carries no such clip"),
			static_cast<int32>(Missed.Outcome), static_cast<int32>(EElysiumAnimOutcome::MissingSequence));
	}

	// --- CCC10 — AimYaw/AimPitch pass through the resolver unmodified, on any channel ----------------
	{
		// The player's own producer is what pins `AimYaw` at the literal 0.0f
		// (`docs/vtmb/animation_and_movers.md`); the resolver's job is only to carry whatever it was
		// handed through to step 6 without deriving or clamping it. Asserted with a NON-zero yaw
		// precisely so this cannot pass by coincidentally matching the producer's own default.
		FElysiumAnimationIntent Aiming = ActivityIntent(TEXT("pc_body"), TEXT("ACT_IDLE"));
		Aiming.Channel = EElysiumAnimChannel::UpperBody;
		Aiming.AimYaw = 27.0f;
		Aiming.AimPitch = -12.5f;
		FElysiumAnimationSelection AimSelection;
		ElysiumAnimResolve::Resolve(Aiming, PcCatalog, AimSelection);
		TestEqual(TEXT("aim yaw passes through unmodified"), AimSelection.AimYaw, 27.0f);
		TestEqual(TEXT("aim pitch passes through unmodified"), AimSelection.AimPitch, -12.5f);
	}

	// --- Layer host: standing sequence first, table fallback, a miss names each form -------------
	{
		FElysiumBlendTable Table;
		FElysiumAutoLayerBinding AimIdleLayers;
		AimIdleLayers.Clips = { TEXT("glock_aim_layer") };
		Table.AutoLayers.Add(TEXT("aim_idle"), AimIdleLayers);
		FElysiumAutoLayerBinding IdleLayers;
		IdleLayers.Clips = { TEXT("glock_aim_layer") };
		Table.AutoLayers.Add(TEXT("idle"), IdleLayers);
		FElysiumAutoLayerBinding WalkLayers;
		WalkLayers.Clips = { TEXT("glock_aim_layer"), TEXT("katana_bobble_layer") };
		Table.AutoLayers.Add(TEXT("walk"), WalkLayers);

		TestEqual(TEXT("the standing sequence is the host, even when a sorted table would pick another"),
			ElysiumAnimResolve::ResolveLayerHost(TEXT("walk"), &Table, TEXT("glock_aim_layer")),
			FString(TEXT("walk")));
		TestEqual(TEXT("an empty standing sequence falls back to the first sorted declaring host"),
			ElysiumAnimResolve::ResolveLayerHost(FString(), &Table, TEXT("glock_aim_layer")),
			FString(TEXT("aim_idle")));
		TestEqual(TEXT("a standing sequence is the host even when the table does not declare it"),
			ElysiumAnimResolve::ResolveLayerHost(TEXT("run"), &Table, TEXT("glock_aim_layer")),
			FString(TEXT("run")));
		TestTrue(TEXT("table fallback is empty when no host declares the layer"),
			ElysiumAnimResolve::ResolveLayerHost(FString(), &Table, TEXT("missing_layer")).IsEmpty());

		TArray<FString> Hosts;
		ElysiumAnimResolve::CollectDeclaringHosts(&Table, TEXT("glock_aim_layer"), Hosts);
		TestEqual(TEXT("declaring hosts are sorted"), Hosts.Num(), 3);
		if (Hosts.Num() == 3)
		{
			TestEqual(TEXT("first sorted host is aim_idle"), Hosts[0], FString(TEXT("aim_idle")));
			TestEqual(TEXT("then idle"), Hosts[1], FString(TEXT("idle")));
			TestEqual(TEXT("then walk"), Hosts[2], FString(TEXT("walk")));
		}

		const FString Miss = ElysiumAnimResolve::DescribeLayerAssetMiss(
			TEXT("glock_aim_layer"), TEXT("move_and_ranged_male"), TEXT("walk"), &Table);
		TestTrue(TEXT("a miss names the label"), Miss.Contains(TEXT("'glock_aim_layer'")));
		TestTrue(TEXT("and the owner"), Miss.Contains(TEXT("move_and_ranged_male")));
		TestTrue(TEXT("and the derived form"), Miss.Contains(TEXT("glock_aim_layer@walk")));
		TestTrue(TEXT("and the host that was tried"), Miss.Contains(TEXT("walk")));
		const FString NoHost = ElysiumAnimResolve::DescribeLayerAssetMiss(
			TEXT("glock_aim_layer"), TEXT("move_and_ranged_male"), FString(), &Table);
		TestTrue(TEXT("an empty host is named as a table miss"),
			NoHost.Contains(TEXT("was not found in the owner's autolayer table")));

		// A host outside the binding is a different failure from a host inside it whose derived form
		// is missing: no bake would ever produce `glock_aim_layer@run`, so the line has to say the
		// host does not declare the layer and name the ones that do, or the reader goes looking for
		// an asset that was never meant to exist.
		const FString Undeclared = ElysiumAnimResolve::DescribeLayerAssetMiss(
			TEXT("glock_aim_layer"), TEXT("move_and_ranged_male"), TEXT("run"), &Table);
		TestTrue(TEXT("an undeclared host is named as such"),
			Undeclared.Contains(TEXT("'run' does not declare it")));
		TestTrue(TEXT("and the hosts that do declare the layer are listed"),
			Undeclared.Contains(TEXT("aim_idle, idle, walk")));

		TestEqual(TEXT("a derived sequence is reported as the derived form"),
			ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::DerivedSequence,
				TEXT("glock_aim_layer"), TEXT("walk")),
			FString(TEXT("derived form 'glock_aim_layer@walk'")));
		TestEqual(TEXT("a plain sequence is reported as the plain-label fallback"),
			ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::PlainSequence,
				TEXT("glock_aim_layer"), TEXT("walk")),
			FString(TEXT("plain-label fallback 'glock_aim_layer'")));
		TestEqual(TEXT("a derived grid is reported as such"),
			ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::DerivedGrid,
				TEXT("glock_aim_layer"), TEXT("idle")),
			FString(TEXT("derived grid 'glock_aim_layer'@'idle'")));
		TestEqual(TEXT("a miss reports nothing"),
			ElysiumAnimResolve::DescribeLayerArmedForm(
				ElysiumAnimResolve::ELayerAssetForm::None, FString(), FString()),
			FString(TEXT("nothing")));
	}

	// --- LIFE5 — the directional hit fan, as the PAIR its angle sits between ---------------------
	//
	// The shipped shape, reproduced exactly: a nine-cell fan bound to a SECOND declared pose
	// parameter named `hit_yaw`. Two rules meet here — the parameter reaches the grid at all, and the
	// fan resolves to a floor cell plus a fraction to the next, which is what a blend space evaluates.
	// **One rule for every fan**: the same table's `move_yaw` grid is asserted the same way beside it,
	// so a resolver that grew a per-parameter special case would fail rather than merely be untested.
	{
		FElysiumPoseParamDesc HitYawParam;
		HitYawParam.Name = TEXT("hit_yaw");
		HitYawParam.Start = -180.0f;
		HitYawParam.End = 180.0f;
		HitYawParam.Loop = 360.0f;

		FElysiumBlendGrid HitFan;
		HitFan.Label = TEXT("hit_torso");
		HitFan.GroupSize[0] = 9;
		HitFan.GroupSize[1] = 1;
		// Index 1: the shipped sidecar declares `move_yaw` first and binds this fan to the second
		// parameter, so a resolver that assumed index 0 would steer a flinch by the walk direction.
		HitFan.ParamIndex[0] = 1;
		HitFan.ParamIndex[1] = INDEX_NONE;
		HitFan.ParamStart[0] = -180.0f;
		HitFan.ParamEnd[0] = 180.0f;
		static const TCHAR* const HitCells[9] = {
			TEXT("hit_torso"), TEXT("hit_torso_back_left"), TEXT("hit_torso_left"),
			TEXT("hit_torso_front_left"), TEXT("hit_torso_front"), TEXT("hit_torso_front_right"),
			TEXT("hit_torso_right"), TEXT("hit_torso_back_right"), TEXT("hit_torso")
		};
		for (int32 Index = 0; Index < 9; ++Index)
		{
			FElysiumBlendCell Cell;
			Cell.Axis[0] = Index;
			Cell.Axis[1] = 0;
			Cell.Clip = HitCells[Index];
			HitFan.Cells.Add(Cell);
		}

		FElysiumBlendTable HitTable;
		HitTable.Stem = MiscBank;
		HitTable.PoseParams.Add(MoveYawParam());
		HitTable.PoseParams.Add(HitYawParam);
		HitTable.Grids.Add(TEXT("hit_torso"), HitFan);
		// The same bank's locomotion fan, which the guard must leave flooring.
		HitTable.Grids.Add(TEXT("walk"), MakeFan(TEXT("walk"), TEXT("walk"), 200.0f, 254.0f));

		FElysiumNpcClipSet HitBody;
		HitBody.Stem = TEXT("hit_body");
		HitBody.Clips.Add(TEXT("hit_torso"), MakeClip(MiscBank, TEXT("ACT_HIT_TORSO"), 30, 0x0));
		HitBody.Clips.Add(TEXT("walk"), MakeClip(MiscBank, TEXT("ACT_WALK"), 30, 0x1));

		auto HitTables = [&HitTable](const FString& Owner) -> const FElysiumBlendTable*
		{
			return Owner.Equals(MiscBank, ESearchCase::IgnoreCase) ? &HitTable : nullptr;
		};
		FElysiumAnimationCatalog HitCatalog;
		HitCatalog.Clips = &HitBody;
		HitCatalog.BlendTableFor = HitTables;

		auto HitIntent = [](float HitYaw)
		{
			FElysiumAnimationIntent Intent = ActivityIntent(TEXT("hit_body"), TEXT("ACT_HIT_TORSO"),
				EElysiumAnimSource::Damage, EElysiumAnimBodyKind::Cast);
			Intent.HitYaw = HitYaw;
			return Intent;
		};

		// The parameter reaches the pose set at all, at its own value and under its own name.
		const FElysiumPoseParams Pose = ElysiumAnimResolve::PoseFrom(HitIntent(113.0f));
		TestEqual(TEXT("the intent's hit yaw reaches the pose parameters"),
			Pose.Get(TEXT("hit_yaw")), 113.0f);
		TestEqual(TEXT("and it does not disturb move_yaw"), Pose.Get(TEXT("move_yaw")), 0.0f);

		FElysiumAnimationSelection Hit;
		ElysiumAnimResolve::Resolve(HitIntent(0.0f), HitCatalog, Hit);
		TestEqual(TEXT("a hit from straight ahead resolves the fan"),
			static_cast<int32>(Hit.Outcome), static_cast<int32>(EElysiumAnimOutcome::Resolved));
		TestEqual(TEXT("...on the axis the sidecar's SECOND parameter declares"), Hit.AxisName[0],
			FString(TEXT("hit_yaw")));
		TestEqual(TEXT("...with one axis"), Hit.Axes, 1);
		TestEqual(TEXT("...owned by the bank the include DAG named"), Hit.OwnerStem,
			FString(MiscBank));
		TestEqual(TEXT("...and it is the forward cell, not the fan's -180 base"), Hit.AnimationName,
			FString(TEXT("hit_torso_front")));

		// A cell the angle lands EXACTLY on: the fan's own value, no second half to weigh. The pair is
		// still named, because the graph's blend space still samples between two samples and a record
		// that stopped naming the neighbour at a boundary would be describing a different asset there.
		ElysiumAnimResolve::Resolve(HitIntent(90.0f), HitCatalog, Hit);
		TestEqual(TEXT("a hit from the right lands exactly on cell 6"), Hit.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestEqual(TEXT("...with the next cell named"), Hit.NextAnimationName,
			FString(TEXT("hit_torso_back_right")));
		TestEqual(TEXT("...carrying none of its weight"), Hit.AxisFraction[0], 0.0f);
		TestEqual(TEXT("...and the axis value the producer asked for"), Hit.AxisValue[0], 90.0f);

		// The exact boundary. A nine-cell fan over 360 degrees is 45 degrees a cell, so the midpoint
		// between cells 6 and 7 is 112.5 — an EVEN MIX of the two named cells, which is exactly the
		// pose a snap-to-nearest rule cannot strike and the whole reason the branch plays a fan.
		ElysiumAnimResolve::Resolve(HitIntent(112.5f), HitCatalog, Hit);
		TestEqual(TEXT("the exact half-cell midpoint names the lower cell of the pair"),
			Hit.AnimationName, FString(TEXT("hit_torso_right")));
		TestEqual(TEXT("...and the upper one"), Hit.NextAnimationName,
			FString(TEXT("hit_torso_back_right")));
		TestTrue(TEXT("...as an even mix of the two"),
			FMath::IsNearlyEqual(Hit.AxisFraction[0], 0.5f, 0.001f));

		// Either side of it: the pair does not move, only its weight does. A rule that quantized would
		// name two different cells here and a fraction of zero on both.
		ElysiumAnimResolve::Resolve(HitIntent(113.0f), HitCatalog, Hit);
		TestEqual(TEXT("just past the midpoint the pair is unchanged"), Hit.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestTrue(TEXT("...and leans onto the upper cell"), Hit.AxisFraction[0] > 0.5f);
		ElysiumAnimResolve::Resolve(HitIntent(111.0f), HitCatalog, Hit);
		TestEqual(TEXT("just short of it the pair is the same again"), Hit.AnimationName,
			FString(TEXT("hit_torso_right")));
		TestTrue(TEXT("...leaning onto the lower cell"), Hit.AxisFraction[0] < 0.5f);

		// The wrap seam: `ResolveAxis` folds +180 onto -180, so a hit from directly behind lands on the
		// fan's BASE cell rather than on its last one — the two carry the same clip, which is what the
		// seam means. It sits exactly on a cell, so nothing is weighed onto its neighbour.
		ElysiumAnimResolve::Resolve(HitIntent(180.0f), HitCatalog, Hit);
		TestEqual(TEXT("a hit from directly behind takes the wrapped seam cell"), Hit.AnimationName,
			FString(TEXT("hit_torso")));
		TestEqual(TEXT("...with nothing weighed onto its neighbour"), Hit.AxisFraction[0], 0.0f);
		// Just inside the seam, which is where the fan's last pair actually is.
		ElysiumAnimResolve::Resolve(HitIntent(179.0f), HitCatalog, Hit);
		TestEqual(TEXT("just inside the seam the pair is the fan's last two cells"), Hit.AnimationName,
			FString(TEXT("hit_torso_back_right")));
		TestEqual(TEXT("...ending on the shared seam clip"), Hit.NextAnimationName,
			FString(TEXT("hit_torso")));

		ElysiumAnimResolve::Resolve(HitIntent(-90.0f), HitCatalog, Hit);
		TestEqual(TEXT("and one from the left takes cell 2"), Hit.AnimationName,
			FString(TEXT("hit_torso_left")));
		TestEqual(TEXT("...paired with cell 3"), Hit.NextAnimationName,
			FString(TEXT("hit_torso_front_left")));

		// **One rule for both fans.** The same table's `move_yaw` grid, sampled the same fraction past
		// a cell, answers in exactly the same shape — a floor cell, its neighbour, and the weight
		// between them. This used to be a guard asserting that the hit fan behaved DIFFERENTLY; it is
		// now the positive statement that it does not.
		FElysiumAnimationIntent Strafing = ActivityIntent(TEXT("hit_body"), TEXT("ACT_WALK"),
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		Strafing.Body.MoveYawVelocity = 115.0f;
		Strafing.Body.MoveYawPose = 115.0f;
		FElysiumAnimationSelection Strafe;
		ElysiumAnimResolve::Resolve(Strafing, HitCatalog, Strafe);
		TestEqual(TEXT("a move_yaw fan names its floor cell"), Strafe.AnimationName,
			FString(TEXT("walk_90")));
		TestEqual(TEXT("...on its own axis"), Strafe.AxisName[0], FString(TEXT("move_yaw")));
		TestEqual(TEXT("...paired with the next"), Strafe.NextAnimationName,
			FString(TEXT("walk_135")));
		TestTrue(TEXT("...weighted past the half-cell, exactly as the hit fan is"),
			Strafe.AxisFraction[0] > 0.5f);

		// --- the gesture path's cleared ladder ---------------------------------------------------
		//
		// A body with no reaction in its vocabulary at all, but with the disposition the ladder would
		// substitute. The two answers below are the whole point of the flag: retail's gesture call
		// walks no rung and animates nothing, and the activity chain walks every rung it has.
		FElysiumNpcClipSet NoReaction;
		NoReaction.Stem = TEXT("stoic_body");
		NoReaction.Clips.Add(TEXT("Stance_Normal_Idle_1"),
			MakeClip(TEXT("stances"), TEXT("ACT_DISPOSITION"), 30, 0x1));
		FElysiumAnimationCatalog StoicCatalog;
		StoicCatalog.Clips = &NoReaction;
		StoicCatalog.BlendTableFor = HitTables;

		FElysiumAnimationIntent Gesture = ActivityIntent(TEXT("stoic_body"), TEXT("ACT_HIT_TORSO"),
			EElysiumAnimSource::Damage, EElysiumAnimBodyKind::Cast);
		Gesture.bAllowFallbackLadder = false;
		FElysiumAnimationSelection GestureMiss;
		ElysiumAnimResolve::Resolve(Gesture, StoicCatalog, GestureMiss);
		TestEqual(TEXT("a reaction with the ladder cleared misses by name"),
			static_cast<int32>(GestureMiss.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MissingSequence));
		TestTrue(TEXT("...and substitutes nothing at all"), GestureMiss.SequenceLabel.IsEmpty());

		FElysiumAnimationIntent Laddered = Gesture;
		Laddered.bAllowFallbackLadder = true;
		FElysiumAnimationSelection Substituted;
		ElysiumAnimResolve::Resolve(Laddered, StoicCatalog, Substituted);
		TestEqual(TEXT("while the same request WITH the ladder retries as a disposition"),
			static_cast<int32>(Substituted.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Disposition));
		TestEqual(TEXT("...which is exactly the substitution the gesture path must not make"),
			Substituted.SequenceLabel, FString(TEXT("Stance_Normal_Idle_1")));

		// The seam's own default is the activity chain's, not the gesture path's: the ladder is part
		// of `CAI_BaseNPC`'s translation and every ordinary producer walks it.
		TestTrue(TEXT("an activity request allows the fallback ladder by default"),
			FElysiumActivityClipRequest().bAllowFallbackLadder);
		TestEqual(TEXT("and states a resting hit yaw"), FElysiumActivityClipRequest().HitYaw, 0.0f);
		// The default pair answers for ONE body: an NPC producer on the cast chain. A default naming
		// the player's one-pass translation under an `Npc` source would describe a body that does not
		// exist, and every producer states both explicitly anyway.
		TestEqual(TEXT("...and the default source/chain pair is the cast's"),
			static_cast<int32>(FElysiumActivityClipRequest().BodyKind),
			static_cast<int32>(EElysiumAnimBodyKind::Cast));
		TestEqual(TEXT("...paired with the NPC producer"),
			static_cast<int32>(FElysiumActivityClipRequest().Source),
			static_cast<int32>(EElysiumAnimSource::Npc));

		// --- The seam's own forwarding ------------------------------------------------------------
		//
		// The resolver-level rules above are proven; what is NOT proven by them is that the activity
		// seam carries a producer's request onto an intent whole. Both of the reaction fields are the
		// ones a dropped forward would hide: a lost `HitYaw` resolves every directional reaction at
		// the fan's forward cell, and a lost cleared ladder substitutes a stance for a missing hit
		// clip. Asserted as the pure mapping `UElysiumAnimSubsystem::ResolveActivityClip` is
		// expressed over, so no subsystem, game instance or export corpus is involved.
		{
			FElysiumActivityClipRequest Reaction;
			Reaction.Stem = TEXT("hit_body");
			Reaction.Activity = TEXT("ACT_HIT_TORSO");
			Reaction.Variant = 4;
			Reaction.Source = EElysiumAnimSource::Damage;
			Reaction.BodyKind = EElysiumAnimBodyKind::Cast;
			Reaction.ActorClassname = TEXT("npc_gangbanger_a");
			Reaction.WeaponClassname = TEXT("item_w_glock_17c");
			Reaction.ActorState = EElysiumNpcState::Combat;
			Reaction.HitYaw = -113.0f;
			Reaction.bAllowFallbackLadder = false;

			const FElysiumAnimationIntent Threaded =
				ElysiumAnimResolve::ActivityIntentFor(Reaction);
			TestEqual(TEXT("the seam threads the hit yaw onto the intent"),
				Threaded.HitYaw, -113.0f);
			TestFalse(TEXT("...and the cleared fallback ladder with it"),
				Threaded.bAllowFallbackLadder);
			// The rest of the key, because a forward that carried only the two reaction fields would
			// select for a different body than the producer's.
			TestEqual(TEXT("...the stem"), Threaded.Stem, FString(TEXT("hit_body")));
			TestEqual(TEXT("...the activity"), Threaded.Activity, FString(TEXT("ACT_HIT_TORSO")));
			TestEqual(TEXT("...the variant"), Threaded.Variant, 4);
			TestEqual(TEXT("...the producer"), static_cast<int32>(Threaded.Source),
				static_cast<int32>(EElysiumAnimSource::Damage));
			TestEqual(TEXT("...the chain"), static_cast<int32>(Threaded.BodyKind),
				static_cast<int32>(EElysiumAnimBodyKind::Cast));
			TestEqual(TEXT("...the actor classname"), Threaded.ActorClassname,
				FString(TEXT("npc_gangbanger_a")));
			TestEqual(TEXT("...the weapon classname"), Threaded.WeaponClassname,
				FString(TEXT("item_w_glock_17c")));
			TestEqual(TEXT("...and the alert/relaxed state"), static_cast<int32>(Threaded.ActorState),
				static_cast<int32>(EElysiumNpcState::Combat));
			// The route is the activity one, never the label's: this seam weighs candidates.
			TestEqual(TEXT("...over the activity route"), static_cast<int32>(Threaded.Route),
				static_cast<int32>(EElysiumAnimRoute::Activity));

			// And the threading is real end to end: a request resolved through the seam's own mapping
			// lands on the fan cell its angle names rather than on the forward one. Stated on a bare
			// request so the assertion is about the forwarding and not about a translation row.
			FElysiumActivityClipRequest Bare;
			Bare.Stem = TEXT("hit_body");
			Bare.Activity = TEXT("ACT_HIT_TORSO");
			Bare.Source = EElysiumAnimSource::Damage;
			Bare.BodyKind = EElysiumAnimBodyKind::Cast;
			Bare.HitYaw = -113.0f;
			Bare.bAllowFallbackLadder = false;
			FElysiumAnimationSelection Threading;
			ElysiumAnimResolve::Resolve(ElysiumAnimResolve::ActivityIntentFor(Bare), HitCatalog,
				Threading);
			TestEqual(TEXT("a threaded hit yaw steers the fan"), Threading.AnimationName,
				FString(TEXT("hit_torso_back_left")));
		}
	}

	return true;
}

// =====================================================================================
// LIFE5 — direction-keyed attack entry selection, the rung that sits AHEAD of the weighted draw.
//
// `docs/vtmb/combat-and-damage.md` § "Melee attack, combo, block and damage": the player selector at
// `0x10160F90` reads each candidate sequence's authored state mask (`mstudioseqdesc_t`+0x2D4) and
// compares it with the player's own button field reduced to `0x79A`, preferring an exact match, then
// the two partial classes, then the neutral mask-0 attack. Only when none of those answers does the
// activity's weighted draw decide.
//
// Everything below is the rule and its catalogue — no world, no weapon and no buttons — which is the
// only way "a held direction changes WHICH attack, and spends no randomness doing it" can be stated
// exactly.
// =====================================================================================

namespace
{
	FElysiumNpcClip MaskedAttack(int32 Mask, const TCHAR* Chain = TEXT(""), int32 Weight = 1)
	{
		FElysiumNpcClip Clip;
		Clip.Owner = TEXT("fists");
		Clip.Activity = TEXT("ACT_MELEE_ATTACK");
		Clip.Weight = Weight;
		Clip.Frames = 31;
		Clip.Fps = 30.0f;
		Clip.Combo.bStated = true;
		Clip.Combo.Mask = Mask;
		Clip.Combo.Chain = Chain;
		Clip.Combo.WindowOpen = 0.5f;
		Clip.Combo.WindowClose = 0.9f;
		Clip.Combo.HoldCycle = 0.91f;
		return Clip;
	}

	// A candidate answering the same activity that states no block at all — one of the 13,804.
	FElysiumNpcClip PlainAttack(int32 Weight)
	{
		FElysiumNpcClip Clip;
		Clip.Owner = TEXT("fists");
		Clip.Activity = TEXT("ACT_MELEE_ATTACK");
		Clip.Weight = Weight;
		Clip.Frames = 31;
		Clip.Fps = 30.0f;
		return Clip;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationStateMaskTest,
	"Elysium.Substrate.AnimationStateMask", GElysiumAnimationTestFlags)
bool FElysiumAnimationStateMaskTest::RunTest(const FString&)
{
	using namespace ElysiumCombo;

	FElysiumNpcClipSet Pc;
	Pc.Stem = TEXT("male_pc");
	// The five masks the whole install states, one clip each, plus the weight-30 candidate that would
	// win every draw and the plumbing sequence that answers nothing.
	Pc.Clips.Add(TEXT("fists_attack_JabLeft"), MaskedAttack(0, TEXT("fists_attack_longright")));
	Pc.Clips.Add(TEXT("Fists_attack_W1"), MaskedAttack(InForward, TEXT("Fists_attack_W2")));
	Pc.Clips.Add(TEXT("fists_attack_back"), MaskedAttack(InBack));
	Pc.Clips.Add(TEXT("fists_attack_left"), MaskedAttack(InMoveLeft));
	Pc.Clips.Add(TEXT("fists_attack_right"), MaskedAttack(InMoveRight));
	Pc.Clips.Add(TEXT("fists_attack_plain"), PlainAttack(30));
	// A second activity whose candidates author no mask at all — the control for "the maskless path
	// is unchanged".
	FElysiumNpcClip Idle;
	Idle.Owner = TEXT("misc");
	Idle.Activity = TEXT("ACT_IDLE");
	Idle.Weight = 30;
	Idle.Flags = 0x1;
	Pc.Clips.Add(TEXT("idle01"), Idle);
	FElysiumNpcClip Fidget = Idle;
	Fidget.Weight = 1;
	Fidget.Flags = 0;
	Pc.Clips.Add(TEXT("fidget01"), Fidget);

	FElysiumAnimationCatalog Catalog;
	Catalog.Clips = &Pc;

	const FString Attack = TEXT("ACT_MELEE_ATTACK");

	// --- The rank rule itself, boundary by boundary ------------------------------------------------
	TestTrue(TEXT("an unset mask is never a selection candidate"),
		RankStateMask(MaskUnset, InForward) == EStateMatch::None);
	TestTrue(TEXT("a stated 0 against no direction held is an EXACT match, not the fallback"),
		RankStateMask(0, 0) == EStateMatch::Exact);
	TestTrue(TEXT("the same mask as the state is exact"),
		RankStateMask(InForward, InForward) == EStateMatch::Exact);
	TestTrue(TEXT("a shared forward bit under a held jump is the directional partial"),
		RankStateMask(InForward, InForward | InJump) == EStateMatch::Directional);
	TestTrue(TEXT("a shared strafe bit is the strafe partial"),
		RankStateMask(InMoveLeft, InMoveLeft | InJump) == EStateMatch::Strafe);
	TestTrue(TEXT("the neutral attack is the fallback for a direction nothing answers"),
		RankStateMask(0, InLeft) == EStateMatch::Neutral);
	TestTrue(TEXT("a mask sharing nothing with the state is not selectable"),
		RankStateMask(InBack, InForward) == EStateMatch::None);
	// Everything outside `0x79A` is invisible to the comparison, which is what makes a held attack
	// button not a direction.
	TestTrue(TEXT("bits outside the selection mask are not part of the state"),
		RankStateMask(0, ~SelectionMask) == EStateMatch::Exact);

	// --- The five authored masks, end to end through the resolver ---------------------------------
	struct FCase { int32 State; const TCHAR* Label; const TCHAR* Why; };
	const FCase Cases[] = {
		{ 0,           TEXT("fists_attack_JabLeft"), TEXT("no direction held selects the mask-0 entry") },
		{ InForward,   TEXT("Fists_attack_W1"),      TEXT("forward selects the forward entry") },
		{ InBack,      TEXT("fists_attack_back"),    TEXT("back selects the back entry") },
		{ InMoveLeft,  TEXT("fists_attack_left"),    TEXT("strafe left selects the left entry") },
		{ InMoveRight, TEXT("fists_attack_right"),   TEXT("strafe right selects the right entry") },
	};
	for (const FCase& Case : Cases)
	{
		FElysiumAnimationIntent Intent = ActivityIntent(TEXT("male_pc"), *Attack);
		Intent.StateMask = Case.State;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		TestEqual(Case.Why, Out.SequenceLabel, FString(Case.Label));
	}

	// --- Exact beats partial, and forward/back beats strafe ---------------------------------------
	{
		FElysiumAnimationIntent Intent = ActivityIntent(TEXT("male_pc"), *Attack);
		Intent.StateMask = InForward | InJump;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		TestEqual(TEXT("a held jump makes the forward entry a PARTIAL match, and it still wins"),
			Out.SequenceLabel, FString(TEXT("Fists_attack_W1")));

		Intent.StateMask = InForward | InMoveLeft;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		TestEqual(TEXT("forward and strafe held together take the forward/back partial first"),
			Out.SequenceLabel, FString(TEXT("Fists_attack_W1")));

		// Nothing authored answers a keyboard-yaw key, so the neutral entry is what is left.
		Intent.StateMask = InLeft;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		TestEqual(TEXT("a direction no entry answers falls back to the neutral mask-0 entry"),
			Out.SequenceLabel, FString(TEXT("fists_attack_JabLeft")));
	}

	// --- The RNG pin: a mask-selected entry never reaches the draw ---------------------------------
	//
	// The weighted draw would answer `fists_attack_plain` — it carries thirty times the share of every
	// other candidate — so the fact that no state selects it is the whole assertion. And the pick is
	// invariant in the selection token: a draw moves with the variant, and this does not.
	{
		TestEqual(TEXT("the draw, left to itself, answers the heavy candidate"),
			ElysiumAnimResolve::PickWeighted(Pc, Attack, 0).Label,
			FString(TEXT("fists_attack_plain")));
		for (int32 Variant = 0; Variant < 8; ++Variant)
		{
			FElysiumAnimationIntent Intent = ActivityIntent(TEXT("male_pc"), *Attack);
			Intent.StateMask = InForward;
			Intent.Variant = Variant;
			FElysiumAnimationSelection Out;
			ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
			TestEqual(TEXT("a mask-selected entry does not move with the selection token"),
				Out.SequenceLabel, FString(TEXT("Fists_attack_W1")));
		}
	}

	// --- The maskless paths, unchanged ------------------------------------------------------------
	{
		// A body with no button field at all — every cast body — selects by weight exactly as before.
		FElysiumAnimationIntent Cast = ActivityIntent(TEXT("male_pc"), *Attack,
			EElysiumAnimSource::Npc, EElysiumAnimBodyKind::Cast);
		Cast.StateMask = INDEX_NONE;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Cast, Catalog, Out);
		TestEqual(TEXT("a body with no button field draws, and the mask column changes nothing"),
			Out.SequenceLabel, ElysiumAnimResolve::PickWeighted(Pc, Attack, 0).Label);

		// An activity none of whose candidates authors a mask falls straight through to the draw, even
		// with a direction held.
		FElysiumAnimationIntent Held = ActivityIntent(TEXT("male_pc"), TEXT("ACT_IDLE"));
		Held.StateMask = InForward;
		ElysiumAnimResolve::Resolve(Held, Catalog, Out);
		TestEqual(TEXT("an activity with no authored masks is decided by the draw as it always was"),
			Out.SequenceLabel, ElysiumAnimResolve::PickWeighted(Pc, TEXT("ACT_IDLE"), 0).Label);

		// And the selection function says so directly: empty is "nothing here is direction-keyed".
		TestTrue(TEXT("the mask pick answers nothing for an unmasked activity"),
			ElysiumAnimResolve::PickByStateMask(Pc, TEXT("ACT_IDLE"), InForward).IsEmpty());
		TestTrue(TEXT("...and nothing for a body with no button field"),
			ElysiumAnimResolve::PickByStateMask(Pc, Attack, INDEX_NONE).IsEmpty());
	}

	// --- The PLAYER arm of the melee selector: no mask, no answer ---------------------------------
	//
	// Retail selects a melee attack sequence through vtable slot 331 on the OWNER, and the arms are
	// different systems: `CBasePlayer` (`0x10160F90`) matches authored button masks and seeds its
	// answer with -1, returning `answer >= 0`; `CBaseCombatCharacter::ChooseMeleeAttackSequence`
	// (`0x10347180`) scores candidates geometrically and never reads a mask. So an activity whose
	// candidates author NO mask is unanswerable on the player arm and ordinary on the cast arm.
	//
	// This is the measured shape of the shipped `ACT_MELEE_ATTACK_2COMBO_<FAMILY>` family: none of its
	// clips carries a mask, and 43 of 43 player presses at Melee 5 lost the combo to exactly this.
	{
		// `ACT_IDLE` stands in for the shape — its candidates author no masks, same as the 2COMBO set.
		FElysiumAnimationIntent Player = ActivityIntent(TEXT("male_pc"), TEXT("ACT_IDLE"));
		Player.StateMask = InForward;
		Player.bRequireStateMask = true;
		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Player, Catalog, Out);
		TestTrue(TEXT("the player arm refuses an activity no candidate masks"),
			Out.SequenceLabel.IsEmpty());
		TestTrue(TEXT("...and says so as a named miss rather than a silent substitution"),
			Out.Outcome == EElysiumAnimOutcome::MissingSequence);

		// The same request WITHOUT the flag is the cast arm, and it draws as it always did. One field
		// is the whole difference, which is what makes the two arms one seam rather than two paths.
		FElysiumAnimationIntent Cast = ActivityIntent(TEXT("male_pc"), TEXT("ACT_IDLE"));
		Cast.StateMask = InForward;
		ElysiumAnimResolve::Resolve(Cast, Catalog, Out);
		TestEqual(TEXT("the cast arm still draws the same activity"),
			Out.SequenceLabel, ElysiumAnimResolve::PickWeighted(Pc, TEXT("ACT_IDLE"), 0).Label);

		// A masked activity is answered on the player arm exactly as before: the flag refuses a
		// FALLBACK, never a selection.
		FElysiumAnimationIntent Masked = ActivityIntent(TEXT("male_pc"), *Attack);
		Masked.StateMask = InForward;
		Masked.bRequireStateMask = true;
		ElysiumAnimResolve::Resolve(Masked, Catalog, Out);
		TestEqual(TEXT("a direction-keyed attack is unaffected by the flag"),
			Out.SequenceLabel, FString(TEXT("Fists_attack_W1")));
	}

	// --- The busy predicate's three arms -----------------------------------------------------------
	{
		TestTrue(TEXT("the ordinary attack takes the authored-hold arm"),
			BusyArmFor(TEXT("ACT_MELEE_ATTACK")) == EBusyArm::Hold);
		TestTrue(TEXT("the 2COMBO family is busy for its whole clip"),
			BusyArmFor(TEXT("ACT_MELEE_ATTACK_2COMBO")) == EBusyArm::WholeClip);
		TestTrue(TEXT("...and so are heavy and air"),
			BusyArmFor(TEXT("ACT_MELEE_ATTACK_HEAVY")) == EBusyArm::WholeClip
			&& BusyArmFor(TEXT("ACT_MELEE_AIR_ATTACK")) == EBusyArm::WholeClip);
		TestTrue(TEXT("the block family reads the clock alone"),
			BusyArmFor(TEXT("ACT_BLOCK")) == EBusyArm::Clock
			&& BusyArmFor(TEXT("ACT_BLOCK_HEAVY")) == EBusyArm::Clock
			&& BusyArmFor(TEXT("ACT_PREBLOCK")) == EBusyArm::Clock);
		TestTrue(TEXT("anything else is not a busy family"),
			BusyArmFor(TEXT("ACT_IDLE")) == EBusyArm::None);

		// The hold arm: the per-sequence value, read and never derived.
		TestTrue(TEXT("an ordinary attack is busy below its own hold"),
			IsBusy(TEXT("ACT_MELEE_ATTACK"), 0.90f, 0.91f, 0.0, 0.0));
		TestFalse(TEXT("...and free at it"),
			IsBusy(TEXT("ACT_MELEE_ATTACK"), 0.91f, 0.91f, 0.0, 0.0));
		// `katana_running_attack`: the hold releases at 0.9 while the window stays open to 1.0.
		TestFalse(TEXT("a hold below the window's close releases before the window does"),
			IsBusy(TEXT("ACT_MELEE_ATTACK"), 0.95f, 0.9f, 0.0, 0.0));
		TestTrue(TEXT("the whole-clip arm holds to the last frame"),
			IsBusy(TEXT("ACT_MELEE_ATTACK_2COMBO"), 0.99f, 0.5f, 0.0, 0.0));
		TestFalse(TEXT("...and releases at it"),
			IsBusy(TEXT("ACT_MELEE_ATTACK_2COMBO"), 1.0f, 0.5f, 0.0, 0.0));
		TestTrue(TEXT("the block arm reads the deadline and ignores the cycle"),
			IsBusy(TEXT("ACT_BLOCK"), 0.999f, 0.0f, 1.0, 2.0));
		TestFalse(TEXT("...on both sides of it"),
			IsBusy(TEXT("ACT_BLOCK"), 0.0f, 1.0f, 3.0, 2.0));
	}
	return true;
}

// =====================================================================================
// CCC5 — the graph's own two rules: which state realizes a selection, and how long the transition
// into it lasts.
//
// Both are content-free, and deliberately so. The transition duration is the one number the
// authored graph asset may NOT carry — `Content/ElysiumAuthored/README.md` forbids encoding
// game-derived timings in a tracked package — so it arrives at runtime from the clip's own record,
// and this is where the combine is proven rather than in the asset.
// =====================================================================================

namespace
{
	// A resolved selection carrying one authored fade.
	FElysiumAnimationSelection Faded(float FadeSeconds, bool bSnap = false)
	{
		FElysiumAnimationSelection S;
		S.Outcome = EElysiumAnimOutcome::Resolved;
		S.FadeSeconds = FadeSeconds;
		S.bSnap = bSnap;
		return S;
	}

	int32 AsInt(EElysiumGraphState State) { return static_cast<int32>(State); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationGraphTest,
	"Elysium.Substrate.AnimationGraph", GElysiumAnimationTestFlags)
bool FElysiumAnimationGraphTest::RunTest(const FString&)
{
	using namespace ElysiumAnimGraph;

	// --- The transition combine -------------------------------------------------------------------
	{
		// Retail's rule is `max`, not "the incoming clip's". The 0.2/0.3 pair is the one that
		// actually occurs in this slice: `idle01` on a player body authors 0.3 while walk, run,
		// sneak and crouch all author 0.2, so a graph that took either side alone would be wrong on
		// every transition into and out of idle.
		const FElysiumAnimationSelection Walk = Faded(0.2f);
		const FElysiumAnimationSelection Idle = Faded(0.3f);
		TestEqual(TEXT("walk -> idle takes idle's longer authored fade"),
			TransitionSeconds(&Walk, Idle), 0.3f);
		TestEqual(TEXT("and idle -> walk takes it too, because the combine is max and not incoming"),
			TransitionSeconds(&Idle, Walk), 0.3f);

		// The lying-down and damaged stance idles, the corpus's longest. It reaches the blend
		// stack's `BlendTime` pin at exactly this value: the graph carries no authored duration of
		// its own, so there is nothing left that could min-merge it down to a cap.
		const FElysiumAnimationSelection Long = Faded(0.5f);
		TestEqual(TEXT("the longest authored pair reaches the graph unclamped"),
			TransitionSeconds(&Long, Long), 0.5f);

		// A fresh body has nothing to fade FROM, and retail refuses that outright: `FUN_1008de30`'s
		// first gate is `!out || !in || (in->flags & 0x2)`, so a missing outgoing descriptor ranks
		// with the hard cut. Fading instead is the T-pose defect — the un-entered state machine poses
		// the bind pose, so a non-zero answer inertializes the first clip of every map up out of it.
		TestEqual(TEXT("a body that has played nothing has nothing to fade from, so it snaps"),
			TransitionSeconds(nullptr, Idle), 0.0f);

		// `flags & 0x2`, the most common authored transition behaviour in the corpus, and a property
		// of the clip being ENTERED rather than of whatever is running.
		const FElysiumAnimationSelection Snap = Faded(0.3f, /*bSnap=*/true);
		TestEqual(TEXT("an incoming hard cut overrides the pair entirely"),
			TransitionSeconds(&Long, Snap), 0.0f);
		TestEqual(TEXT("and it is the incoming clip's property, so an outgoing snap does not cut"),
			TransitionSeconds(&Snap, Long), 0.5f);

		// The two refusals are independent operands of one gate, so neither may stand in for the
		// other's absence: a hard cut arriving on a body that has played nothing still answers zero.
		TestEqual(TEXT("both refusals at once still answer zero"),
			TransitionSeconds(nullptr, Snap), 0.0f);
	}

	// --- The state projection ---------------------------------------------------------------------
	{
		// The relaxed forms are what the classifier emits before translation, and a player body
		// carries no sequence for either — but they are the same gait and so the same state.
		TestEqual(TEXT("ACT_WALK_RELAXED is the walk state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::WalkRelaxed)),
			AsInt(EElysiumGraphState::Walk));
		TestEqual(TEXT("ACT_RUN_RELAXED is the run state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::RunRelaxed)),
			AsInt(EElysiumGraphState::Run));

		// The jump chain the controlled corpus recorded: ACT_LEAP, then ACT_FALLING, then a moving
		// gait or ACT_LAND. It does not visit ACT_LEAP_ASCEND or ACT_LEAP_DESCEND, and neither does
		// this projection.
		TestEqual(TEXT("phase 1 is the leap state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Leap)), AsInt(EElysiumGraphState::Leap));
		TestEqual(TEXT("phase 7 is the falling state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Falling)),
			AsInt(EElysiumGraphState::Falling));
		TestEqual(TEXT("phase 8 standing is the land state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Land)), AsInt(EElysiumGraphState::Land));

		// The one declared answer: ACT_LAND_CROUCH resolves nothing on a validated player body, so
		// the graph says where the body stands instead of the resolver inventing a clip.
		TestEqual(TEXT("a ducked landing is declared onto the land state"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::LandCrouch)),
			AsInt(EElysiumGraphState::Land));

		// --- the repeat rule: a held stance replays, an event does not -------------------------
		// Retail holds a sustained unarmed crouch by reselecting sequence 8 once its finished flag
		// is set, which repeated is a loop. Freezing on the terminal frame is recorded as **not
		// faithful**, so this is the defect fix rather than a choice.
		using ElysiumAnimGraph::ShouldRepeatClip;
		TestTrue(TEXT("a held crouch repeats its non-looping into-pose"),
			ShouldRepeatClip(EElysiumGraphState::Crouch, /*bAuthoredLooping*/ false));
		// And the two one-shots that END must not be caught by it: a looping landing never reports
		// complete, and the latch would hold the body in phase 8 forever.
		TestFalse(TEXT("a landing does not repeat"),
			ShouldRepeatClip(EElysiumGraphState::Land, false));
		TestFalse(TEXT("a leap does not repeat"),
			ShouldRepeatClip(EElysiumGraphState::Leap, false));
		// Everything else is the model's own bit, unchanged in both directions.
		TestTrue(TEXT("an authored loop still loops"),
			ShouldRepeatClip(EElysiumGraphState::Walk, true));
		TestFalse(TEXT("and a non-looping ordinary state still does not"),
			ShouldRepeatClip(EElysiumGraphState::Idle, false));

		// --- the three rules that each cost a visible defect once --------------------------------
		{
			using ElysiumAnimGraph::IsPlayableRemaining;
			using ElysiumAnimGraph::ShouldHoldPose;
			using ElysiumAnimGraph::OneShotStateFor;
			using EOne = EElysiumOneShotState;

			// **`MAX_flt` is a refusal, not a long clip.** Read as a duration it reports a clip as
			// playing forever, which parks whatever waits on it — the body sat in ACT_LAND for
			// fourteen seconds before this was bounded. The bound holds for any producer, because
			// nothing honest exceeds the clip it belongs to.
			TestFalse(TEXT("MAX_flt is a refusal, not a remaining time"),
				IsPlayableRemaining(MAX_flt, 1.5f));
			TestFalse(TEXT("and so is anything past the clip's own length"),
				IsPlayableRemaining(1.6f, 1.5f));
			TestFalse(TEXT("a negative remaining answers nothing"), IsPlayableRemaining(-1.0f, 1.5f));
			TestFalse(TEXT("a clip with no length answers nothing rather than 'finished'"),
				IsPlayableRemaining(0.0f, 0.0f));
			TestTrue(TEXT("a real remaining time is an answer"), IsPlayableRemaining(0.4f, 1.5f));
			TestTrue(TEXT("and zero remaining on a real clip is 'finished'"),
				IsPlayableRemaining(0.0f, 1.5f));

			// **An unresolved request holds the pose it has**, which is what retail does: a failed
			// selection never reaches ResetSequenceInfo. Projecting it leaves an asset pin null, and
			// a sequence player with no asset evaluates to the bind pose — the T-pose.
			TestTrue(TEXT("a request that resolved nothing holds the pose it has"),
				ShouldHoldPose(/*bHasAppliedOnce*/ true, false, false));
			TestFalse(TEXT("a resolved sequence is published"), ShouldHoldPose(true, true, false));
			TestFalse(TEXT("a resolved blend space is published"), ShouldHoldPose(true, false, true));
			TestFalse(TEXT("and a body that has published nothing yet has no pose to hold"),
				ShouldHoldPose(/*bHasAppliedOnce*/ false, false, false));

			using ElysiumAnimGraph::StateCanPlayBlendSpace;
			using ElysiumAnimGraph::TryParseState;
			using ElysiumAnimGraph::ActivityForState;
			for (int32 i = 0; i < NumGraphStates; ++i)
			{
				const EElysiumGraphState State = static_cast<EElysiumGraphState>(i);
				TestTrue(FString::Printf(TEXT("%s plays a blend space"), StateName(State)),
					StateCanPlayBlendSpace(State));
			}
			EElysiumGraphState Parsed = EElysiumGraphState::Walk;
			TestTrue(TEXT("Idle parses"), TryParseState(TEXT("Idle"), Parsed)
				&& Parsed == EElysiumGraphState::Idle);
			TestTrue(TEXT("and it is case-insensitive"), TryParseState(TEXT("land"), Parsed)
				&& Parsed == EElysiumGraphState::Land);
			TestFalse(TEXT("an unknown name does not parse"), TryParseState(TEXT("Swim"), Parsed));
			TestEqual(TEXT("Walk's stand activity is ACT_WALK"),
				FString(ActivityForState(EElysiumGraphState::Walk)), FString(TEXT("ACT_WALK")));

			// **No clip is a FINISHED one-shot, not an unanswerable one.** Treating the ducked
			// landing's miss as unknown parked the body in its previous pose for the whole fallback
			// window, which reads as floating after touching down.
			auto OneAsInt = [](EOne State) { return static_cast<int32>(State); };
			TestEqual(TEXT("a request with no clip is already complete"),
				OneAsInt(OneShotStateFor(/*bHasAsset*/ false, true, true, false)),
				OneAsInt(EOne::Complete));
			TestEqual(TEXT("even when no report agrees with it"),
				OneAsInt(OneShotStateFor(false, false, false, false)), OneAsInt(EOne::Complete));
			// **A stale report describes a clip that is no longer playing.**
			TestEqual(TEXT("a report from a superseded generation answers nothing"),
				OneAsInt(OneShotStateFor(true, /*bGenerationMatches*/ false, true, true)),
				OneAsInt(EOne::Unknown));
			TestEqual(TEXT("and so does a state that is not a one-shot"),
				OneAsInt(OneShotStateFor(true, true, /*bInOneShotState*/ false, true)),
				OneAsInt(EOne::Unknown));
			TestEqual(TEXT("a live one-shot still running is Playing"),
				OneAsInt(OneShotStateFor(true, true, true, /*bComplete*/ false)),
				OneAsInt(EOne::Playing));
			TestEqual(TEXT("and a finished one is Complete"),
				OneAsInt(OneShotStateFor(true, true, true, true)), OneAsInt(EOne::Complete));

			// --- the loop bit the blend stack cannot notice on its own (S2) ----------------------
			//
			// `FAnimNode_BlendStack::ConditionalBlendTo` returns early when the requested asset
			// matches the playing one, and `bLoop` is consumed only inside `BlendTo`. So the pin
			// holds a new value the node never reads, silently — and `Crouch` is exactly that
			// request, republished as a held stance over its own non-looping into-pose.
			using ElysiumAnimGraph::NeedsForcedReblend;
			TestTrue(TEXT("the same asset with a flipped loop bit has to be forced"),
				NeedsForcedReblend(/*bSameAsset*/ true, /*bLoopChanged*/ true));
			TestFalse(TEXT("the same asset with the same loop bit is already what is playing"),
				NeedsForcedReblend(true, false));
			// A different asset re-blends on its own; forcing there would ask the stack to push a
			// second player for the transition it is already performing.
			TestFalse(TEXT("a new asset needs no force -- the node notices that itself"),
				NeedsForcedReblend(/*bSameAsset*/ false, /*bLoopChanged*/ true));
			TestFalse(TEXT("and neither does a new asset with an unchanged loop bit"),
				NeedsForcedReblend(false, false));
		}

		// --- the transition curve the stack is pinned to (S2) ---------------------------------
		//
		// Retail's own blend shape, recorded at `0x10225158` as the constant its alpha is passed
		// through (`docs/vtmb/animation_and_movers.md`): `3t^2 - 2t^3`. The engine spells it
		// `EAlphaBlendOption::HermiteCubic`, which the generator writes onto the node — and because
		// that value equals the engine's own default it appears NOWHERE in the exported graph text,
		// so this is the only place the identity itself is stated. `Elysium.Content.GraphBlendStack`
		// asserts the node carries it; this asserts what carrying it means.
		{
			const float Ts[] = { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f };
			for (const float T : Ts)
			{
				const float Retail = 3.0f * T * T - 2.0f * T * T * T;
				TestEqual(*FString::Printf(TEXT("HermiteCubic(%.2f) is retail's 3t^2-2t^3"), T),
					FAlphaBlend::AlphaToBlendOption(T, EAlphaBlendOption::HermiteCubic), Retail,
					1e-6f);
			}
			// Both ends clamp rather than extrapolating, which is what makes a fade that overshoots
			// its own duration hold the pose instead of pulling past it.
			TestEqual(TEXT("an alpha below zero clamps to zero"),
				FAlphaBlend::AlphaToBlendOption(-0.5f, EAlphaBlendOption::HermiteCubic), 0.0f, 1e-6f);
			TestEqual(TEXT("and one past one clamps to one"),
				FAlphaBlend::AlphaToBlendOption(1.5f, EAlphaBlendOption::HermiteCubic), 1.0f, 1e-6f);
		}

		// Reachable but outside the slice. Standing is a stated answer, not a hole.
		TestEqual(TEXT("swimming stands rather than falling through the projection"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Swim)), AsInt(EElysiumGraphState::Idle));
		TestEqual(TEXT("and so does an activity this slice cannot name"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Unknown)),
			AsInt(EElysiumGraphState::Idle));
		// The death family (LIFE5) is in the vocabulary and is deliberately NOT a locomotion state:
		// a death pose plays on the reaction branch over whatever the graph is posing, so this
		// projection names the state underneath it rather than a ninth state the asset does not have.
		TestEqual(TEXT("ACT_DIESIMPLE names the state underneath the reaction, not one of its own"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::DieSimple)),
			AsInt(EElysiumGraphState::Idle));
		TestEqual(TEXT("and so does the ragdoll seed label"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::DieRagdoll)),
			AsInt(EElysiumGraphState::Idle));
		TestEqual(TEXT("both are spelled once, in the one activity vocabulary"),
			FString(ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::DieSimple)),
			FString(TEXT("ACT_DIESIMPLE")));
		TestEqual(TEXT("...including the ragdoll label"),
			FString(ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::DieRagdoll)),
			FString(TEXT("ACT_DIERAGDOLL")));

		// --- coverage: every activity the slice can emit lands somewhere that can play it ---------
		//
		// Spot checks prove the rows that were argued over; this proves there is no row missing. A
		// classifier answer with no state is what would leave a body in the reference pose with
		// nothing in the log, so the enum is walked whole rather than sampled.
		//
		// **The bound is the enum's own `Count`, and it has to be.** It was previously the literal
		// `DieRagdoll`, which the comment described as "the last enumerator" — but the ten grounded
		// knockback cells already sat past it, so the walk had silently stopped covering them. A
		// sentinel cannot go stale that way, and every family added to the vocabulary now joins this
		// walk with it.
		using ElysiumAnimGraph::StateCanPlay;
		for (uint8 Code = 0; Code < static_cast<uint8>(EElysiumAnimActivityCode::Count); ++Code)
		{
			const EElysiumAnimActivityCode Activity = static_cast<EElysiumAnimActivityCode>(Code);
			const EElysiumGraphState State = StateForActivity(Activity);
			const FString Named = Code == 0
				? FString(TEXT("(outside the slice)"))
				: FString(ElysiumAnimIntent::ActivityName(Activity));
			TestTrue(*FString::Printf(TEXT("%s routes to one of the eight states"), *Named),
				static_cast<int32>(State) >= 0 && static_cast<int32>(State) < NumGraphStates);
			// Both shapes the resolver can hand back, because which one an activity resolves to is
			// the body's data and not this projection's to know: a fan on one model is a single clip
			// on another, and a state that could play only one of them would be a hole that shows up
			// on some cast members and not others.
			TestTrue(*FString::Printf(TEXT("%s's state %s plays a sequence"), *Named,
					StateName(State)),
				StateCanPlay(State, EElysiumAnimAssetKind::Sequence));
			TestTrue(*FString::Printf(TEXT("%s's state %s plays a blend space"), *Named,
					StateName(State)),
				StateCanPlay(State, EElysiumAnimAssetKind::BlendSpace));
			// And a miss is playable everywhere, because the graph answers it by holding its pose.
			TestTrue(*FString::Printf(TEXT("%s's state %s can answer a miss"), *Named,
					StateName(State)),
				StateCanPlay(State, EElysiumAnimAssetKind::None));
		}

		// The one shape no state may take as a base pose. A masked overlay rides the layered blend;
		// standing one as the body would replace the pose it is supposed to compose over.
		for (int32 i = 0; i < NumGraphStates; ++i)
		{
			const EElysiumGraphState State = static_cast<EElysiumGraphState>(i);
			TestFalse(*FString::Printf(TEXT("%s refuses a layer as a base pose"), StateName(State)),
				StateCanPlay(State, EElysiumAnimAssetKind::Layer));
		}

		// The record carries the projection itself: the resolver runs it once and every reader — the
		// anim instance, the Cog row, the trace, the MCP surface — reads this field instead of
		// deriving a second answer.
		FElysiumAnimationCatalog Empty;
		FElysiumAnimationIntent Sneaking;
		Sneaking.Stem = TEXT("nobody");
		Sneaking.Activity = ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::Sneak);
		FElysiumAnimationSelection Sel;
		ElysiumAnimResolve::Resolve(Sneaking, Empty, Sel);
		TestEqual(TEXT("the record names the state its request projects to"),
			AsInt(Sel.GraphState), AsInt(EElysiumGraphState::Sneak));
		// Even with no vocabulary behind it. A body that resolved nothing still has to stand
		// somewhere, and a record that named no state would make that "nowhere".
		TestEqual(TEXT("even when nothing resolved"), static_cast<int32>(Sel.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::NoVocabulary));
	}

	// --- The names the authored asset is asserted against -----------------------------------------
	{
		// Every state has a distinct name, because the graph's state nodes carry exactly these and a
		// duplicate would make two states indistinguishable to the asset check.
		TSet<FString> Names;
		for (int32 i = 0; i < NumGraphStates; ++i)
		{
			Names.Add(StateName(static_cast<EElysiumGraphState>(i)));
		}
		TestEqual(TEXT("the eight state names are distinct"), Names.Num(), NumGraphStates);

		// The three non-looping clips the authored data actually carries.
		TestTrue(TEXT("leap, land and crouch are the one-shots"),
			IsOneShotState(EElysiumGraphState::Leap)
			&& IsOneShotState(EElysiumGraphState::Land)
			&& IsOneShotState(EElysiumGraphState::Crouch));
		TestFalse(TEXT("and the gaits are not"),
			IsOneShotState(EElysiumGraphState::Walk)
			|| IsOneShotState(EElysiumGraphState::Run)
			|| IsOneShotState(EElysiumGraphState::Sneak)
			|| IsOneShotState(EElysiumGraphState::Idle)
			|| IsOneShotState(EElysiumGraphState::Falling));
	}

	// --- The overlay slot, as the graph is handed it ------------------------------------------------
	//
	// Two numbers reach the slot's blend and its evaluator, and neither is computed in the graph: the
	// enveloped weight (`ElysiumAnimIntent::SlotWeightAt`) and the explicit time the evaluator is
	// pinned to. The envelope's own table — both recovered families, the per-weapon renames, the held
	// claim — is asserted whole in `Elysium.Substrate.AnimationArbitration`; what is asserted here is
	// the pair as the PINS see it, because that is where a ceiling written instead of an envelope, or
	// a playhead carried across a swapped asset, becomes a pose.
	{
		using namespace ElysiumAnimIntent;

		FElysiumClipSegment Reload;
		Reload.ClipName = TEXT("glock_reload_layer");
		Reload.Channel = EElysiumAnimChannel::UpperBody;
		Reload.Activity = TEXT("ACT_RELOAD_LAYER");
		const FElysiumAnimationRequest ReloadClaim = ClaimForSegment(Reload, /*PlayLengthSeconds=*/2.0f);

		FElysiumClipSegment Shot = Reload;
		Shot.ClipName = TEXT("glock_fire_layer");
		Shot.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
		const FElysiumAnimationRequest ShotClaim = ClaimForSegment(Shot, /*PlayLengthSeconds=*/0.5f);

		// The weight pin, both recovered families: an attack layer is at the ceiling on the frame it
		// is armed and a reload is at the foot of its ramp. Writing `SlotWeightMax` on the pin instead
		// — which is what the autolayer blend's own weight legitimately does — would compose these two
		// identically and lose the ramp outright.
		TestEqual(TEXT("the slot's weight pin snaps to the ceiling for an attack layer"),
			SlotWeightAt(ShotClaim, 0.0f), SlotWeightMax);
		TestEqual(TEXT("...and starts a reload layer at zero, on the smoothstep's foot"),
			SlotWeightAt(ReloadClaim, 0.0f), 0.0f);
		TestEqual(TEXT("...reaching the smoothstep's midpoint half way up the ramp"),
			SlotWeightAt(ReloadClaim, 0.2f), 0.5f);
		// PAST the end, not merely at it: `SlotCycle` clamps, so an age beyond the claim's own hold
		// still reads cycle 1 and the layer is gone. A body whose producer stopped ticking must not
		// leave a shot composing forever.
		TestEqual(TEXT("...and weightless past the cycle its claim ends on"),
			SlotWeightAt(ReloadClaim, 3.0f), 0.0f);

		// The evaluator's time pin: the claim's phase projected onto the clip's own seconds, so the
		// layer cannot finish early or linger past the claim that expires it.
		TestEqual(TEXT("the slot evaluator seats at the head on cycle zero"),
			SlotEvaluatorTime(0.0f, 2.0f, /*bSequenceChanged=*/false), 0.0f);
		TestEqual(TEXT("...half way through the clip at cycle 0.5"),
			SlotEvaluatorTime(0.5f, 2.0f, /*bSequenceChanged=*/false), 1.0f);
		TestEqual(TEXT("...at the clip's end when the cycle reaches 1"),
			SlotEvaluatorTime(1.0f, 2.0f, /*bSequenceChanged=*/false), 2.0f);
		// A cycle the caller has not clamped cannot walk the playhead off the clip.
		TestEqual(TEXT("...and never past it"),
			SlotEvaluatorTime(3.0f, 2.0f, /*bSequenceChanged=*/false), 2.0f);
		// The restart. A re-fire of the SAME clip restarts through the cycle (a new claim starts at
		// age zero); what only this flag can state is a publish that swaps the asset while still
		// carrying the previous claim's phase, which would seat a fresh clip mid-motion.
		TestEqual(TEXT("a swapped layer asset re-seats the playhead at its head"),
			SlotEvaluatorTime(0.5f, 2.0f, /*bSequenceChanged=*/true), 0.0f);
		// No clip, no playhead — and no division into a zero length.
		TestEqual(TEXT("and a layer with no length has no playhead at all"),
			SlotEvaluatorTime(0.5f, 0.0f, /*bSequenceChanged=*/false), 0.0f);
	}

	return true;
}

// =====================================================================================
// LIFE3 - the driver, and the one speed number it publishes.
//
// The whole chain is content-free, so all of it is asserted here: the body key the tables resolve
// under, the gait the stride is read at, the discrete key that decides when anything re-resolves,
// and the sample the record was classified from. Every one of them is a place the speed and the
// pose can come apart, and the failure they share is the same one -- the body plays a cell it is
// not travelling at.
// =====================================================================================

namespace
{
	// A five-cell `move_yaw` fan over the shipped -180..180 axis: back, side, forward, side, back.
	FElysiumGaitSpeedTable Fan(float Forward, float Side, float Back)
	{
		FElysiumGaitSpeedTable Table;
		Table.Count = 5;
		Table.AxisMin = -180.0f;
		Table.AxisMax = 180.0f;
		Table.Cells[0] = Back;
		Table.Cells[1] = Side;
		Table.Cells[2] = Forward;
		Table.Cells[3] = Side;
		Table.Cells[4] = Back;
		Table.Scale = 1.0f;
		return Table;
	}

	// A grounded body travelling straight ahead at `Speed`, which is what a path-following cast
	// member reports once its facing has settled onto its path.
	FElysiumLocomotionSample Travelling(float Speed)
	{
		FElysiumLocomotionSample Sample;
		Sample.bOnGround = true;
		Sample.LocalVelocity = FVector(Speed, 0.0f, 0.0f);
		Sample.WishScale = Speed > 0.0f ? 1.0f : 0.0f;
		return Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationDriverTest,
	"Elysium.Substrate.AnimationDriver", GElysiumAnimationTestFlags)
bool FElysiumAnimationDriverTest::RunTest(const FString&)
{
	constexpr float ForwardWalk = 140.0f;
	constexpr float SideWalk = 70.0f;
	constexpr float BackWalk = 60.0f;
	constexpr float ForwardRun = 560.0f;

	// --- The body key: the same chain the pose walks ----------------------------------------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.ActorClassname = TEXT("npc_gangbanger_a");
		Driver.WeaponClassname = TEXT("item_w_glock_17c");
		Driver.FormTag = TEXT("human");
		Driver.ActorState = EElysiumNpcState::Alert;
		Driver.Variant = 3;

		const FElysiumGaitSpeedRequest Key = Driver.BuildGaitKey();
		TestEqual(TEXT("the gait key walks the body's own pre-translation chain"),
			static_cast<int32>(Key.BodyKind), static_cast<int32>(EElysiumAnimBodyKind::Cast));
		TestEqual(TEXT("...beside the producer that drives it"),
			static_cast<int32>(Key.Source), static_cast<int32>(EElysiumAnimSource::Npc));
		TestEqual(TEXT("...carries the actor classname that finds its class bodies"),
			Key.ActorClassname, Driver.ActorClassname);
		TestEqual(TEXT("...and the state the alert/relaxed branch reads"),
			static_cast<int32>(Key.ActorState), static_cast<int32>(EElysiumNpcState::Alert));
		TestEqual(TEXT("...beside the weapon and form it already carried"),
			Key.WeaponClassname, Driver.WeaponClassname);

		// Each of the three is a different set of sequences, so each of the three has to move the
		// key. A key that compares equal across them resolves one body's speeds for another's pose.
		FElysiumGaitSpeedRequest Player = Key;
		Player.BodyKind = EElysiumAnimBodyKind::Player;
		TestTrue(TEXT("a player-chain key is not the same key"), Player != Key);

		FElysiumGaitSpeedRequest OtherClass = Key;
		OtherClass.ActorClassname = TEXT("npc_thug_a");
		TestTrue(TEXT("another class body is not the same key"), OtherClass != Key);

		FElysiumGaitSpeedRequest Combat = Key;
		Combat.ActorState = EElysiumNpcState::Combat;
		TestTrue(TEXT("another actor state is not the same key"), Combat != Key);

		FElysiumGaitSpeedRequest OtherForm = Key;
		OtherForm.FormTag = TEXT("werewolf");
		TestTrue(TEXT("another form is not the same key"), OtherForm != Key);

		FElysiumGaitSpeedRequest Unarmed = Key;
		Unarmed.WeaponClassname.Reset();
		TestTrue(TEXT("and empty hands are not the same key"), Unarmed != Key);

		// Field-wise rather than reflexive: a key assembled from the same values on a different
		// body has to compare equal, or every producer re-resolves its tables every frame. Copying
		// the key and comparing it with itself would pass whatever the comparison was written to do.
		FElysiumGaitSpeedRequest Rebuilt;
		Rebuilt.Stem = TEXT("gangbanger_a");
		Rebuilt.Source = EElysiumAnimSource::Npc;
		Rebuilt.BodyKind = EElysiumAnimBodyKind::Cast;
		Rebuilt.ActorClassname = TEXT("npc_gangbanger_a");
		Rebuilt.WeaponClassname = TEXT("item_w_glock_17c");
		Rebuilt.FormTag = TEXT("human");
		Rebuilt.ActorState = EElysiumNpcState::Alert;
		Rebuilt.Variant = 3;
		TestTrue(TEXT("and the same values assembled again are the same key"), Rebuilt == Key);
	}

	// --- The stride: classified from the graph state, read at a direction -------------------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, SideWalk, BackWalk);
		Driver.GaitSpeeds.Run = Fan(ForwardRun, 300.0f, 240.0f);

		// The record a translated request actually publishes: the resolved name is a weapon-ladder
		// literal outside the slice's own code vocabulary, and the resolve-time cell is whatever the
		// body happened to be doing when the activity last changed.
		Driver.Selection.GraphState = EElysiumGraphState::Walk;
		Driver.Selection.ResolvedActivity = TEXT("ACT_WALK_RELAXED_PISTOL");
		Driver.Selection.GroundSpeedCmPerSecond = 999.0f;

		TestEqual(TEXT("an armed walk still reads its own walk fan"),
			Driver.GaitSpeedForSelection(0.0f), ForwardWalk);
		TestEqual(TEXT("...at the direction it is travelling in, not at forward"),
			Driver.GaitSpeedForSelection(90.0f), SideWalk);
		TestEqual(TEXT("...including the reversal a patrol turnaround plays"),
			Driver.GaitSpeedForSelection(180.0f), BackWalk);

		Driver.Selection.GraphState = EElysiumGraphState::Run;
		TestEqual(TEXT("and the run state reads the run fan"),
			Driver.GaitSpeedForSelection(0.0f), ForwardRun);

		// Anything that is not one of the three gaits keeps the resolved cell's own authored speed,
		// which is what a scripted label route publishes and what the caller asked for by name.
		Driver.Selection.GraphState = EElysiumGraphState::Idle;
		TestEqual(TEXT("a non-gait state keeps the resolved cell's own speed"),
			Driver.GaitSpeedForSelection(0.0f), 999.0f);
	}

	// --- The discrete key, and the sample the record was classified from --------------------------
	{
		constexpr float Dt = 1.0f / 60.0f;
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, SideWalk, BackWalk);
		Driver.GaitSpeeds.Run = Fan(ForwardRun, 300.0f, 240.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		const FElysiumLocomotionSample Walking = Travelling(ForwardWalk);
		Driver.Tick(Dt, Walking, nullptr, nullptr);
		const uint32 First = Driver.Generation;
		TestTrue(TEXT("the first tick is a request"), First > 0);
		TestEqual(TEXT("a walking body stands in the walk state"),
			AsInt(Driver.Selection.GraphState), AsInt(EElysiumGraphState::Walk));
		// With no catalog behind it the record resolves nothing -- and the stride is STILL the
		// body's own fan, because the gait comes off the projected state rather than off a resolved
		// name.
		TestEqual(TEXT("a body with no vocabulary still travels at its own authored cell"),
			Driver.Selection.GroundSpeedCmPerSecond, ForwardWalk);

		// The sample the driver classified, kept beside the record it produced.
		TestEqual(TEXT("the driver keeps the sample it classified"),
			Driver.Sample.Speed2D(), ForwardWalk);

		Driver.Tick(Dt, Walking, nullptr, nullptr);
		TestEqual(TEXT("the same request does not re-resolve"), Driver.Generation, First);

		// The classname arrives a frame late -- the owner has to resolve the entity before it can
		// push one -- so it has to be part of the key, or the class-less first answer is kept for
		// the life of the request.
		Driver.ActorClassname = TEXT("npc_gangbanger_a");
		Driver.Tick(Dt, Walking, nullptr, nullptr);
		TestTrue(TEXT("a resolved actor classname is a new request"), Driver.Generation > First);

		const uint32 Classed = Driver.Generation;
		Driver.Tick(Dt, Walking, nullptr, nullptr);
		TestEqual(TEXT("...and settles once it stops moving"), Driver.Generation, Classed);

		// The pose parameter is a rate, and the pair has to carry the FILTERED value. A body walking
		// dead ahead cannot show that — its raw yaw and its filtered one are both zero, so the two
		// sides of the assertion agree whichever one the driver published. A body that has just
		// begun to turn can: the slew is one step in, so the filtered value is neither the raw
		// reading nor where it started.
		FElysiumLocomotionSample Turning = Travelling(ForwardWalk);
		Turning.MoveYawVelocity = 60.0f;
		Driver.Tick(Dt, Turning, nullptr, nullptr);
		TestEqual(TEXT("the record is steered by the sample's own pose parameter"),
			Driver.Sample.MoveYawPose, Driver.Selection.MoveYaw);
		TestEqual(TEXT("...which keeps the raw reading it was filtered from"),
			Driver.Sample.MoveYawVelocity, Turning.MoveYawVelocity);
		TestTrue(TEXT("...and is neither that reading"),
			!FMath::IsNearlyEqual(Driver.Sample.MoveYawPose, Turning.MoveYawVelocity));
		TestTrue(TEXT("...nor the zero it started from"),
			FMath::Abs(Driver.Sample.MoveYawPose) > UE_KINDA_SMALL_NUMBER);
		// And the stride follows it off the fan, at the angle that was actually published.
		TestEqual(TEXT("...and the stride is the fan read at that angle"),
			Driver.Selection.GroundSpeedCmPerSecond,
			Driver.GaitSpeeds.Walk.SpeedAt(Driver.Selection.MoveYaw));

		// A reset drops the published pair together: a sample left behind beside an empty record
		// would trace a body moving through a frame nothing classified.
		Driver.Reset();
		TestEqual(TEXT("a reset forgets the sample with the record"), Driver.Sample.Speed2D(), 0.0f);
	}

	// --- LIFE5 — a reaction's verdict rides the published record, not a second channel -------------
	// The flinch is armed off the damage commit and the graph obeys the record, so the two facts a
	// graph reads have to be on the record the driver publishes every frame: that the base is not the
	// publish's to take, and who is holding it. Asserted on the ordinary tick path, because that is
	// the only path the graph ever reads.
	{
		constexpr float Dt = 1.0f / 60.0f;
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, SideWalk, BackWalk);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestTrue(TEXT("an unclaimed travelling publish owns the base"),
			Driver.Selection.bBasePoseOwned);

		FElysiumAnimationRequest Flinch;
		Flinch.Source = EElysiumAnimSource::Damage;
		Flinch.Channel = EElysiumAnimChannel::Base;
		Flinch.Priority = EElysiumAnimPriority::Reaction;
		Flinch.Label = TEXT("hit_torso_back_right");
		Flinch.HoldSeconds = 0.5f;
		TestTrue(TEXT("the reaction claims the base"), Driver.SubmitRequest(Flinch) != 0u);

		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestFalse(TEXT("the published record says the publish does not own the base"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and names the flinch that is holding it"),
			Driver.Selection.BaseHold.Contains(TEXT("hit_torso_back_right")));
		TestTrue(TEXT("...as a reaction"), Driver.Selection.BaseHold.Contains(TEXT("reaction")));
		// The rest of the record is untouched: the body is still travelling, and a held base must not
		// be reported as a body that stopped.
		TestEqual(TEXT("...while the body keeps travelling on the same record"),
			AsInt(Driver.Selection.GraphState), AsInt(EElysiumGraphState::Walk));
		TestEqual(TEXT("...at its own stride"), Driver.Selection.GroundSpeedCmPerSecond, ForwardWalk);
	}

	// --- A segment's own channel becomes the claim's channel ---------------------------------------
	//
	// `FElysiumClipSegment::Channel` is what a partial-body overlay layer (retail's
	// `CBaseAnimatingOverlay` slot 0 — ranged fire/reload/dry-fire) claims through.
	// `ElysiumAnimIntent::ClaimForSegment` carries it onto the submitted claim, and the driver's
	// per-channel slots keep that claim apart from Base.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;

		FElysiumClipSegment Overlay;
		Overlay.ClipName = TEXT("dryfire_9mm");
		Overlay.bLoop = false;
		Overlay.Source = EElysiumAnimSource::Player;
		Overlay.Priority = EElysiumAnimPriority::Ambient;
		Overlay.Channel = EElysiumAnimChannel::UpperBody;

		const FElysiumAnimationRequest OverlayClaim =
			ElysiumAnimIntent::ClaimForSegment(Overlay, /*PlayLengthSeconds=*/0.4f);
		TestEqual(TEXT("a segment naming UpperBody produces a claim on UpperBody"),
			static_cast<int32>(OverlayClaim.Channel), static_cast<int32>(EElysiumAnimChannel::UpperBody));

		const uint32 OverlayHandle = Driver.SubmitRequest(OverlayClaim);
		TestTrue(TEXT("the claim is accepted on the open UpperBody slot"), OverlayHandle != 0);
		TestNotNull(TEXT("...and ActiveRequest(UpperBody) reads it back"),
			Driver.ActiveRequest(EElysiumAnimChannel::UpperBody));
		TestNull(TEXT("...while ActiveRequest(Base) stays empty — the segment never touched it"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));

		// The regression guard: a caller that states no channel hands `ClaimForSegment` a
		// default-constructed segment, and that segment claims Base — which is what every producer
		// outside the overlay families means.
		FElysiumClipSegment Plain;
		Plain.ClipName = TEXT("Stance_Neutral_Idle_1");
		Plain.bLoop = true;
		const FElysiumAnimationRequest PlainClaim = ElysiumAnimIntent::ClaimForSegment(Plain, 0.0f);
		TestEqual(TEXT("a default-constructed segment still claims Base"),
			static_cast<int32>(PlainClaim.Channel), static_cast<int32>(EElysiumAnimChannel::Base));

		const uint32 PlainHandle = Driver.SubmitRequest(PlainClaim);
		TestTrue(TEXT("...and is accepted on the open Base slot"), PlainHandle != 0);
		TestNotNull(TEXT("...so ActiveRequest(Base) now reads it back"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		TestNotNull(TEXT("...without disturbing the UpperBody claim still standing beside it"),
			Driver.ActiveRequest(EElysiumAnimChannel::UpperBody));
	}

	return true;
}

// =====================================================================================
// LIFE4, Option A - the player's grounded stand/gait comes off the committed retail ladder.
//
// The driver walks `PlayerGaitLadder()` with a live state query: `CombatReady` gates `ACT_AIM`
// on an armed, in-stance, unmorphed body, and `Relaxed` selects the relaxed gaits for an armed
// body out of stance. Water and the air phases stay with `Classify` and the latch, and the cast
// never consults the ladder at all. All content-free: no catalog, no world.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationPlayerGaitTest,
	"Elysium.Substrate.AnimationPlayerGait", GElysiumAnimationTestFlags)
bool FElysiumAnimationPlayerGaitTest::RunTest(const FString&)
{
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float ForwardWalk = 140.0f;
	constexpr float ForwardRun = 560.0f;

	auto MakeDriver = [&](EElysiumAnimSource Source, EElysiumAnimBodyKind BodyKind)
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("pc_body");
		Driver.Source = Source;
		Driver.BodyKind = BodyKind;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, 70.0f, 60.0f);
		Driver.GaitSpeeds.Run = Fan(ForwardRun, 300.0f, 240.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);
		return Driver;
	};
	auto Requested = [](FElysiumAnimationDriver& Driver, const FElysiumLocomotionSample& Sample)
	{
		Driver.Tick(Dt, Sample, nullptr, nullptr);
		return Driver.Selection.RequestedActivity;
	};

	// --- The standing-with-weapon call: CombatReady gates ACT_AIM ---------------------------------
	{
		FElysiumAnimationDriver Driver = MakeDriver(EElysiumAnimSource::Player,
			EElysiumAnimBodyKind::Player);
		Driver.WeaponClassname = TEXT("item_w_glock_17c");

		TestEqual(TEXT("an armed stand out of stance is an idle"),
			Requested(Driver, Travelling(0.0f)), FString(TEXT("ACT_IDLE")));

		Driver.bCombatStance = true;
		TestEqual(TEXT("the same stand in combat stance requests ACT_AIM"),
			Requested(Driver, Travelling(0.0f)), FString(TEXT("ACT_AIM")));
		// ACT_AIM is outside the slice's own code vocabulary, so it projects to Idle — which is
		// also what ranks the combat stand as a standing publish in the arbitration.
		TestEqual(TEXT("...which stands in the Idle graph state"),
			AsInt(Driver.Selection.GraphState), AsInt(EElysiumGraphState::Idle));

		// The recovered weapon test: `item_w_unarmed` never reads combat-ready, and neither do
		// empty hands.
		Driver.WeaponClassname = TEXT("item_w_unarmed");
		TestEqual(TEXT("an unarmed body in stance still idles"),
			Requested(Driver, Travelling(0.0f)), FString(TEXT("ACT_IDLE")));
		Driver.WeaponClassname.Reset();
		TestEqual(TEXT("...as do empty hands"),
			Requested(Driver, Travelling(0.0f)), FString(TEXT("ACT_IDLE")));
	}

	// --- Relaxed is an active weapon out of stance, so stance strips the relaxed forms -------------
	{
		FElysiumAnimationDriver Driver = MakeDriver(EElysiumAnimSource::Player,
			EElysiumAnimBodyKind::Player);
		Driver.WeaponClassname = TEXT("item_w_glock_17c");

		TestEqual(TEXT("an armed walk out of stance is the relaxed form"),
			Requested(Driver, Travelling(ForwardWalk)), FString(TEXT("ACT_WALK_RELAXED")));
		TestEqual(TEXT("...and the run likewise"),
			Requested(Driver, Travelling(ForwardRun)), FString(TEXT("ACT_RUN_RELAXED")));

		Driver.bCombatStance = true;
		TestEqual(TEXT("in stance the same run is the plain gait"),
			Requested(Driver, Travelling(ForwardRun)), FString(TEXT("ACT_RUN")));
		TestEqual(TEXT("...and the walk likewise"),
			Requested(Driver, Travelling(ForwardWalk)), FString(TEXT("ACT_WALK")));

		// An unarmed body satisfies neither `CombatReady` nor `Relaxed` — both require an active
		// weapon — so it keeps the plain gait whatever the stance clock says. Downstream weapon
		// translation would collapse the relaxed forms back to plain for `item_w_unarmed` anyway
		// (`PlayerTranslations()`), so this is the ladder's own answer agreeing with that outcome
		// rather than depending on it.
		Driver.WeaponClassname = TEXT("item_w_unarmed");
		Driver.bCombatStance = false;
		TestEqual(TEXT("an unarmed walk out of stance is the plain gait"),
			Requested(Driver, Travelling(ForwardWalk)), FString(TEXT("ACT_WALK")));
		Driver.bCombatStance = true;
		TestEqual(TEXT("...and stays the plain gait in stance"),
			Requested(Driver, Travelling(ForwardWalk)), FString(TEXT("ACT_WALK")));
	}

	// --- The ducked rows outrank the aim row, exactly as committed --------------------------------
	{
		FElysiumAnimationDriver Driver = MakeDriver(EElysiumAnimSource::Player,
			EElysiumAnimBodyKind::Player);
		Driver.WeaponClassname = TEXT("item_w_glock_17c");
		Driver.bCombatStance = true;

		FElysiumLocomotionSample Ducked = Travelling(0.0f);
		Ducked.Stance = EElysiumStance::Ducked;
		TestEqual(TEXT("a ducked stand in stance crouches rather than aims"),
			Requested(Driver, Ducked), FString(TEXT("ACT_CROUCH")));

		FElysiumLocomotionSample Sneaking = Travelling(ForwardWalk);
		Sneaking.Stance = EElysiumStance::Ducked;
		TestEqual(TEXT("and a ducked mover sneaks, with no stance split below it"),
			Requested(Driver, Sneaking), FString(TEXT("ACT_SNEAK")));
	}

	// --- Water and the air phases stay with the classifier and the latch --------------------------
	{
		FElysiumAnimationDriver Driver = MakeDriver(EElysiumAnimSource::Player,
			EElysiumAnimBodyKind::Player);
		Driver.WeaponClassname = TEXT("item_w_glock_17c");
		Driver.bCombatStance = true;

		FElysiumLocomotionSample Swimming = Travelling(ForwardWalk);
		Swimming.Water = EElysiumWaterLevel::Waist;
		TestEqual(TEXT("a swimming body swims, ladder or no ladder"),
			Requested(Driver, Swimming), FString(TEXT("ACT_SWIM")));

		FElysiumLocomotionSample Airborne = Travelling(ForwardWalk);
		Airborne.bOnGround = false;
		TestEqual(TEXT("a body that walked off a ledge falls — the latch's answer stands"),
			Requested(Driver, Airborne), FString(TEXT("ACT_FALLING")));
	}

	// --- The cast never consults the ladder ---------------------------------------------------------
	{
		FElysiumAnimationDriver Driver = MakeDriver(EElysiumAnimSource::Npc,
			EElysiumAnimBodyKind::Cast);
		Driver.WeaponClassname = TEXT("item_w_glock_17c");
		Driver.bCombatStance = true;

		TestEqual(TEXT("a cast stand in stance is still an idle — the ladder is the player's"),
			Requested(Driver, Travelling(0.0f)), FString(TEXT("ACT_IDLE")));
		TestEqual(TEXT("...and a cast walk stays the plain cast request"),
			Requested(Driver, Travelling(ForwardWalk)), FString(TEXT("ACT_WALK")));
	}

	return true;
}

// =====================================================================================
// LIFE4 - the channel arbitration slot.
//
// Who owns the base pose is a priority decision made in the driver and carried on the record, and
// every one of its rows is content-free: the locomotion publish's own rank comes off the projected
// graph state, a claim's off the request a producer wrote, and the verdict is the comparison. The
// graph merely obeys the verdict (`Elysium.Content.GraphOneShotArbitration` proves that half), so
// everything decided is asserted here with no catalog and no world.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationArbitrationTest,
	"Elysium.Substrate.AnimationArbitration", GElysiumAnimationTestFlags)
bool FElysiumAnimationArbitrationTest::RunTest(const FString&)
{
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float ForwardWalk = 140.0f;

	// --- The table itself: the interim rule is two rows of it, and the bands stack above ----------
	{
		using namespace ElysiumAnimIntent;
		TestEqual(TEXT("a standing publish is the floor"),
			static_cast<int32>(LocomotionPriority(EElysiumGraphState::Idle)),
			static_cast<int32>(EElysiumAnimPriority::LocomotionIdle));
		TestEqual(TEXT("a travelling publish is the travel row"),
			static_cast<int32>(LocomotionPriority(EElysiumGraphState::Walk)),
			static_cast<int32>(EElysiumAnimPriority::LocomotionTravel));
		TestEqual(TEXT("...and so is a jump phase"),
			static_cast<int32>(LocomotionPriority(EElysiumGraphState::Land)),
			static_cast<int32>(EElysiumAnimPriority::LocomotionTravel));
		// The order IS the table: ambient sits between the two locomotion rows — which is exactly
		// the interim while-locomoting rule — and the action bands stack above travel.
		TestTrue(TEXT("ambient holds against a standing publish"),
			EElysiumAnimPriority::Ambient > EElysiumAnimPriority::LocomotionIdle);
		TestTrue(TEXT("...and yields to a travelling one"),
			EElysiumAnimPriority::Ambient < EElysiumAnimPriority::LocomotionTravel);
		TestTrue(TEXT("a scripted beat outranks travel"),
			EElysiumAnimPriority::Scripted > EElysiumAnimPriority::LocomotionTravel);
		TestTrue(TEXT("a reaction outranks a scripted beat"),
			EElysiumAnimPriority::Reaction > EElysiumAnimPriority::Scripted);
		TestTrue(TEXT("a scene outranks a reaction"),
			EElysiumAnimPriority::Scene > EElysiumAnimPriority::Reaction);
		TestTrue(TEXT("and the owner's hand outranks everything"),
			EElysiumAnimPriority::Debug > EElysiumAnimPriority::Scene);
		// The default bands a funnel lands on when it cannot know its caller.
		TestEqual(TEXT("an NPC-sourced claim defaults ambient"),
			static_cast<int32>(DefaultPriority(EElysiumAnimSource::Npc)),
			static_cast<int32>(EElysiumAnimPriority::Ambient));
		TestEqual(TEXT("a scene-sourced claim defaults scene"),
			static_cast<int32>(DefaultPriority(EElysiumAnimSource::Scene)),
			static_cast<int32>(EElysiumAnimPriority::Scene));
		TestEqual(TEXT("a damage-sourced claim defaults reaction"),
			static_cast<int32>(DefaultPriority(EElysiumAnimSource::Damage)),
			static_cast<int32>(EElysiumAnimPriority::Reaction));
	}

	// --- (a) The interim rule, subsumed: ambient vs the two locomotion rows ----------------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, 70.0f, 60.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		// No claim: every publish owns the base, standing or not.
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("an unclaimed base belongs to the locomotion publish"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and names no holder"), Driver.Selection.BaseHold.IsEmpty());

		// The schedule's stance: an ambient claim on a standing body.
		FElysiumAnimationRequest Stance;
		Stance.Source = EElysiumAnimSource::Npc;
		Stance.Channel = EElysiumAnimChannel::Base;
		Stance.Priority = EElysiumAnimPriority::Ambient;
		Stance.Label = TEXT("Stance_Neutral_Idle_1");
		const uint32 Handle = Driver.SubmitRequest(Stance);
		TestTrue(TEXT("a claim on an open channel is accepted"), Handle != 0);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestFalse(TEXT("a standing publish yields the base to the ambient claim"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and the record names the holder"),
			Driver.Selection.BaseHold.Contains(TEXT("Stance_Neutral_Idle_1")));
		TestNotNull(TEXT("...and the slot reads the claim back"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));

		// The verdict is continuous: the discrete request never moves, the body simply travels.
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestTrue(TEXT("a travelling publish takes the base back from the ambient claim"),
			Driver.Selection.bBasePoseOwned);
		TestNull(TEXT("...which consumes the claim"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("...so standing again does not resurrect it"),
			Driver.Selection.bBasePoseOwned);
	}

	// --- (b) A higher band holds against the every-tick publish and releases back ----------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, 70.0f, 60.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		FElysiumAnimationRequest Beat;
		Beat.Source = EElysiumAnimSource::Scene;
		Beat.Channel = EElysiumAnimChannel::Base;
		Beat.Priority = EElysiumAnimPriority::Scene;
		Beat.Label = TEXT("jack_wave");
		const uint32 Handle = Driver.SubmitRequest(Beat);
		TestTrue(TEXT("the scene's claim is accepted"), Handle != 0);

		// Sixty travelling publishes — a full second of the every-tick claim the interim rule let
		// win — and the scene holds all of them.
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		}
		TestFalse(TEXT("a scene claim holds the base against a travelling publish"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...naming itself as the holder"),
			Driver.Selection.BaseHold.Contains(TEXT("jack_wave")));

		// A lower band cannot displace it, and refusing says so by handle.
		FElysiumAnimationRequest Stance;
		Stance.Source = EElysiumAnimSource::Npc;
		Stance.Channel = EElysiumAnimChannel::Base;
		Stance.Priority = EElysiumAnimPriority::Ambient;
		Stance.Label = TEXT("Stance_Neutral_Idle_1");
		TestEqual(TEXT("a dialogue stance cannot displace the scene that owns the body"),
			Driver.SubmitRequest(Stance), 0u);

		// Released, the very next publish owns the base again.
		TestTrue(TEXT("the scene gives its claim back by handle"), Driver.ReleaseRequest(Handle));
		TestFalse(TEXT("...and a second release finds it gone"), Driver.ReleaseRequest(Handle));
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestTrue(TEXT("a released base goes back to the locomotion publish"),
			Driver.Selection.bBasePoseOwned);
	}

	// --- A one-shot's claim runs its clip length rather than parking the channel -----------------
	// Deliberate divergence from the interim while-locomoting rule, kept on purpose: the sample
	// here STANDS (an Idle publish) the whole time, and the standing publish still takes the base
	// the tick the hold crosses its length. `HoldSeconds` is the clip's own play length, so the
	// montage's blend-out has already completed when the publish lands — the visible outcome is
	// unchanged, which `Elysium.Content.GraphOneShotArbitration` pins on the real graph.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumAnimationRequest OneShot;
		OneShot.Source = EElysiumAnimSource::Npc;
		OneShot.Channel = EElysiumAnimChannel::Base;
		OneShot.Priority = EElysiumAnimPriority::Ambient;
		OneShot.Label = TEXT("fidget");
		OneShot.HoldSeconds = 0.10f;
		Driver.SubmitRequest(OneShot);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestFalse(TEXT("the one-shot's claim holds while its clip runs"),
			Driver.Selection.bBasePoseOwned);
		// Minor-2 pin: the verdict carries the claim's age, so a reader can see how long the
		// holder has stood.
		TestTrue(TEXT("...and the verdict carries the claim's age"),
			Driver.Selection.BaseHoldSeconds > 0.0f);
		for (int32 Frame = 0; Frame < 4; ++Frame)    // 5 x Dt total ~= 0.083 s, still inside the hold
		{
			Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		}
		TestFalse(TEXT("the frame before the hold's length, the claim still holds"),
			Driver.Selection.bBasePoseOwned);
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);   // 6 x Dt = 0.1 s: expiry
		TestTrue(TEXT("the tick the hold crosses its clip length, a STANDING publish takes the base"),
			Driver.Selection.bBasePoseOwned);
		TestNull(TEXT("...and the slot is empty"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		TestEqual(TEXT("...and the age reads zero while owned"),
			Driver.Selection.BaseHoldSeconds, 0.0f);
	}

	// --- (c) LIFE5 — the Reaction band, which is the flinch's own claim -------------------------
	//
	// One band, three relationships, and each is a behaviour a player sees: a flinch interrupts a
	// walking body and a scripted beat, it cannot interrupt a choreographed scene, and it hands the
	// base back when its clip is done rather than parking the channel on a reaction.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, 70.0f, 60.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		// A scripted beat holds the base first — the band a flinch has to be able to displace.
		FElysiumAnimationRequest Beat;
		Beat.Source = EElysiumAnimSource::Scene;
		Beat.Channel = EElysiumAnimChannel::Base;
		Beat.Priority = EElysiumAnimPriority::Scripted;
		Beat.Label = TEXT("scripted_gesture");
		TestTrue(TEXT("the scripted beat claims the base"), Driver.SubmitRequest(Beat) != 0u);

		FElysiumAnimationRequest Flinch;
		Flinch.Source = EElysiumAnimSource::Damage;
		Flinch.Channel = EElysiumAnimChannel::Base;
		Flinch.Priority = EElysiumAnimPriority::Reaction;
		Flinch.Label = TEXT("hit_torso_left");
		Flinch.HoldSeconds = 0.10f;
		const uint32 Handle = Driver.SubmitRequest(Flinch);
		TestTrue(TEXT("a reaction displaces a scripted beat"), Handle != 0u);

		// And it holds against the locomotion publish — a TRAVELLING one, which is the row the
		// ambient band yields to.
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestFalse(TEXT("a reaction holds the base against a travelling publish"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and the record names the flinch as the holder"),
			Driver.Selection.BaseHold.Contains(TEXT("hit_torso_left")));
		TestTrue(TEXT("...naming the producer that armed it"),
			Driver.Selection.BaseHold.Contains(TEXT("damage")));
		TestTrue(TEXT("...with the claim's age on the verdict"),
			Driver.Selection.BaseHoldSeconds > 0.0f);

		// It expires at its clip length rather than parking the channel: an armer that never comes
		// back cannot leave a body standing in a hit reaction.
		for (int32 Frame = 0; Frame < 4; ++Frame)   // 5 x Dt ~= 0.083 s, still inside the hold
		{
			Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		}
		TestFalse(TEXT("the frame before its length, the reaction still holds"),
			Driver.Selection.bBasePoseOwned);
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);   // 6 x Dt = 0.1 s: expiry
		TestTrue(TEXT("the tick the hold crosses its clip length, the publish takes the base back"),
			Driver.Selection.bBasePoseOwned);
		TestNull(TEXT("...and the slot is empty"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		TestFalse(TEXT("...so the expired handle releases nothing"), Driver.ReleaseRequest(Handle));
	}

	// --- (d) LIFE5 — a scene-held base REFUSES a flinch ------------------------------------------
	// The claim is submitted BEFORE the clip is played, so a refusal here is the whole of "a reaction
	// must not ride over a choreographed scene": nothing plays at all.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumAnimationRequest Scene;
		Scene.Source = EElysiumAnimSource::Scene;
		Scene.Channel = EElysiumAnimChannel::Base;
		Scene.Priority = EElysiumAnimPriority::Scene;
		Scene.Label = TEXT("jack_wave");
		TestTrue(TEXT("the scene owns the body"), Driver.SubmitRequest(Scene) != 0u);

		FElysiumAnimationRequest Flinch;
		Flinch.Source = EElysiumAnimSource::Damage;
		Flinch.Channel = EElysiumAnimChannel::Base;
		Flinch.Priority = EElysiumAnimPriority::Reaction;
		Flinch.Label = TEXT("hit_torso_front");
		Flinch.HoldSeconds = 0.10f;
		TestEqual(TEXT("a flinch on a scene-held body is refused"),
			Driver.SubmitRequest(Flinch), 0u);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("...and the scene is still the holder the record names"),
			Driver.Selection.BaseHold.Contains(TEXT("jack_wave")));
	}

	// --- An unexpiring claim's age still runs, so a leaked hold is diagnosable -------------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumAnimationRequest Scene;
		Scene.Source = EElysiumAnimSource::Scene;
		Scene.Channel = EElysiumAnimChannel::Base;
		Scene.Priority = EElysiumAnimPriority::Scene;
		Scene.Label = TEXT("entire_scene");
		Driver.SubmitRequest(Scene);   // HoldSeconds 0: held until released or outranked

		for (int32 Frame = 0; Frame < 60; ++Frame)   // one second held
		{
			Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		}
		TestFalse(TEXT("the unexpiring claim holds"), Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and its age accumulates on the verdict"),
			Driver.Selection.BaseHoldSeconds > 0.9f);
	}

	// --- The slot survives nothing it should not: a reset drops the claims ------------------------
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumAnimationRequest Beat;
		Beat.Source = EElysiumAnimSource::Scene;
		Beat.Channel = EElysiumAnimChannel::Base;
		Beat.Priority = EElysiumAnimPriority::Scene;
		Beat.Label = TEXT("held_across_epochs");
		const uint32 Handle = Driver.SubmitRequest(Beat);
		Driver.Reset();
		TestNull(TEXT("a reset drops the standing claims"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		TestFalse(TEXT("...and a pre-reset handle releases nothing"),
			Driver.ReleaseRequest(Handle));
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("...so the next publish owns the base"), Driver.Selection.bBasePoseOwned);
	}

	// --- A non-base channel stores its claim for the layer families without touching the base -----
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("gangbanger_a");
		Driver.Source = EElysiumAnimSource::Npc;
		Driver.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumAnimationRequest Aim;
		Aim.Source = EElysiumAnimSource::Player;
		Aim.Channel = EElysiumAnimChannel::UpperBody;
		Aim.Priority = EElysiumAnimPriority::Scripted;
		Aim.Label = TEXT("aim_layer");
		const uint32 Handle = Driver.SubmitRequest(Aim);
		TestTrue(TEXT("an upper-body claim is accepted"), Handle != 0);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("it does not touch the base verdict"), Driver.Selection.bBasePoseOwned);
		const FElysiumAnimationRequest* Held =
			Driver.ActiveRequest(EElysiumAnimChannel::UpperBody);
		if (TestNotNull(TEXT("the layer family reads its channel back"), Held))
		{
			TestEqual(TEXT("...as the claim that was written"), Held->Label, FString(TEXT("aim_layer")));
		}
	}

	// --- Release on scene stop: EVERY stop path gives the scene's claim back ----------------------
	// Regression: `FElysiumAnimating::StopCinematicClip` released the embodiment's cinematic claim
	// only when the idle reset FAILED. With a resolvable idle the Scene claim outlived its scene
	// and refused every ambient claim after it — the base channel parked forever on a scene no
	// longer playing. Both stop routes, InputCancel and natural finish, must give the claim back
	// BEFORE the idle crossfade submits its own.
	{
		ElysiumScene::RegisterInline(TEXT("test/arbitration_scene.vcd"), TEXT(
			"// Choreo version 1\n"
			"actor \"A\"\n"
			"{\n"
			"  channel \"C\"\n"
			"  {\n"
			"    event sequence \"body\"\n"
			"    {\n"
			"      time 0.000000 4.000000\n"
			"      param \"some_clip\"\n"
			"    }\n"
			"  }\n"
			"}\n"
			"fps 60\n"
			"snap off\n"));

		auto BuildSceneWorld = [](FElysiumEntityWorld& World)
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__arbitration__");

			FElysiumEntityDef Actor;
			Actor.Classname = TEXT("npc_VHumanCombatant");
			Actor.TargetName = TEXT("A");
			Actor.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
			Defs.Defs.Add(MoveTemp(Actor));

			FElysiumEntityDef Scene;
			Scene.Classname = TEXT("logic_choreographed_scene");
			Scene.TargetName = TEXT("scene1");
			Scene.Keys.Add(TEXT("SceneFile"), TEXT("test/arbitration_scene.vcd"));
			Scene.Keys.Add(TEXT("BaseAnim"), TEXT("models/cinematic/test_scene.mdl"));
			Defs.Defs.Add(MoveTemp(Scene));

			World.Load(MoveTemp(Defs));
			World.Activate(0.0);
		};

		auto IndexOf = [](const FElysiumRecordingServices& Services, const TCHAR* Prefix)
		{
			for (int32 Index = 0; Index < Services.Calls.Num(); ++Index)
			{
				if (Services.Calls[Index].StartsWith(Prefix))
				{
					return Index;
				}
			}
			return static_cast<int32>(INDEX_NONE);
		};

		// The cancel route: ReleaseActorClips off InputCancel.
		{
			FElysiumRecordingServices Services;
			Services.bCinematicClipsResolve = true;
			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			BuildSceneWorld(World);

			World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")),
				FElysiumVariant::Void(), 0.0, {}, {});
			World.Tick(0.0);
			World.Tick(0.5);
			TestTrue(TEXT("the scene's clip is playing on the actor"),
				Services.Saw(TEXT("PlayCinematicClip")));

			Services.Calls.Reset();
			World.EnqueueInput(TEXT("scene1"), FName(TEXT("Cancel")),
				FElysiumVariant::Void(), 0.0, {}, {});
			World.Tick(1.0);
			const int32 Release = IndexOf(Services, TEXT("ReleaseCinematicClaim"));
			const int32 Idle = IndexOf(Services, TEXT("RefreshNpcIdle"));
			TestTrue(TEXT("Cancel gives the scene's channel claim back"), Release != INDEX_NONE);
			TestTrue(TEXT("...and still crossfades the actor to its idle"), Idle != INDEX_NONE);
			TestTrue(TEXT("...with the claim released BEFORE the idle claims the channel itself"),
				Release != INDEX_NONE && Idle != INDEX_NONE && Release < Idle);
		}

		// The natural-finish route: the same release when the timeline simply ends.
		{
			FElysiumRecordingServices Services;
			Services.bCinematicClipsResolve = true;
			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			BuildSceneWorld(World);

			World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")),
				FElysiumVariant::Void(), 0.0, {}, {});
			World.Tick(0.0);
			World.Tick(0.5);
			TestTrue(TEXT("the scene's clip is playing before the finish"),
				Services.Saw(TEXT("PlayCinematicClip")));

			Services.Calls.Reset();
			for (double Step = 1.0; Step < 7.0; Step += 0.5)
			{
				World.Tick(Step);
			}
			const int32 Release = IndexOf(Services, TEXT("ReleaseCinematicClaim"));
			const int32 Idle = IndexOf(Services, TEXT("RefreshNpcIdle"));
			TestTrue(TEXT("a finished scene gives the channel claim back too"), Release != INDEX_NONE);
			TestTrue(TEXT("...and crossfades the actor to its idle"), Idle != INDEX_NONE);
			TestTrue(TEXT("...claim first, idle second"),
				Release != INDEX_NONE && Idle != INDEX_NONE && Release < Idle);
		}

		ElysiumScene::ClearCache();
	}

	// --- The overlay slot composes over the base; it never competes for it -------------------------
	//
	// Retail layers a weapon's fire, reload and dry-fire through `CBaseAnimatingOverlay` slot 0, which
	// is accumulated on top of whatever owns the base pose rather than replacing it. Three things
	// follow, and all three are what this asserts: the layer leaves `bBasePoseOwned` alone, its own
	// activity never becomes the body's ideal activity, and the record names it separately so a layer
	// composing invisibly is readable rather than being a body that merely looks wrong.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;
		Driver.GaitSpeeds.Walk = Fan(ForwardWalk, 70.0f, 60.0f);
		Driver.Gait = ElysiumAnimIntent::GaitFrom(Driver.GaitSpeeds);

		// A shot: one layer clip on UpperBody, carrying the forced layer activity a weapon states.
		FElysiumClipSegment Shot;
		Shot.ClipName = TEXT("glock_fire_layer");
		Shot.Source = EElysiumAnimSource::Player;
		Shot.Priority = EElysiumAnimPriority::Scripted;
		Shot.Channel = EElysiumAnimChannel::UpperBody;
		Shot.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
		const FElysiumAnimationRequest ShotClaim =
			ElysiumAnimIntent::ClaimForSegment(Shot, /*PlayLengthSeconds=*/0.5f);
		const uint32 SlotHandle = Driver.SubmitRequest(ShotClaim);
		TestTrue(TEXT("the layer claim is accepted on the open UpperBody slot"), SlotHandle != 0u);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("a layer leaves the base pose with the locomotion publish"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and names no base holder"), Driver.Selection.BaseHold.IsEmpty());
		TestEqual(TEXT("...while the record names the layer that is composing"),
			Driver.Selection.SlotLabel, FString(TEXT("glock_fire_layer")));
		TestEqual(TEXT("...at full weight, because the attack family's envelope is a snap"),
			Driver.Selection.SlotWeight, ElysiumAnimIntent::SlotWeightMax);
		TestTrue(TEXT("...and on a phase of its own clip rather than the base's"),
			Driver.Selection.SlotCycle > 0.0f && Driver.Selection.SlotCycle < 1.0f);

		// The whole reason a layer claim must not reach the ideal activity: `ACT_RANGE_ATTACK1_LAYER`
		// is a layer family, and taken as the body's ideal activity it would answer for what the body
		// is DOING — engaging the melee movement lock and the reselection guard over a shot.
		TestTrue(TEXT("a layer activity never becomes the body's forced ideal activity"),
			Driver.ForcedIdealActivity().Activity.IsEmpty());
		TestFalse(TEXT("...so the layer locks no movement"), Driver.bMovementLocked);
		TestFalse(TEXT("...and gates no reselection"), Driver.bAnimationDriven);

		// The base owner changes underneath it — twice, and in both directions — and the layer is
		// untouched by either, because nothing about it was ever ranked.
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestEqual(TEXT("a travelling publish does not consume the layer"),
			Driver.Selection.SlotLabel, FString(TEXT("glock_fire_layer")));

		FElysiumAnimationRequest Beat;
		Beat.Source = EElysiumAnimSource::Scene;
		Beat.Channel = EElysiumAnimChannel::Base;
		Beat.Priority = EElysiumAnimPriority::Scene;
		Beat.Label = TEXT("jack_wave");
		TestTrue(TEXT("a scene takes the base beside the layer"), Driver.SubmitRequest(Beat) != 0u);
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestFalse(TEXT("the base is held by the scene"), Driver.Selection.bBasePoseOwned);
		TestEqual(TEXT("...and the layer is still the same layer over it"),
			Driver.Selection.SlotLabel, FString(TEXT("glock_fire_layer")));
		TestNotNull(TEXT("...standing on its own channel"),
			Driver.ActiveRequest(EElysiumAnimChannel::UpperBody));

		// Released, the record stops naming it in the same tick — a stale layer line would report a
		// body that is still shooting.
		TestTrue(TEXT("the layer's handle gives it back"), Driver.ReleaseRequest(SlotHandle));
		Driver.Tick(Dt, Travelling(ForwardWalk), nullptr, nullptr);
		TestTrue(TEXT("a released layer leaves no label on the record"),
			Driver.Selection.SlotLabel.IsEmpty());
		TestEqual(TEXT("...no weight"), Driver.Selection.SlotWeight, 0.0f);
		TestEqual(TEXT("...and no phase"), Driver.Selection.SlotCycle, 0.0f);
		TestNotNull(TEXT("...while the base claim it composed over is untouched"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
	}

	// --- A held layer run survives an ordinary base clip starting and stopping ---------------------
	//
	// The two runs on one body are independent: a held layer's claim carries no duration and only its
	// own stop path ends it, while an ordinary base clip claims for exactly its play. The handle record
	// a run is remembered by is per-channel for this reason — one handle per body confuses the two, and
	// a held claim nothing expires then parks its channel for the life of the body.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;

		FElysiumClipSegment Reload;
		Reload.ClipName = TEXT("glock_reload_layer");
		Reload.Source = EElysiumAnimSource::Player;
		Reload.Priority = EElysiumAnimPriority::Scripted;
		Reload.Channel = EElysiumAnimChannel::UpperBody;
		Reload.Activity = TEXT("ACT_RELOAD_LAYER");
		Reload.bHoldUntilReleased = true;
		const FElysiumAnimationRequest HeldClaim =
			ElysiumAnimIntent::ClaimForSegment(Reload, /*PlayLengthSeconds=*/2.0f);
		TestEqual(TEXT("a held segment claims with no duration at all"), HeldClaim.HoldSeconds, 0.0f);
		const uint32 HeldHandle = Driver.SubmitRequest(HeldClaim);
		TestTrue(TEXT("the held layer takes UpperBody"), HeldHandle != 0u);

		// The ordinary clip beside it: one segment, one claim, and the claim goes when the clip does.
		FElysiumClipSegment Ambient;
		Ambient.ClipName = TEXT("Stance_Neutral_Idle_1");
		const FElysiumAnimationRequest OnceClaim =
			ElysiumAnimIntent::ClaimForSegment(Ambient, /*PlayLengthSeconds=*/0.10f);
		TestEqual(TEXT("a band-less segment claims Base"),
			static_cast<int32>(OnceClaim.Channel), static_cast<int32>(EElysiumAnimChannel::Base));
		TestTrue(TEXT("...and is accepted beside the held layer"),
			Driver.SubmitRequest(OnceClaim) != 0u);

		// Run the ordinary clip out. Twelve standing frames is twice its length.
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		}
		TestNull(TEXT("the ordinary base claim expired with its own clip"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));
		TestNotNull(TEXT("...and the held layer is still standing on its channel"),
			Driver.ActiveRequest(EElysiumAnimChannel::UpperBody));
		TestEqual(TEXT("...with the record still naming it"),
			Driver.Selection.SlotLabel, FString(TEXT("glock_reload_layer")));
		// **And reporting the phase of its own clip, not zero.** A held claim has no expiry, so the
		// duration the record reads its phase against is the clip's own length — twelve frames at
		// 1/60 is 0.2s of a 2s layer. A held layer answering zero would be one still frame of the
		// reload pinned on the evaluator for as long as the producer held the channel.
		TestEqual(TEXT("...and reporting where it stands on its own clip, rather than freezing at 0"),
			Driver.Selection.SlotCycle, 0.10f, UE_KINDA_SMALL_NUMBER);
		TestTrue(TEXT("...so its own stop path still finds it"), Driver.ReleaseRequest(HeldHandle));
	}

	// --- The layer blend envelope: one helper, two recovered answers ------------------------------
	{
		const ElysiumAnimIntent::FSlotBlend ReloadBlend =
			ElysiumAnimIntent::SlotBlendFor(TEXT("ACT_RELOAD_LAYER"));
		TestEqual(TEXT("a reload layer blends in over a fifth of its cycle"),
			ReloadBlend.InFraction, 0.2f);
		TestEqual(TEXT("...and out over the same"), ReloadBlend.OutFraction, 0.2f);

		const ElysiumAnimIntent::FSlotBlend FireBlend =
			ElysiumAnimIntent::SlotBlendFor(TEXT("ACT_RANGE_ATTACK1_LAYER"));
		TestEqual(TEXT("an attack layer snaps in"), FireBlend.InFraction, 0.0f);
		TestEqual(TEXT("...and snaps out"), FireBlend.OutFraction, 0.0f);
		const ElysiumAnimIntent::FSlotBlend DryFireBlend =
			ElysiumAnimIntent::SlotBlendFor(TEXT("ACT_DRYFIRE_LAYER"));
		TestEqual(TEXT("...as does a dry-fire layer"), DryFireBlend.InFraction, 0.0f);

		TestEqual(TEXT("and the weight ceiling every layer is capped at is 1.0"),
			ElysiumAnimIntent::SlotWeightMax, 1.0f);

		// The translated spellings reach the same two answers. Retail renames a layer activity per
		// weapon exactly as it renames every other one, so a producer handing the per-weapon name must
		// not fall silently into the snap the attack families take.
		const ElysiumAnimIntent::FSlotBlend RenamedReload =
			ElysiumAnimIntent::SlotBlendFor(TEXT("ACT_RELOAD_LAYER_M37"));
		TestEqual(TEXT("a per-weapon reload layer still takes the reload envelope"),
			RenamedReload.InFraction, 0.2f);
		TestEqual(TEXT("...at both ends"), RenamedReload.OutFraction, 0.2f);
		const ElysiumAnimIntent::FSlotBlend RenamedAttack =
			ElysiumAnimIntent::SlotBlendFor(TEXT("ACT_RANGE_ATTACK_LAYER_SHOTGUN"));
		TestEqual(TEXT("...while a renamed attack layer is still the family that snaps"),
			RenamedAttack.InFraction, 0.0f);
	}

	// --- The slot's published weight is the ENVELOPE, never the ceiling ---------------------------
	//
	// The whole value of the record's weight line is separating a layer composing at full strength
	// from one stuck at zero, and a line that published `SlotWeightMax` answers 1.0 for both. So the
	// number is computed: a smoothstep ramp over the blend-in and blend-out fractions of the claim's
	// OWN cycle, capped at the ceiling, and gone the instant that cycle reaches 1 — which is the same
	// instant `AdvanceRequests` drops the claim.
	{
		using namespace ElysiumAnimIntent;

		FElysiumClipSegment Reload;
		Reload.ClipName = TEXT("glock_reload_layer");
		Reload.Channel = EElysiumAnimChannel::UpperBody;
		Reload.Activity = TEXT("ACT_RELOAD_LAYER");
		const FElysiumAnimationRequest Claim = ClaimForSegment(Reload, /*PlayLengthSeconds=*/2.0f);
		TestEqual(TEXT("the claim holds for exactly the clip"), Claim.HoldSeconds, 2.0f);

		TestEqual(TEXT("a reload layer is weightless on the frame it is armed"),
			SlotWeightAt(Claim, 0.0f), 0.0f);
		// Cycle 0.1 of a 0.2 ramp is the smoothstep's own midpoint, which is 0.5 exactly.
		TestEqual(TEXT("...half way up its ramp it is at the smoothstep's midpoint"),
			SlotWeightAt(Claim, 0.2f), 0.5f);
		TestEqual(TEXT("...at the ceiling through the middle of the clip"),
			SlotWeightAt(Claim, 1.0f), SlotWeightMax);
		const float Fading = SlotWeightAt(Claim, 1.9f);
		TestTrue(TEXT("...and back down the out ramp before it ends"),
			Fading > 0.0f && Fading < SlotWeightMax);
		TestEqual(TEXT("...gone when its cycle reaches 1"), SlotWeightAt(Claim, 2.0f), 0.0f);

		// The attack family's envelope is a snap, so it is at the ceiling for the whole clip and gone
		// on the frame after — no ramp at either end.
		FElysiumClipSegment Shot = Reload;
		Shot.ClipName = TEXT("glock_fire_layer");
		Shot.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
		const FElysiumAnimationRequest ShotClaim = ClaimForSegment(Shot, /*PlayLengthSeconds=*/0.5f);
		TestEqual(TEXT("an attack layer is at weight on the frame it is armed"),
			SlotWeightAt(ShotClaim, 0.0f), SlotWeightMax);
		TestEqual(TEXT("...and gone on the frame its cycle reaches 1"),
			SlotWeightAt(ShotClaim, 0.5f), 0.0f);

		// --- A duration-less claim rides its own clip, and does not stand still ---------------------
		//
		// A held or looping claim has no expiry — its producer ends it — but it still has a clip, and
		// the clip's length is what the layer's motion rides. A phase of zero is not "waiting": it is
		// ONE FRAME of the clip, pinned on the evaluator at whatever weight cycle 0 gives, for as long
		// as the producer holds the channel.
		FElysiumClipSegment Held = Reload;
		Held.bHoldUntilReleased = true;
		const FElysiumAnimationRequest HeldClaim = ClaimForSegment(Held, /*PlayLengthSeconds=*/2.0f);
		TestEqual(TEXT("a held claim carries no expiry"), HeldClaim.HoldSeconds, 0.0f);
		TestEqual(TEXT("...but does carry its clip's own length"), HeldClaim.ClipLengthSeconds, 2.0f);
		TestEqual(TEXT("...so a held reload is still weightless on the frame it is armed"),
			SlotWeightAt(HeldClaim, 0.0f), 0.0f);
		TestEqual(TEXT("...climbs its ramp on the clip's own clock"),
			SlotWeightAt(HeldClaim, 0.2f), 0.5f);
		TestEqual(TEXT("...stands at the ceiling through the middle of it"),
			SlotWeightAt(HeldClaim, 1.0f), SlotWeightMax);
		// Not looping, so the phase clamps and retail's overlay weight dies at cycle 1 whether or not
		// the producer has come back for the channel yet.
		TestEqual(TEXT("...and is gone at its clip's end, held claim or not"),
			SlotWeightAt(HeldClaim, 5.0f), 0.0f);

		// A LOOPING claim wraps instead, which is what honours the segment's own `bLoop` here: the
		// layer rides its motion again from the head and the envelope rides with it, rather than
		// running once and standing at zero weight forever.
		FElysiumClipSegment Looping = Reload;
		Looping.bLoop = true;
		const FElysiumAnimationRequest LoopClaim = ClaimForSegment(Looping, /*PlayLengthSeconds=*/2.0f);
		TestTrue(TEXT("a looping claim states that it loops"), LoopClaim.bLoop);
		TestEqual(TEXT("...and carries no expiry either"), LoopClaim.HoldSeconds, 0.0f);
		TestEqual(TEXT("a looping layer is back at the head one clip in"),
			SlotCycle(LoopClaim, 2.0f), 0.0f);
		TestEqual(TEXT("...and half way through its second pass at three clip-halves"),
			SlotCycle(LoopClaim, 3.0f), 0.5f);
		TestEqual(TEXT("...so its ramp is ridden again on the second pass"),
			SlotWeightAt(LoopClaim, 2.2f), 0.5f);
		// 101s is 50 whole passes and a half, so the layer is mid-clip on its fifty-first — at weight,
		// where a claim that clamped instead would have been gone since its first pass ended.
		TestEqual(TEXT("...and it is never gone, however long it stands"),
			SlotWeightAt(LoopClaim, 101.0f), SlotWeightMax);

		// The rate is folded into that one length, so a slowed layer rides a longer wall clock and its
		// ramp stretches with it rather than being timed against the authored seconds.
		FElysiumClipSegment Slowed = Reload;
		Slowed.bHoldUntilReleased = true;
		Slowed.PlaybackRate = 0.5f;
		const FElysiumAnimationRequest SlowClaim = ClaimForSegment(Slowed, /*PlayLengthSeconds=*/2.0f);
		TestEqual(TEXT("a half-rate layer occupies twice the wall clock"),
			SlowClaim.ClipLengthSeconds, 4.0f);
		TestEqual(TEXT("...and reaches its ramp's midpoint at twice the age"),
			SlotWeightAt(SlowClaim, 0.4f), 0.5f);

		// A claim that states no length names no clip to stand on. Composing nothing is the honest
		// answer; the ceiling would stand one frozen frame of the evaluator at full strength.
		FElysiumAnimationRequest Lengthless;
		Lengthless.Channel = EElysiumAnimChannel::UpperBody;
		Lengthless.Label = TEXT("glock_fire_layer");
		TestEqual(TEXT("a claim with no length has no phase"), SlotCycle(Lengthless, 1.0f), 0.0f);
		TestEqual(TEXT("...and composes nothing rather than freezing at the ceiling"),
			SlotWeightAt(Lengthless, 1.0f), 0.0f);
	}

	// --- The record and the assets beside it can never disagree about which layer is composing -----
	//
	// `Tick` has two player-only exits that publish the record without re-entering the resolver: an
	// animation-driven frame, and a frame whose reselection an unfinished swing refuses. Both restate
	// the slot from the CLAIM alone — so a claim that moved since the last resolve would otherwise be
	// named beside the previous layer's bank, mask and rate, and the record would read "clip B out of
	// bank A" while the graph is holding clip A.
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;

		FElysiumClipSegment FirstShot;
		FirstShot.ClipName = TEXT("glock_fire_layer");
		FirstShot.Source = EElysiumAnimSource::Player;
		FirstShot.Priority = EElysiumAnimPriority::Scripted;
		FirstShot.Channel = EElysiumAnimChannel::UpperBody;
		FirstShot.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
		const uint32 FirstHandle = Driver.SubmitRequest(
			ElysiumAnimIntent::ClaimForSegment(FirstShot, /*PlayLengthSeconds=*/0.5f));
		TestTrue(TEXT("the first layer claim is accepted"), FirstHandle != 0u);

		// One ordinary tick, which is what marks the claim as one the resolver has answered for.
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);

		// Stand in for what that resolve would have written on a body with a mount behind it: the bank
		// the include DAG named, and the baked layer that came out of it. Nothing here needs a real
		// mount — what is asserted is that a later frame cannot leave these describing another claim.
		UAnimSequence* FirstLayer = NewObject<UAnimSequence>();
		Driver.Selection.SlotOwnerStem = TEXT("shared_pc_bank");
		Driver.Assets.SlotSequence = FirstLayer;
		Driver.Assets.SlotMaskName = TEXT("upperbody_49");
		TestTrue(TEXT("the body is layering before the claim moves"), Driver.Assets.HasSlotLayer());

		// The swing that closes the selector: a melee claim on the base channel with a live clip under
		// it is retail's `vt+0x670`, and every frame of it takes the animation-driven exit.
		FElysiumAnimationRequest Swing;
		Swing.Source = EElysiumAnimSource::Player;
		Swing.Channel = EElysiumAnimChannel::Base;
		Swing.Priority = EElysiumAnimPriority::Scripted;
		Swing.Label = TEXT("baseballbat_attack_med");
		Swing.Activity = TEXT("ACT_MELEE_ATTACK");
		TestTrue(TEXT("the swing takes the base channel"), Driver.SubmitRequest(Swing) != 0u);
		Driver.BaseClipCycle.bPlaying = true;
		Driver.BaseClipCycle.Cycle = 0.20f;
		Driver.BaseClipCycle.HoldCycle = 0.90f;

		// The second shot, armed while the selector is closed.
		TestTrue(TEXT("the first layer's handle gives it back"), Driver.ReleaseRequest(FirstHandle));
		FElysiumClipSegment SecondShot = FirstShot;
		SecondShot.ClipName = TEXT("glock_fire_layer_2");
		const uint32 SecondHandle = Driver.SubmitRequest(
			ElysiumAnimIntent::ClaimForSegment(SecondShot, /*PlayLengthSeconds=*/0.5f));
		TestTrue(TEXT("...and the second claim takes the channel"), SecondHandle != 0u);

		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("the frame really did take the animation-driven exit"),
			Driver.bAnimationDriven);
		TestEqual(TEXT("the record names the layer that is actually claimed"),
			Driver.Selection.SlotLabel, FString(TEXT("glock_fire_layer_2")));
		TestTrue(TEXT("...and names no bank, because nothing has resolved this claim yet"),
			Driver.Selection.SlotOwnerStem.IsEmpty());
		TestFalse(TEXT("...so the graph is handed no layer either, rather than the last shot's"),
			Driver.Assets.HasSlotLayer());
		TestTrue(TEXT("...mask included"), Driver.Assets.SlotMaskName.IsNone());

		// --- ...and the claim is ANSWERED on that exit, rather than left unresolved forever ---------
		//
		// The exit skips the base SELECTION pass; it does not skip the slot, which composes over
		// whatever owns the base rather than participating in the choice of one. So the resolver's own
		// key moves here — and that is the whole difference between a layer that resolves on the next
		// frame of a swing and one that is republished as a named label with no asset for the swing's
		// entire length, because the key never caught up with the claim.
		TestEqual(TEXT("the animation-driven exit answers for the claim it published"),
			static_cast<int32>(Driver.LastSlotHandle), static_cast<int32>(SecondHandle));
		// Stand in for what a resolve on that frame binds on a body with a mount behind it. The next
		// animation-driven frame must leave it alone: it belongs to the claim still standing.
		UAnimSequence* SecondLayer = NewObject<UAnimSequence>();
		Driver.Assets.SlotSequence = SecondLayer;
		Driver.Assets.SlotMaskName = TEXT("upperbody_49");
		Driver.Selection.SlotOwnerStem = TEXT("shared_pc_bank");
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("...on the same exit again"), Driver.bAnimationDriven);
		TestTrue(TEXT("a layer resolved mid-swing survives the rest of the swing"),
			Driver.Assets.SlotSequence == SecondLayer);
		TestEqual(TEXT("...bank and all"), Driver.Selection.SlotOwnerStem,
			FString(TEXT("shared_pc_bank")));
		TestEqual(TEXT("...and the record still names it"), Driver.Selection.SlotLabel,
			FString(TEXT("glock_fire_layer_2")));

		// The inactive branch takes the assets too, on the same exit. Without it `HasSlotLayer()` — a
		// pointer test — stays true at weight 0 under a record that has stopped naming a layer at all.
		Driver.Assets.SlotSequence = FirstLayer;
		Driver.Assets.SlotMaskName = TEXT("upperbody_49");
		TestTrue(TEXT("the second layer's handle gives it back"), Driver.ReleaseRequest(SecondHandle));
		Driver.Tick(Dt, Travelling(0.0f), nullptr, nullptr);
		TestTrue(TEXT("...on the same animation-driven exit"), Driver.bAnimationDriven);
		TestTrue(TEXT("a released layer leaves no label on the record"),
			Driver.Selection.SlotLabel.IsEmpty());
		TestFalse(TEXT("...and no asset behind it"), Driver.Assets.HasSlotLayer());
		TestTrue(TEXT("...nor a mask"), Driver.Assets.SlotMaskName.IsNone());
	}

	// --- The per-channel run-claim row: a body runs on more than one channel at once ---------------
	//
	// A held segment records the claim its run will come back for; any other segment on the same
	// channel clears it. Both writes are scoped to the ONE channel the segment played on. A single
	// handle per body confuses a held upper-body layer with the ordinary base clip underneath it — and
	// a held claim carries no duration, so the driver never expires the orphan and that channel stays
	// locked for the life of the body.
	{
		FElysiumSegmentClaims Claims;
		TestTrue(TEXT("a body with no run holds nothing"), Claims.IsEmpty());

		Claims.Set(EElysiumAnimChannel::UpperBody, 7u);
		Claims.Set(EElysiumAnimChannel::Base, 9u);
		TestEqual(TEXT("each channel remembers its own handle"),
			static_cast<int32>(Claims.Get(EElysiumAnimChannel::UpperBody)), 7);
		TestEqual(TEXT("...and the base its own"),
			static_cast<int32>(Claims.Get(EElysiumAnimChannel::Base)), 9);

		// The bug itself: an ordinary base clip starting must not erase a standing layer's handle.
		Claims.Set(EElysiumAnimChannel::Base, 0u);
		TestEqual(TEXT("clearing the base leaves a standing layer's handle alone"),
			static_cast<int32>(Claims.Get(EElysiumAnimChannel::UpperBody)), 7);
		TestFalse(TEXT("...and the row stays, because a channel still holds a claim"),
			Claims.IsEmpty());

		Claims.Set(EElysiumAnimChannel::UpperBody, 0u);
		TestTrue(TEXT("an all-zero row reports empty, so the body leaves the map"), Claims.IsEmpty());

		// The stop path takes every channel at once and leaves nothing behind to release twice.
		Claims.Set(EElysiumAnimChannel::Base, 3u);
		Claims.Set(EElysiumAnimChannel::Gesture, 4u);
		const FElysiumSegmentClaims Taken = Claims.TakeAll();
		TestEqual(TEXT("a run gives back every channel it took"),
			static_cast<int32>(Taken.Get(EElysiumAnimChannel::Base)), 3);
		TestEqual(TEXT("...including the last of them"),
			static_cast<int32>(Taken.Get(EElysiumAnimChannel::Gesture)), 4);
		TestTrue(TEXT("...and the row it came from holds nothing after"), Claims.IsEmpty());
	}

	return true;
}

// =====================================================================================
// The same resolver, against the real corpus.
//
// The fixture above proves the *rule* — that a resolver keyed on the owner and not on the label
// separates two banks. This proves the **data** still says what the RE says it says: the player's
// `ACT_RUN` really does reach a PC-only bank the cast never touches, and `ACT_LAND_CROUCH` really
// does resolve nothing. A fixture cannot fail when an export regresses; this can.
// =====================================================================================

static constexpr EAutomationTestFlags GElysiumAnimationContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A blend-table lookup over the real sidecars, cached per owner. The owner may be a character or
	// a bank, so both halves of the index are consulted — that is the include DAG's own answer.
	struct FRealTables
	{
		const FElysiumNpcIndex* Index = nullptr;
		TMap<FString, TSharedPtr<FElysiumBlendTable>> Cache;

		const FElysiumBlendTable* operator()(const FString& Owner)
		{
			if (const TSharedPtr<FElysiumBlendTable>* Found = Cache.Find(Owner))
			{
				return Found->Get();
			}
			const FElysiumNpcIndexEntry* Entry = Index->Npcs.Find(Owner);
			if (Entry == nullptr) { Entry = Index->Banks.Find(Owner); }

			TSharedPtr<FElysiumBlendTable> Table;
			if (Entry != nullptr && !Entry->Blends.IsEmpty())
			{
				Table = MakeShared<FElysiumBlendTable>();
				FString Error;
				if (!Table->Load(Entry->Blends, Error))
				{
					Table.Reset();
				}
			}
			Cache.Add(Owner, Table);
			return Table.Get();
		}
	};

	// A player body's stem. The 56 of them are named by clan and slot, and the shipped corpus always
	// carries the Tremere male; anything else with the same shape serves if it does not.
	FString FindPlayerStem(const FElysiumNpcIndex& Index)
	{
		if (Index.Npcs.Contains(TEXT("tremere_Male_Armor_0")))
		{
			return TEXT("tremere_Male_Armor_0");
		}
		TArray<FString> Stems;
		Index.Npcs.GenerateKeyArray(Stems);
		Stems.Sort();
		for (const FString& Stem : Stems)
		{
			if (Stem.Contains(TEXT("_Male_Armor_")) || Stem.Contains(TEXT("_Female_Armor_")))
			{
				return Stem;
			}
		}
		return FString();
	}

	// The rest of the key a cast request is resolved under. It is a struct rather than three more
	// defaulted parameters because the whole point of the cast sweep is that these three are what the
	// committed tables are joined to: the classname finds the `+0x5dc`/`+0x5e0` class bodies, the
	// weapon walks the ladders, and the state picks the alert set over the relaxed one. A sweep that
	// left them empty drives none of that work and asserts a body no map stands.
	struct FCastContext
	{
		FString ActorClassname;
		FString WeaponClassname;
		EElysiumNpcState ActorState = EElysiumNpcState::Idle;
	};

	// `bLadder` defaults off: the ladder would mask a miss behind a walk or a disposition, and a miss
	// is exactly what several of the assertions below are about. The cast's own coverage turns it on,
	// because that is the chain a cast body actually resolves through at runtime.
	FElysiumAnimationSelection ResolveOn(const FElysiumNpcClipSet& Set, FRealTables& Tables,
		const TCHAR* Activity, EElysiumAnimSource Source, EElysiumAnimBodyKind BodyKind,
		bool bLadder = false, const FCastContext& Context = FCastContext())
	{
		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = [&Tables](const FString& Owner) { return Tables(Owner); };

		FElysiumAnimationIntent Intent;
		Intent.Stem = Set.Stem;
		Intent.Activity = Activity;
		Intent.Source = Source;
		Intent.BodyKind = BodyKind;
		Intent.bAllowFallbackLadder = bLadder;
		Intent.ActorClassname = Context.ActorClassname;
		Intent.WeaponClassname = Context.WeaponClassname;
		Intent.ActorState = Context.ActorState;

		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		return Out;
	}

}



// =====================================================================================
// The graph against the real corpus.
//
// The fixtures above prove the rule. This proves that the authored fades the transition arithmetic
// reads are really what the export carries; the graph asset-kind matrix is a property of what the
// bake wrote, and the character verifier owns it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerGraphTransitionParityTest,
	"Elysium.Content.PlayerGraphTransitionParity", GElysiumAnimationContentFlags)
bool FElysiumPlayerGraphTransitionParityTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index (run: uv run elysium export grid)"));
		return true;
	}

	FRealTables Tables;
	Tables.Index = &Index;

	// One real player body. The parity claim is about the authored data, so it has to be read off a
	// body the export actually carries rather than off a fixture.
	TArray<FString> Stems;
	Index.Npcs.GenerateKeyArray(Stems);
	Stems.Sort();
	FElysiumNpcClipSet Body;
	FString Chosen;
	for (const FString& Stem : Stems)
	{
		if (!Stem.Contains(TEXT("_Male_Armor_")) && !Stem.Contains(TEXT("_Female_Armor_")))
		{
			continue;
		}
		FString LoadError;
		if (Body.Load(Stem, LoadError) && Body.ByActivity(TEXT("ACT_WALK")).Num() > 0)
		{
			Chosen = Stem;
			break;
		}
	}
	if (Chosen.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the export carries no player body with a walk"));
		return true;
	}

	// The six ordered pairs the locomotion slice can actually make.
	const TCHAR* Pairs[][2] = {
		{ TEXT("ACT_IDLE"),    TEXT("ACT_WALK")    },
		{ TEXT("ACT_WALK"),    TEXT("ACT_IDLE")    },
		{ TEXT("ACT_WALK"),    TEXT("ACT_RUN")     },
		{ TEXT("ACT_RUN"),     TEXT("ACT_WALK")    },
		{ TEXT("ACT_LEAP"),    TEXT("ACT_FALLING") },
		{ TEXT("ACT_FALLING"), TEXT("ACT_LAND")    },
	};

	int32 Checked = 0;
	for (const TCHAR* (&Pair)[2] : Pairs)
	{
		const FElysiumAnimationSelection Out = ResolveOn(Body, Tables, Pair[0],
			EElysiumAnimSource::Player, EElysiumAnimBodyKind::Player);
		const FElysiumAnimationSelection In = ResolveOn(Body, Tables, Pair[1],
			EElysiumAnimSource::Player, EElysiumAnimBodyKind::Player);
		if (!Out.IsResolved() || !In.IsResolved())
		{
			continue;   // an activity this body does not carry is the coverage test's business
		}
		++Checked;
		const float Expected = In.bSnap ? 0.0f : FMath::Max(Out.FadeSeconds, In.FadeSeconds);
		// The exact value, and there is nothing left to compare it against: the graph asset carries
		// no authored duration of its own, so the combine's answer reaches the blend stack's
		// `BlendTime` pin whole and nothing downstream can min-merge it down to a cap.
		TestEqual(*FString::Printf(TEXT("%s -> %s combines the authored fades (%.2f, %.2f)"),
			Pair[0], Pair[1], Out.FadeSeconds, In.FadeSeconds),
			ElysiumAnimGraph::TransitionSeconds(&Out, In), Expected);
	}
	TestTrue(TEXT("at least one slice transition was measured"), Checked > 0);
	AddInfo(FString::Printf(TEXT("%d of 6 slice transitions measured on '%s'"), Checked, *Chosen));
	return true;
}


// =====================================================================================
// The two pickers (LIFE5). Retail collects an activity's candidates once and hands the array to
// one of two functions: `SelectWeightedSequence` (`vampire.dll 0x1008dc40` -> `FUN_10427fc0`) draws
// by authored `actweight`, and `SelectHeaviestSequence` (`0x1008dd30` -> `FUN_104280f0`) keeps the
// largest. Which one answers is latched per commit by entity flag `0x40000000`
// (`apply_player_activity_and_sequence`, `0x101644f0`), set on a commanded state change.
//
// The arithmetic is what this asserts, because the corpus cannot: `run` carries two candidates at
// weight 1 whose only difference is 30 fps against 18, so the tie rule alone decides whether a gait
// cycles at the rate its ground speed asks for.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationSelectorsTest,
	"Elysium.Substrate.AnimationSelectors", GElysiumAnimationTestFlags)
bool FElysiumAnimationSelectorsTest::RunTest(const FString&)
{
	using namespace ElysiumAnimResolve;

	// A clip that knows its place in the flat sequence space, which is what orders the candidates.
	auto Numbered = [](const TCHAR* Owner, const TCHAR* Activity, int32 Weight, int32 RawIndex)
	{
		FElysiumNpcClip Clip = MakeClip(Owner, Activity, Weight, 0x1);
		Clip.RawIndex = RawIndex;
		return Clip;
	};

	// --- the candidate order --------------------------------------------------------------------
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("ordering_body");
		// Added out of order on purpose: the array retail builds is ascending global sequence
		// number, and nothing about a TMap's iteration order may reach the pickers.
		Set.Clips.Add(TEXT("late"), Numbered(CastBank, TEXT("ACT_RUN"), 1, 40));
		Set.Clips.Add(TEXT("early"), Numbered(PcBank, TEXT("ACT_RUN"), 1, 4));
		Set.Clips.Add(TEXT("middle"), Numbered(MiscBank, TEXT("ACT_RUN"), 1, 12));
		// No slice stated this one's number, which is every row of a corpus exported before the
		// column existed.
		Set.Clips.Add(TEXT("unnumbered"), MakeClip(MiscBank, TEXT("ACT_RUN"), 1, 0x1));

		const TArray<FElysiumClipRef> Candidates = Set.ByActivity(TEXT("ACT_RUN"));
		TestEqual(TEXT("every candidate carrying the activity is collected"), Candidates.Num(), 4);
		TestEqual(TEXT("the lowest sequence number leads"), Candidates[0].Label, FString(TEXT("early")));
		TestEqual(TEXT("then the next"), Candidates[1].Label, FString(TEXT("middle")));
		TestEqual(TEXT("then the last numbered one"), Candidates[2].Label, FString(TEXT("late")));
		TestEqual(TEXT("and a row stating no number sorts behind every row that does"),
			Candidates[3].Label, FString(TEXT("unnumbered")));
	}

	// --- heaviest -------------------------------------------------------------------------------
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("heaviest_body");
		Set.Clips.Add(TEXT("light_first"), Numbered(PcBank, TEXT("ACT_IDLE"), 1, 4));
		Set.Clips.Add(TEXT("heavy"), Numbered(MiscBank, TEXT("ACT_IDLE"), 30, 9));
		Set.Clips.Add(TEXT("light_last"), Numbered(CastBank, TEXT("ACT_IDLE"), 1, 20));

		TestEqual(TEXT("the largest authored share is the answer"),
			PickHeaviest(Set, TEXT("ACT_IDLE")).Label, FString(TEXT("heavy")));
		TestTrue(TEXT("and it names the bank that declared it"),
			PickHeaviest(Set, TEXT("ACT_IDLE")).Owner.Equals(MiscBank));
		TestTrue(TEXT("an activity no clip carries answers nothing"),
			PickHeaviest(Set, TEXT("ACT_SWIM")).IsEmpty());
		TestTrue(TEXT("and so does an empty activity"),
			PickHeaviest(Set, FString()).IsEmpty());
	}

	// **The tie rule, which is the whole reason this picker is not the draw.** Retail compares
	// `best < candidate`, so an equal weight never displaces the entry already held and the answer
	// is the FIRST candidate at the maximum — the lowest global sequence number. This is the shape
	// of the shipped `run` pair exactly.
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("tie_body");
		// The higher-numbered copy is added first, so insertion order and sequence order disagree
		// and only one of them can be producing the answer.
		Set.Clips.Add(TEXT("run"), Numbered(CastBank, TEXT("ACT_RUN"), 1, 7));
		Set.Clips.Add(TEXT("run"), Numbered(PcBank, TEXT("ACT_RUN"), 1, 4));

		const FElysiumClipRef Tied = PickHeaviest(Set, TEXT("ACT_RUN"));
		TestEqual(TEXT("a tie keeps the lowest sequence number"), Tied.Owner, FString(PcBank));
		TestEqual(TEXT("...which is one label under two banks"), Tied.Label, FString(TEXT("run")));

		// **It spends no randomness**, which its signature states: there is no selection token to
		// hand it. Asserted anyway against repeated calls, because a picker that reached for a
		// stream would answer differently the second time and nothing else here would notice.
		for (int32 Call = 0; Call < 8; ++Call)
		{
			TestEqual(TEXT("and every call answers the same"),
				PickHeaviest(Set, TEXT("ACT_RUN")).Owner, FString(PcBank));
		}
	}

	// --- the draw's own arithmetic ---------------------------------------------------------------
	//
	// **A zero share is a zero chance.** Flooring every weight at 1 hands a clip the author wrote
	// out of the draw a slice of it, which is a candidate appearing where retail has none.
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("zero_share_body");
		Set.Clips.Add(TEXT("never"), Numbered(PcBank, TEXT("ACT_IDLE"), 0, 4));
		Set.Clips.Add(TEXT("always"), Numbered(MiscBank, TEXT("ACT_IDLE"), 5, 9));

		for (int32 Variant = 0; Variant < 16; ++Variant)
		{
			TestEqual(*FString::Printf(TEXT("variant %d never draws the zero-weight clip"), Variant),
				PickWeighted(Set, TEXT("ACT_IDLE"), Variant).Label, FString(TEXT("always")));
		}
	}

	// **A candidate set whose shares sum to nothing is drawn uniformly**, which is retail's own
	// second branch: with no share to divide there is nothing to walk, so the roll addresses the
	// candidates directly instead of the first one being certain.
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("zero_sum_body");
		Set.Clips.Add(TEXT("a"), Numbered(PcBank, TEXT("ACT_IDLE"), 0, 4));
		Set.Clips.Add(TEXT("b"), Numbered(MiscBank, TEXT("ACT_IDLE"), 0, 9));
		Set.Clips.Add(TEXT("c"), Numbered(CastBank, TEXT("ACT_IDLE"), 0, 20));

		TSet<FString> Seen;
		for (int32 Variant = 0; Variant < 24; ++Variant)
		{
			const FElysiumClipRef Drawn = PickWeighted(Set, TEXT("ACT_IDLE"), Variant);
			TestFalse(TEXT("a zero-sum draw still answers"), Drawn.IsEmpty());
			Seen.Add(Drawn.Label);
		}
		TestTrue(TEXT("and it addresses more than the first candidate"), Seen.Num() > 1);

		// The heaviest arm over the same set is still deterministic: every weight ties at zero, so
		// the lowest sequence number wins outright.
		TestEqual(TEXT("heaviest over an all-zero set takes the lowest sequence number"),
			PickHeaviest(Set, TEXT("ACT_IDLE")).Label, FString(TEXT("a")));
	}

	// --- the fork reaches the resolver -----------------------------------------------------------
	//
	// The two arms are one call apart in `TryActivity`, so the intent's own mode is what a producer
	// changes. `run` is the fixture again: same label, two banks, equal weight.
	{
		FElysiumNpcClipSet Set;
		Set.Stem = TEXT("fork_body");
		Set.Clips.Add(TEXT("run"), Numbered(PcBank, TEXT("ACT_RUN"), 1, 4));
		Set.Clips.Add(TEXT("run"), Numbered(CastBank, TEXT("ACT_RUN"), 1, 7));

		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = [](const FString&) -> const FElysiumBlendTable* { return nullptr; };

		FElysiumAnimationIntent Intent;
		Intent.Stem = Set.Stem;
		Intent.Route = EElysiumAnimRoute::Activity;
		Intent.Activity = TEXT("ACT_RUN");
		Intent.Source = EElysiumAnimSource::Player;
		Intent.BodyKind = EElysiumAnimBodyKind::Player;
		Intent.bAllowFallbackLadder = false;

		TestEqual(TEXT("the default mode is the draw, which is what every producer had"),
			static_cast<int32>(Intent.Select), static_cast<int32>(EElysiumAnimSelect::Weighted));

		Intent.Select = EElysiumAnimSelect::Heaviest;
		FElysiumAnimationSelection Canonical;
		Resolve(Intent, Catalog, Canonical);
		TestEqual(TEXT("a commanded state change commits the canonical clip"),
			Canonical.OwnerStem, FString(PcBank));
		TestEqual(TEXT("...and both candidates were on the table"), Canonical.Candidates, 2);

		// Both arms see the same candidate array; only the pick differs.
		Intent.Select = EElysiumAnimSelect::Weighted;
		FElysiumAnimationSelection Drawn;
		Resolve(Intent, Catalog, Drawn);
		TestEqual(TEXT("the draw sees the same candidates"), Drawn.Candidates, 2);
		TestFalse(TEXT("and still answers one of them"), Drawn.OwnerStem.IsEmpty());
	}

	// **The player's own locomotion request states the canonical arm**, because that is the request
	// retail's `apply_player_activity_and_sequence` serves and the commit its state-change flag is
	// latched for. A cast gait keeps the draw: retail's setter is a `CBasePlayer` virtual.
	{
		const FElysiumGaitReference Gait;
		const FElysiumJumpLatch Latch;
		const FElysiumEntityHandle NoCharacter;
		const FElysiumAnimationIntent Player = ElysiumAnimIntent::BuildLocomotionIntent(
			Moving(0.0f), Latch, Gait, EElysiumAnimSource::Player,
			EElysiumAnimBodyKind::Player, TEXT("male_pc"), NoCharacter, 0);
		TestEqual(TEXT("the player's locomotion commit takes the canonical clip"),
			static_cast<int32>(Player.Select), static_cast<int32>(EElysiumAnimSelect::Heaviest));

		const FElysiumAnimationIntent Cast = ElysiumAnimIntent::BuildLocomotionIntent(
			Moving(0.0f), Latch, Gait, EElysiumAnimSource::Npc,
			EElysiumAnimBodyKind::Cast, TEXT("cast_body"), NoCharacter, 0);
		TestEqual(TEXT("a cast gait keeps the draw, whose retail rule this rung has not recovered"),
			static_cast<int32>(Cast.Select), static_cast<int32>(EElysiumAnimSelect::Weighted));
	}

	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
