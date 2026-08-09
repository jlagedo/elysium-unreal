#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimationResolve.h"

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

	// --- Hysteresis, which is required rather than polish ------------------------------------------
	{
		// Without it a body decelerating through the split flickers between the two gaits, advancing
		// the request generation every frame and defeating "resolve once when the request changes".
		FElysiumJumpLatch Latch;
		Step(Latch, Moving(Gait.RunSpeedCmPerSecond), Gait);
		TestTrue(TEXT("the run is latched"), Latch.bLastGaitWasRun);

		const float Split = Gait.RunSplitSpeed();
		const float Margin = Gait.HysteresisSpeed();

		TestEqual(TEXT("below the split but inside the margin, the run holds"),
			AsInt(Step(Latch, Moving(Split - Margin * 0.5f), Gait)),
			AsInt(EElysiumAnimActivityCode::RunRelaxed));
		TestEqual(TEXT("past the margin it walks, and only then"),
			AsInt(Step(Latch, Moving(Split - Margin * 2.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
		// And it does not flick back on the way down.
		TestEqual(TEXT("and it stays walking as it slows further"),
			AsInt(Step(Latch, Moving(Split - Margin * 3.0f), Gait)),
			AsInt(EElysiumAnimActivityCode::WalkRelaxed));
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
		// Every NPC and the `elysium.SourceMovement 0` capsule body reach this path: their movement
		// component's jump is an impulse, so the hold window is permanently zero and the press edge
		// never fires. They must still reach falling and landing from the ground transitions alone.
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

	// --- Step 3, the translation pass ---------------------------------------------------------------
	{
		const FElysiumTranslationResult Walk = TranslateActivity(TEXT("ACT_WALK_RELAXED"),
			FString(), FString());
		TestEqual(TEXT("the relaxed walk translates"), Walk.Resolved, FString(TEXT("ACT_WALK")));
		// The row's authored bit is REPORTED. Nothing branches on it, and the assertion that says so
		// lives in the resolver test — the pinned server translator never reads the third dword, so
		// giving it a gate would be giving it behaviour retail does not have.
		TestEqual(TEXT("the authored required bit rides along as provenance"), Walk.bRequired, true);
		TestEqual(TEXT("a miss falls back to what came in"), Walk.Incoming,
			FString(TEXT("ACT_WALK_RELAXED")));

		const FElysiumTranslationResult Run = TranslateActivity(TEXT("ACT_RUN_RELAXED"),
			FString(), FString());
		TestEqual(TEXT("the relaxed run translates"), Run.Resolved, FString(TEXT("ACT_RUN")));

		// **The weapon seam is a real pass over an empty table, not a bypass.** With no weapon the
		// loop runs and terminates on retail's own stop condition, and the first weapon answer equals
		// the incoming activity by construction rather than by accident.
		TestEqual(TEXT("the first weapon answer is the incoming activity"), Run.FirstWeaponActivity,
			FString(TEXT("ACT_RUN_RELAXED")));
		TestEqual(TEXT("and so is the last"), Run.WeaponActivity, FString(TEXT("ACT_RUN_RELAXED")));

		const FElysiumTranslationResult Idle = TranslateActivity(TEXT("ACT_IDLE"),
			FString(), FString());
		TestEqual(TEXT("an activity with no row passes through unchanged"), Idle.Resolved,
			FString(TEXT("ACT_IDLE")));
		TestEqual(TEXT("and no row applied"), Idle.Iterations, 0);
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
			EElysiumAnimSource::Player, TEXT("tremere_Male_Armor_0"),
			FElysiumEntityHandle(7, 1), 3);

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
			EElysiumAnimSource::Player, TEXT("tremere_Male_Armor_0"),
			FElysiumEntityHandle(7, 1), 0);
		TestEqual(TEXT("the land is a one-shot"), Land.bLoop, false);

		FElysiumJumpLatch Grounded;
		FElysiumLocomotionSample Crouching = Moving(0.0f);
		Crouching.Stance = EElysiumStance::Ducked;
		const FElysiumAnimationIntent Crouch = BuildLocomotionIntent(Crouching, Grounded, Gait,
			EElysiumAnimSource::Npc, TEXT("regular_cop"), FElysiumEntityHandle(), 0);
		TestTrue(TEXT("a held crouch is not"), Crouch.bLoop);
		TestEqual(TEXT("and the cast comes through the same builder"),
			static_cast<int32>(Crouch.Source), static_cast<int32>(EElysiumAnimSource::Npc));
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

	FElysiumAnimationIntent ActivityIntent(const TCHAR* Stem, const TCHAR* Activity,
		EElysiumAnimSource Source = EElysiumAnimSource::Player)
	{
		FElysiumAnimationIntent Intent;
		Intent.Stem = Stem;
		Intent.Activity = Activity;
		Intent.Source = Source;
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
		ActivityIntent(TEXT("cast_body"), TEXT("ACT_RUN"), EElysiumAnimSource::Npc),
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
		Backpedal.Body.MoveYawVelocity = 180.0f;
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

	// --- A required override that misses is still just a miss ---------------------------------------------
	{
		// `CBaseCombatWeapon::ActivityOverride` never reads a row's third dword, so the flagged rows
		// and the optional ones take the same availability path. Both actor rows are flagged, so a
		// body carrying the relaxed form and NOT the translated one must fall back to what came in —
		// if the resolver branched on the bit, this would report a catalog error instead.
		FElysiumNpcClipSet RelaxedOnly;
		RelaxedOnly.Stem = TEXT("relaxed_body");
		RelaxedOnly.Clips.Add(TEXT("run_relaxed"),
			MakeClip(CastBank, TEXT("ACT_RUN_RELAXED"), 30, 0x1));
		FElysiumAnimationCatalog RelaxedCatalog;
		RelaxedCatalog.Clips = &RelaxedOnly;
		RelaxedCatalog.BlendTableFor = Tables;

		FElysiumAnimationSelection Fell;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("relaxed_body"), TEXT("ACT_RUN_RELAXED")), RelaxedCatalog, Fell);
		TestEqual(TEXT("a flagged override with no sequence falls back like any other"),
			static_cast<int32>(Fell.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::TranslatedFallback));
		TestEqual(TEXT("landing on the activity that came in"), Fell.ResolvedActivity,
			FString(TEXT("ACT_RUN_RELAXED")));
		TestEqual(TEXT("and it plays the sequence that body actually carries"), Fell.SequenceLabel,
			FString(TEXT("run_relaxed")));
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
			ActivityIntent(TEXT("walker"), TEXT("ACT_RUN"), EElysiumAnimSource::Npc),
			WalkCatalog, Retried);
		TestEqual(TEXT("a missing run retries the walk"), static_cast<int32>(Retried.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::RunToWalk));
		TestEqual(TEXT("and lands on the walk's label"), Retried.SequenceLabel,
			FString(TEXT("walk")));
		TestEqual(TEXT("while the logical request stays what was asked for"),
			Retried.RequestedActivity, FString(TEXT("ACT_RUN")));

		FElysiumAnimationSelection Disposed;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("cast_body"), TEXT("ACT_COWER"), EElysiumAnimSource::Npc),
			CastCatalog, Disposed);
		TestEqual(TEXT("a remaining miss retries the whole request as a disposition"),
			static_cast<int32>(Disposed.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::Disposition));
		TestEqual(TEXT("landing on the stance idle"), Disposed.SequenceLabel,
			FString(TEXT("Stance_Normal_Idle_1")));

		FElysiumAnimationSelection Zero;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("walker"), TEXT("ACT_COWER"), EElysiumAnimSource::Npc),
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
			ElysiumAnimResolve::PickWeighted(Pc, TEXT("ACT_IDLE"), 0));
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

	// --- The layer binding keeps its declaration order ---------------------------------------------------
	{
		// An overlay declared after an additive overwrites the bones it owns, so a consumer that
		// sorted, deduped or assumed overlay-first would compose the host differently from retail.
		FElysiumAnimationSelection Walk;
		ElysiumAnimResolve::Resolve(
			ActivityIntent(TEXT("cast_body"), TEXT("ACT_WALK"), EElysiumAnimSource::Npc),
			CastCatalog, Walk);
		if (TestEqual(TEXT("the host's two layers travel with the selection"), Walk.LayerLabels.Num(),
			2))
		{
			TestEqual(TEXT("the additive stays first, because that is what the model declared"),
				Walk.LayerLabels[0], FString(TEXT("pistol_aim_layer")));
			TestEqual(TEXT("and the overlay second"), Walk.LayerLabels[1],
				FString(TEXT("pistol_aim_overlay")));
		}
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
		FElysiumAnimationSelection Sideways;
		ElysiumAnimResolve::Resolve(Strafing, PcCatalog, Sideways);
		TestEqual(TEXT("the published speed is the body's own"), Sideways.Speed, 400.0f);
		TestEqual(TEXT("the published move_yaw steers the fan"), Sideways.AxisValue[0], 90.0f);
		TestEqual(TEXT("which lands on the right-strafe cell"), Sideways.AnimationName,
			FString(TEXT("run_90")));
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

		// The lying-down and damaged stance idles, the corpus's longest.
		const FElysiumAnimationSelection Long = Faded(0.5f);
		TestEqual(TEXT("the longest authored pair is the ceiling the asset bakes"),
			TransitionSeconds(&Long, Long), TransitionCeilingSeconds);

		// A fresh body has nothing to fade FROM. Taking a default-constructed record's zero would
		// make the first clip of every map snap.
		TestEqual(TEXT("a body that has played nothing takes the incoming fade alone"),
			TransitionSeconds(nullptr, Idle), 0.3f);

		// `flags & 0x2`, the most common authored transition behaviour in the corpus, and a property
		// of the clip being ENTERED rather than of whatever is running.
		const FElysiumAnimationSelection Snap = Faded(0.3f, /*bSnap=*/true);
		TestEqual(TEXT("an incoming hard cut overrides the pair entirely"),
			TransitionSeconds(&Long, Snap), 0.0f);
		TestEqual(TEXT("and it is the incoming clip's property, so an outgoing snap does not cut"),
			TransitionSeconds(&Snap, Long), 0.5f);
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

		// Reachable but outside the slice. Standing is a stated answer, not a hole.
		TestEqual(TEXT("swimming stands rather than falling through the projection"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Swim)), AsInt(EElysiumGraphState::Idle));
		TestEqual(TEXT("and so does an activity this slice cannot name"),
			AsInt(StateForActivity(EElysiumAnimActivityCode::Unknown)),
			AsInt(EElysiumGraphState::Idle));

		// The record carries the LOGICAL request, so the projection reads it off the un-translated
		// activity the way the resolver's own record does.
		FElysiumAnimationSelection Sel;
		Sel.RequestedActivity = ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::Sneak);
		TestEqual(TEXT("a selection projects off its requested activity"),
			AsInt(StateFor(Sel)), AsInt(EElysiumGraphState::Sneak));
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

	FElysiumAnimationSelection ResolveOn(const FElysiumNpcClipSet& Set, FRealTables& Tables,
		const TCHAR* Activity, EElysiumAnimSource Source)
	{
		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = [&Tables](const FString& Owner) { return Tables(Owner); };

		FElysiumAnimationIntent Intent;
		Intent.Stem = Set.Stem;
		Intent.Activity = Activity;
		Intent.Source = Source;
		// The ladder would mask a miss behind a walk or a disposition, and a miss is exactly what two
		// of the assertions below are about.
		Intent.bAllowFallbackLadder = false;

		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationBankOwnershipTest,
	"Elysium.Content.AnimationBankOwnership", GElysiumAnimationContentFlags)
bool FElysiumAnimationBankOwnershipTest::RunTest(const FString&)
{
	// A skip WARNS rather than informs, so a run that validated nothing lands in
	// succeededWithWarnings instead of reporting a clean pass.
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddWarning(TEXT("skipping: the npc export domain is marked incomplete"));
		return true;
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("skipping: no exported npc index (run: uv run elysium export grid)"));
		return true;
	}

	FRealTables Tables;
	Tables.Index = &Index;

	const FString PlayerStem = FindPlayerStem(Index);
	if (PlayerStem.IsEmpty())
	{
		AddInfo(TEXT("skipping: the export carries no player body"));
		return true;
	}

	FElysiumNpcClipSet Player;
	if (!TestTrue(TEXT("the player body's vocabulary loads"), Player.Load(PlayerStem, Error)))
	{
		AddError(Error);
		return true;
	}

	// --- The acceptance clause, against the export ------------------------------------------------
	{
		const FElysiumAnimationSelection Run = ResolveOn(Player, Tables, TEXT("ACT_RUN"),
			EElysiumAnimSource::Player);
		TestEqual(TEXT("the player's ACT_RUN resolves the label `run`"), Run.SequenceLabel,
			FString(TEXT("run")));
		TestTrue(FString::Printf(
			TEXT("and it comes off the PC-only bank, not the shared one (owner was '%s')"),
			*Run.OwnerStem),
			Run.OwnerStem.Contains(TEXT("runotherspc_pcidles_allsequences")));
		TestEqual(TEXT("a movement fan resolves to a blend space"),
			static_cast<int32>(Run.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::BlendSpace));

		const FElysiumAnimationSelection Walk = ResolveOn(Player, Tables, TEXT("ACT_WALK"),
			EElysiumAnimSource::Player);
		TestEqual(TEXT("the walk resolves its own label"), Walk.SequenceLabel,
			FString(TEXT("walk")));
		TestTrue(FString::Printf(TEXT("off the shared bank (owner was '%s')"), *Walk.OwnerStem),
			Walk.OwnerStem.Contains(TEXT("move_and_ranged")));
		// The two banks on one body: the run and the walk of the SAME character come from different
		// glbs, which is the fact a label-keyed resolver erases.
		TestTrue(TEXT("so one body's run and walk do not share a bank"),
			Run.OwnerStem != Walk.OwnerStem);

		const FElysiumAnimationSelection Sneak = ResolveOn(Player, Tables, TEXT("ACT_SNEAK"),
			EElysiumAnimSource::Player);
		TestEqual(TEXT("the sneak resolves"), Sneak.SequenceLabel, FString(TEXT("sneak")));
		TestTrue(TEXT("off the shared bank too"),
			Sneak.OwnerStem.Contains(TEXT("move_and_ranged")));
	}

	// --- The idle, and the fade its own bank authored -------------------------------------------------
	{
		const FElysiumAnimationSelection Idle = ResolveOn(Player, Tables, TEXT("ACT_IDLE"),
			EElysiumAnimSource::Player);
		TestTrue(FString::Printf(TEXT("the idle resolves one of the weighted set ('%s')"),
			*Idle.SequenceLabel),
			Idle.SequenceLabel.StartsWith(TEXT("idle")) || Idle.SequenceLabel.StartsWith(TEXT("fidget")));
		TestTrue(TEXT("out of the misc bank"), Idle.OwnerStem.Contains(TEXT("misc")));
		// 0.3 s, not the 0.2 that nearly every other sequence in the game carries.
		TestEqual(TEXT("carrying the 0.3 second fade its bank authored"), Idle.FadeSeconds, 0.3f);
	}

	// --- The crouch is an into-pose, not a loop ---------------------------------------------------------
	{
		const FElysiumAnimationSelection Crouch = ResolveOn(Player, Tables, TEXT("ACT_CROUCH"),
			EElysiumAnimSource::Player);
		TestEqual(TEXT("the crouch resolves"), Crouch.SequenceLabel, FString(TEXT("crouch")));
		TestEqual(TEXT("and it does not loop, so something has to hold its final frame"),
			Crouch.bLooping, false);
	}

	// --- The jump chain, and the one activity that resolves nothing ---------------------------------------
	{
		for (const TCHAR* Activity : { TEXT("ACT_LEAP"), TEXT("ACT_FALLING"), TEXT("ACT_LAND") })
		{
			const FElysiumAnimationSelection Air = ResolveOn(Player, Tables, Activity,
				EElysiumAnimSource::Player);
			TestEqual(FString::Printf(TEXT("%s resolves"), Activity),
				static_cast<int32>(Air.Outcome), static_cast<int32>(EElysiumAnimOutcome::Resolved));
		}

		// The controlled corpus records the one ducked state-8 request returning -1. The export has to
		// agree with the capture, and the record has to name the miss rather than substitute ACT_LAND.
		const FElysiumAnimationSelection LandCrouch = ResolveOn(Player, Tables,
			TEXT("ACT_LAND_CROUCH"), EElysiumAnimSource::Player);
		TestEqual(TEXT("ACT_LAND_CROUCH resolves nothing on a validated player body"),
			static_cast<int32>(LandCrouch.Outcome),
			static_cast<int32>(EElysiumAnimOutcome::MissingSequence));
		TestEqual(TEXT("and nothing is substituted for it"),
			static_cast<int32>(LandCrouch.AssetKind),
			static_cast<int32>(EElysiumAnimAssetKind::None));
	}

	// --- The relaxed forms, which is why the translation seam is load-bearing -------------------------------
	{
		// A player body carries no sequence for either relaxed gait — only weapon-suffixed variants —
		// so without step 3's table the classifier's own output would select nothing at all.
		for (const TCHAR* Relaxed : { TEXT("ACT_WALK_RELAXED"), TEXT("ACT_RUN_RELAXED") })
		{
			TestEqual(FString::Printf(TEXT("%s has no sequence of its own"), Relaxed),
				Player.ByActivity(Relaxed).Num(), 0);
		}
		// And with the table applied, the same request resolves. That is the seam doing work.
		const ElysiumAnimIntent::FElysiumTranslationResult T =
			ElysiumAnimIntent::TranslateActivity(TEXT("ACT_RUN_RELAXED"), FString(), FString());
		TestEqual(TEXT("which the actor table turns into ACT_RUN"), T.Resolved,
			FString(TEXT("ACT_RUN")));
		TestTrue(TEXT("and that one does resolve"),
			ResolveOn(Player, Tables, *T.Resolved, EElysiumAnimSource::Player).IsResolved());
	}

	// --- The cast's half: the same label, the other bank ------------------------------------------------
	{
		TArray<FString> Stems;
		Index.Npcs.GenerateKeyArray(Stems);
		Stems.Sort();
		FElysiumNpcClipSet Cast;
		FString CastStem;
		for (const FString& Stem : Stems)
		{
			if (Stem.Contains(TEXT("_Armor_")))
			{
				continue;   // a player body; this half is about everyone else
			}
			FElysiumNpcClipSet Candidate;
			FString LoadError;
			if (Candidate.Load(Stem, LoadError) && Candidate.ByActivity(TEXT("ACT_RUN")).Num() > 0)
			{
				Cast = MoveTemp(Candidate);
				CastStem = Stem;
				break;
			}
		}
		if (CastStem.IsEmpty())
		{
			AddInfo(TEXT("no exported cast body carries ACT_RUN; the cast half is not asserted"));
			return true;
		}

		const FElysiumAnimationSelection CastRun = ResolveOn(Cast, Tables, TEXT("ACT_RUN"),
			EElysiumAnimSource::Npc);
		TestTrue(FString::Printf(TEXT("'%s' runs off the shared bank (owner was '%s')"), *CastStem,
			*CastRun.OwnerStem),
			CastRun.OwnerStem.Contains(TEXT("move_and_ranged")));
		// The cells are spelled apart too, which is the numeric proof the export has not aliased the
		// two fans onto one set of clips.
		TestTrue(FString::Printf(TEXT("and its cells are the npc_run_* set ('%s')"),
			*CastRun.AnimationName),
			CastRun.AnimationName.StartsWith(TEXT("npc_run")));
	}

	return true;
}

// The slice-acceptance rule, applied: every activity the classifier can emit either resolves on every
// player body or is a NAMED miss. A slice is not accepted while one of its own requests has no answer
// on a body it ships with (`docs/architecture/animation-architecture.md` section 3.6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationSliceCoverageTest,
	"Elysium.Content.AnimationSliceCoverage", GElysiumAnimationContentFlags)
bool FElysiumAnimationSliceCoverageTest::RunTest(const FString&)
{
	// A skip WARNS rather than informs, so a run that validated nothing lands in
	// succeededWithWarnings instead of reporting a clean pass.
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddWarning(TEXT("skipping: the npc export domain is marked incomplete"));
		return true;
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("skipping: no exported npc index (run: uv run elysium export grid)"));
		return true;
	}

	FRealTables Tables;
	Tables.Index = &Index;

	// The whole locomotion slice, less the water pair — swimming is reachable but outside what the
	// controlled corpus witnessed, so it is not held to the slice rule yet.
	const EElysiumAnimActivityCode Slice[] =
	{
		EElysiumAnimActivityCode::Idle,
		EElysiumAnimActivityCode::Walk,
		EElysiumAnimActivityCode::Run,
		EElysiumAnimActivityCode::Sneak,
		EElysiumAnimActivityCode::Crouch,
		EElysiumAnimActivityCode::Leap,
		EElysiumAnimActivityCode::Falling,
		EElysiumAnimActivityCode::Land,
		EElysiumAnimActivityCode::LandCrouch,
	};

	TArray<FString> Stems;
	Index.Npcs.GenerateKeyArray(Stems);
	Stems.Sort();

	int32 BodiesChecked = 0;
	TSet<FString> UnexplainedMisses;
	for (const FString& Stem : Stems)
	{
		if (!Stem.Contains(TEXT("_Male_Armor_")) && !Stem.Contains(TEXT("_Female_Armor_")))
		{
			continue;
		}
		FElysiumNpcClipSet Body;
		FString LoadError;
		if (!Body.Load(Stem, LoadError))
		{
			continue;
		}
		++BodiesChecked;

		for (EElysiumAnimActivityCode Code : Slice)
		{
			const FElysiumAnimationSelection Sel = ResolveOn(Body, Tables,
				ElysiumAnimIntent::ActivityName(Code), EElysiumAnimSource::Player);
			if (Sel.IsResolved() || Code == EElysiumAnimActivityCode::LandCrouch)
			{
				continue;
			}
			UnexplainedMisses.Add(FString::Printf(TEXT("%s on %s (%s)"),
				ElysiumAnimIntent::ActivityName(Code), *Stem,
				ElysiumAnimIntent::OutcomeName(Sel.Outcome)));
		}
	}

	if (BodiesChecked == 0)
	{
		AddInfo(TEXT("skipping: the export carries no player bodies"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d player bodies checked across %d activities"), BodiesChecked,
		UE_ARRAY_COUNT(Slice)));

	// `ACT_LAND_CROUCH` is the one named miss, and it is named in the code rather than tolerated here.
	for (const FString& Miss : UnexplainedMisses)
	{
		AddError(FString::Printf(TEXT("unexplained miss inside the accepted slice: %s"), *Miss));
	}
	TestEqual(TEXT("every slice activity resolves on every player body, or is the one named miss"),
		UnexplainedMisses.Num(), 0);
	return true;
}

// =====================================================================================
// CCC5 — the graph against the real corpus.
//
// The fixtures above prove the rules. These prove the two things a fixture cannot: that the
// authored fades the transition arithmetic reads are really what the export carries, and that the
// asset kind each graph state drives is really what the resolver answers with.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerGraphTransitionParityTest,
	"Elysium.Content.PlayerGraphTransitionParity", GElysiumAnimationContentFlags)
bool FElysiumPlayerGraphTransitionParityTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddWarning(TEXT("skipping: the npc export domain is marked incomplete"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("skipping: no exported npc index (run: uv run elysium export grid)"));
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
		AddInfo(TEXT("skipping: the export carries no player body with a walk"));
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
			EElysiumAnimSource::Player);
		const FElysiumAnimationSelection In = ResolveOn(Body, Tables, Pair[1],
			EElysiumAnimSource::Player);
		if (!Out.IsResolved() || !In.IsResolved())
		{
			continue;   // an activity this body does not carry is the coverage test's business
		}
		++Checked;
		const float Expected = In.bSnap ? 0.0f : FMath::Max(Out.FadeSeconds, In.FadeSeconds);
		TestEqual(*FString::Printf(TEXT("%s -> %s combines the authored fades (%.2f, %.2f)"),
			Pair[0], Pair[1], Out.FadeSeconds, In.FadeSeconds),
			ElysiumAnimGraph::TransitionSeconds(&Out, In), Expected);

		// The runtime answer has to stay under the ceiling the graph asset bakes, or the min-merge
		// that makes the ceiling a safety net silently becomes a cap on the authored value.
		TestTrue(*FString::Printf(TEXT("%s -> %s stays under the baked ceiling"), Pair[0], Pair[1]),
			ElysiumAnimGraph::TransitionSeconds(&Out, In)
				<= ElysiumAnimGraph::TransitionCeilingSeconds);
	}
	TestTrue(TEXT("at least one slice transition was measured"), Checked > 0);
	AddInfo(FString::Printf(TEXT("%d of 6 slice transitions measured on '%s'"), Checked, *Chosen));
	return true;
}

// Which asset kind each state drives, against the real corpus. A graph state and a resolver answer
// that disagree about node type is a pose that silently never plays: the blend-space branch of a
// gait state would sit on a null fan while the sequence branch held the pose it was never given.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerGraphAssetKindsTest,
	"Elysium.Content.PlayerGraphAssetKinds", GElysiumAnimationContentFlags)
bool FElysiumPlayerGraphAssetKindsTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddWarning(TEXT("skipping: the npc export domain is marked incomplete"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("skipping: no exported npc index (run: uv run elysium export grid)"));
		return true;
	}

	FRealTables Tables;
	Tables.Index = &Index;

	TArray<FString> Stems;
	Index.Npcs.GenerateKeyArray(Stems);
	Stems.Sort();

	int32 Bodies = 0;
	TSet<FString> Wrong;
	for (const FString& Stem : Stems)
	{
		if (!Stem.Contains(TEXT("_Male_Armor_")) && !Stem.Contains(TEXT("_Female_Armor_")))
		{
			continue;
		}
		FElysiumNpcClipSet Body;
		FString LoadError;
		if (!Body.Load(Stem, LoadError))
		{
			continue;
		}
		++Bodies;

		// The three gaits are the fan states; everything else in the slice plays one clip.
		const TCHAR* Gaits[] = { TEXT("ACT_WALK"), TEXT("ACT_RUN"), TEXT("ACT_SNEAK") };
		for (const TCHAR* Activity : Gaits)
		{
			const FElysiumAnimationSelection Sel = ResolveOn(Body, Tables, Activity,
				EElysiumAnimSource::Player);
			if (Sel.IsResolved() && Sel.AssetKind != EElysiumAnimAssetKind::BlendSpace)
			{
				Wrong.Add(FString::Printf(TEXT("%s on %s resolved %s, not a fan"), Activity, *Stem,
					ElysiumAnimIntent::AssetKindName(Sel.AssetKind)));
			}
		}
		const TCHAR* Singles[] = { TEXT("ACT_IDLE"), TEXT("ACT_CROUCH"), TEXT("ACT_LEAP"),
			TEXT("ACT_FALLING"), TEXT("ACT_LAND") };
		for (const TCHAR* Activity : Singles)
		{
			const FElysiumAnimationSelection Sel = ResolveOn(Body, Tables, Activity,
				EElysiumAnimSource::Player);
			if (Sel.IsResolved() && Sel.AssetKind != EElysiumAnimAssetKind::Sequence)
			{
				Wrong.Add(FString::Printf(TEXT("%s on %s resolved %s, not one clip"), Activity, *Stem,
					ElysiumAnimIntent::AssetKindName(Sel.AssetKind)));
			}
		}
	}
	if (Bodies == 0)
	{
		AddInfo(TEXT("skipping: the export carries no player bodies"));
		return true;
	}
	for (const FString& Line : Wrong)
	{
		AddError(Line);
	}
	AddInfo(FString::Printf(TEXT("%d player bodies checked"), Bodies));
	TestEqual(TEXT("every slice state drives the asset kind the resolver answers with"),
		Wrong.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
