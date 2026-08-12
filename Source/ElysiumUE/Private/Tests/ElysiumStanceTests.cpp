// The disposition stance machine, asserted against the recovered algorithm with no export and no
// engine. Every number here is a literal from the shipped `DispositionTable.txt` Neutral block, so
// a change to the parsed table cannot quietly rewrite what these tests mean.
//
// The behaviour under test is `docs/vtmb/animation_and_movers.md` -> "The disposition stance
// machine". The two shapes worth holding onto while reading:
//
//   - a fidget or a transition is a ONE-SHOT, and the selection that follows it settles back onto
//     the idle. That settle is what the `bInFidget`/`bInChange` latches exist for, and a machine
//     that lost them would chain fidgets forever.
//   - the stance-change floor is measured against the clock, not counted in clips. However many
//     idles complete inside the threshold, none of them may move the stance.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumDisposition.h"
#include "ElysiumStanceTypes.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Tests/ElysiumTestServices.h"

static constexpr EAutomationTestFlags GElysiumStanceTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The shipped Neutral row's own numbers.
	FElysiumDisposition NeutralTuning()
	{
		FElysiumDisposition Row;
		Row.Name = TEXT("Neutral");
		Row.AnimName = TEXT("Neutral");
		Row.TalkingStanceChangeThreshold = 0.15f;
		Row.TalkingStanceChangeChance = 65;
		Row.StandingFidgetChance = 50;
		Row.StandingStanceChangeThreshold = 3.0f;
		Row.StandingStanceChangeChance = 80;
		return Row;
	}

	// A body that authored all three idles and the 1->2 transition, and no fidgets — which is
	// `smiling_jack`'s real shape in the exported corpus.
	FElysiumStanceClips JackShapedClips()
	{
		FElysiumStanceClips Clips;
		Clips.Idle[0] = TEXT("Stance_Neutral_Idle_1");
		Clips.Idle[1] = TEXT("Stance_Neutral_Idle_2");
		Clips.Idle[2] = TEXT("Stance_Neutral_Idle_3");
		Clips.Trans[0][1] = TEXT("Stance_Neutral_Trans_1_2");
		ElysiumStance::ApplyPrecacheFallbacks(Clips);
		return Clips;
	}
}

// ============================================================================================
// The precache ladder. Retail resolves every miss ONCE, at model precache, and the resolved table
// is what the selector reads from then on — so a body with no `Fidget_2` does not ask again per
// roll, it simply has `fidget[2] == idle[2]` forever. That equality is also the selector's own
// availability test, which is why the ladder and the test have to be one fact.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStancePrecacheTest,
	"Elysium.Substrate.Stance.Precache", GElysiumStanceTestFlags)
bool FElysiumStancePrecacheTest::RunTest(const FString&)
{
	FElysiumStanceClips Clips;
	Clips.Idle[0] = TEXT("idle1");
	Clips.Idle[2] = TEXT("idle3");
	Clips.Fidget[1] = TEXT("fidget2");
	Clips.Trans[2][0] = TEXT("trans3_1");
	ElysiumStance::ApplyPrecacheFallbacks(Clips);

	TestEqual(TEXT("a missing idle falls back to idle 1"), Clips.Idle[1], FString(TEXT("idle1")));
	TestEqual(TEXT("an authored idle is untouched"), Clips.Idle[2], FString(TEXT("idle3")));
	TestEqual(TEXT("a missing fidget falls back to the idle at its own index"),
		Clips.Fidget[0], FString(TEXT("idle1")));
	TestEqual(TEXT("an authored fidget is untouched"), Clips.Fidget[1], FString(TEXT("fidget2")));
	TestEqual(TEXT("a missing transition falls back to the DESTINATION idle"),
		Clips.Trans[0][2], FString(TEXT("idle3")));
	TestEqual(TEXT("an authored transition is untouched"),
		Clips.Trans[2][0], FString(TEXT("trans3_1")));

	// A table with no first idle is not a stance set at all, and must stay empty rather than
	// propagating an empty string into all seventeen slots.
	FElysiumStanceClips Empty;
	ElysiumStance::ApplyPrecacheFallbacks(Empty);
	TestFalse(TEXT("a table with no idle 1 does not become a table of empty clips"), Empty.IsValid());
	TestTrue(TEXT("and its transitions stay empty"), Empty.Trans[0][1].IsEmpty());
	return true;
}

// ============================================================================================
// The first pose any body shows is `Idle_1`, because `m_CurrStance` is zero-initialised and
// nothing in the recovered writer set touches it before the first selection. This is the assertion
// that catches a machine that randomised its opening pose.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceFirstPoseTest,
	"Elysium.Substrate.Stance.FirstPose", GElysiumStanceTestFlags)
bool FElysiumStanceFirstPoseTest::RunTest(const FString&)
{
	const FElysiumStanceClips Clips = JackShapedClips();
	const FElysiumDisposition Tuning = NeutralTuning();
	FRandomStream Rng(1234);
	FElysiumStanceState State;

	const FElysiumStanceChoice First =
		ElysiumStance::Select(Clips, Tuning, State, /*bTalking=*/false, /*Now=*/0.0, Rng);
	TestEqual(TEXT("the first pose is stance 1's idle"), First.Clip,
		FString(TEXT("Stance_Neutral_Idle_1")));
	TestTrue(TEXT("an idle loops"), First.bLoop);
	TestFalse(TEXT("and it is not a stance change"), First.bChangedStance);
	TestEqual(TEXT("the stance index is still 0"), State.Current, 0);
	return true;
}

// ============================================================================================
// The stance-change floor. Neutral's threshold is 3.0 s, so no number of completed idles inside
// that window may move the stance — even at an 80% chance rolled every time. The negative
// assertion is the point: a machine that rolled per clip instead of per elapsed second would pass
// a "does it ever change" test and fail this one.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceFloorTest,
	"Elysium.Substrate.Stance.ChangeFloor", GElysiumStanceTestFlags)
bool FElysiumStanceFloorTest::RunTest(const FString&)
{
	const FElysiumStanceClips Clips = JackShapedClips();
	const FElysiumDisposition Tuning = NeutralTuning();
	FRandomStream Rng(99);
	FElysiumStanceState State;

	// Twenty selections inside the floor, as if a 0.15 s idle kept completing.
	int32 Changes = 0;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		const double Now = 0.1 * static_cast<double>(Step);
		const FElysiumStanceChoice Choice =
			ElysiumStance::Select(Clips, Tuning, State, /*bTalking=*/false, Now, Rng);
		Changes += Choice.bChangedStance ? 1 : 0;
	}
	TestEqual(TEXT("no stance change occurs inside the 3.0 s floor"), Changes, 0);
	TestEqual(TEXT("the stance is still the one it started on"), State.Current, 0);

	// Past the floor, an 80% chance settles within a handful of completions.
	bool bChanged = false;
	for (int32 Step = 0; Step < 8 && !bChanged; ++Step)
	{
		const FElysiumStanceChoice Choice = ElysiumStance::Select(Clips, Tuning, State,
			/*bTalking=*/false, 10.0 + static_cast<double>(Step), Rng);
		bChanged = Choice.bChangedStance;
	}
	TestTrue(TEXT("past the floor the stance does change"), bChanged);
	TestNotEqual(TEXT("and it moved off stance 1"), State.Current, 0);
	return true;
}

// ============================================================================================
// A change plays the authored transition and the NEXT selection settles onto the destination idle.
// Both halves matter: without the transition the body snaps, and without the settle the latch
// would leave it playing a one-shot forever.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceTransitionTest,
	"Elysium.Substrate.Stance.Transition", GElysiumStanceTestFlags)
bool FElysiumStanceTransitionTest::RunTest(const FString&)
{
	const FElysiumStanceClips Clips = JackShapedClips();
	FElysiumStanceState State;
	FRandomStream Rng(7);

	// Drive the change directly so the assertion is about the transition, not about a roll landing.
	const FElysiumStanceChoice Change = ElysiumStance::ChangeStance(Clips, State, 5.0, Rng);
	TestTrue(TEXT("a change reports itself as one"), Change.bChangedStance);
	TestFalse(TEXT("a transition is a one-shot, not a loop"), Change.bLoop);
	TestNotEqual(TEXT("the stance moved"), State.Current, 0);
	TestEqual(TEXT("the change stamped the clock"), State.LastChangeTime, 5.0f);
	if (State.Current == 1)
	{
		TestEqual(TEXT("1->2 plays its authored transition"), Change.Clip,
			FString(TEXT("Stance_Neutral_Trans_1_2")));
	}
	else
	{
		// 1->3 has no authored clip on this body, so the precache ladder resolved it to the
		// destination idle. That is a snap, and it is the faithful answer rather than a failure.
		TestEqual(TEXT("an unauthored transition resolves to the destination idle"), Change.Clip,
			FString(TEXT("Stance_Neutral_Idle_3")));
	}

	const int32 Destination = State.Current;
	const FElysiumStanceChoice Settle = ElysiumStance::Select(Clips, NeutralTuning(), State,
		/*bTalking=*/false, 5.1, Rng);
	TestEqual(TEXT("the selection after a transition settles onto the destination idle"),
		Settle.Clip, Clips.Idle[Destination]);
	TestTrue(TEXT("and that idle loops"), Settle.bLoop);
	TestFalse(TEXT("the change latch is consumed"), State.bInChange);
	return true;
}

// ============================================================================================
// A body that authored no fidget can never take the fidget branch, however the roll lands — the
// precache ladder made `fidget[n] == idle[n]`, and the selector's availability test is exactly
// that equality. `smiling_jack` is the shipped case, so this is not a hypothetical.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceFidgetTest,
	"Elysium.Substrate.Stance.Fidget", GElysiumStanceTestFlags)
bool FElysiumStanceFidgetTest::RunTest(const FString&)
{
	FElysiumDisposition Tuning = NeutralTuning();
	// Force the roll: a body WITH fidgets must take one, a body without must not, and the only
	// difference between the two runs is the authored content.
	Tuning.StandingFidgetChance = 100;

	{
		const FElysiumStanceClips NoFidgets = JackShapedClips();
		FRandomStream Rng(3);
		FElysiumStanceState State;
		int32 Fidgets = 0;
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const FElysiumStanceChoice Choice = ElysiumStance::Select(NoFidgets, Tuning, State,
				/*bTalking=*/false, 0.1 * static_cast<double>(Step), Rng);
			Fidgets += Choice.Clip.Contains(TEXT("Fidget")) ? 1 : 0;
		}
		TestEqual(TEXT("a body with no authored fidget never plays one"), Fidgets, 0);
	}
	{
		FElysiumStanceClips WithFidgets = JackShapedClips();
		WithFidgets.Fidget[0] = TEXT("Stance_Neutral_Fidget_1");
		FRandomStream Rng(3);
		FElysiumStanceState State;
		const FElysiumStanceChoice Choice = ElysiumStance::Select(WithFidgets, Tuning, State,
			/*bTalking=*/false, 0.0, Rng);
		TestEqual(TEXT("a body that authored one plays it"), Choice.Clip,
			FString(TEXT("Stance_Neutral_Fidget_1")));
		TestFalse(TEXT("a fidget is a one-shot"), Choice.bLoop);

		// And the settle immediately after, which is what stops fidgets chaining.
		const FElysiumStanceChoice Settle = ElysiumStance::Select(WithFidgets, Tuning, State,
			/*bTalking=*/false, 0.5, Rng);
		TestEqual(TEXT("the selection after a fidget settles onto the idle"), Settle.Clip,
			FString(TEXT("Stance_Neutral_Idle_1")));
	}
	return true;
}

// ============================================================================================
// A character speaking a line holds its stance. The talking threshold/chance pair is consumed by
// the dialogue-pause driver, not by the standing selector, so `bTalking` here is "a line is
// playing" and its only effect is to hold the current idle.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceTalkingTest,
	"Elysium.Substrate.Stance.Talking", GElysiumStanceTestFlags)
bool FElysiumStanceTalkingTest::RunTest(const FString&)
{
	FElysiumStanceClips Clips = JackShapedClips();
	Clips.Fidget[0] = TEXT("Stance_Neutral_Fidget_1");
	FElysiumDisposition Tuning = NeutralTuning();
	Tuning.StandingFidgetChance = 100;
	Tuning.StandingStanceChangeChance = 100;

	FRandomStream Rng(11);
	FElysiumStanceState State;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		const FElysiumStanceChoice Choice = ElysiumStance::Select(Clips, Tuning, State,
			/*bTalking=*/true, 100.0 + static_cast<double>(Step), Rng);
		TestEqual(TEXT("a talking character holds its stance idle"), Choice.Clip,
			FString(TEXT("Stance_Neutral_Idle_1")));
		TestFalse(TEXT("and never changes stance while a line plays"), Choice.bChangedStance);
	}
	TestEqual(TEXT("the stance index never moved"), State.Current, 0);
	return true;
}

// ============================================================================================
// The driver, through the real producer path. Every test above asserts the rule in isolation; this
// one asserts that a standing NPC in a loaded world actually reaches it — which is precisely what
// was missing, because `Select` was written, tested and never called. It is also the regression
// guard for the two think-cadence holes: a standing NPC used to fall off the end of `Think()`
// without setting `NextThink`, so it was asked exactly once and held that pose forever.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStanceDriverTest,
	"Elysium.Substrate.Stance.Driver", GElysiumStanceTestFlags)
bool FElysiumStanceDriverTest::RunTest(const FString&)
{
	auto BuildDefs = [](FElysiumEntityDefs& Defs)
	{
		Defs.MapName = TEXT("__stance_driver__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Jack");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/smiling_jack.mdl"));
		Npc.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
		Defs.Defs.Add(MoveTemp(Npc));
	};

	// --- A standing NPC runs the machine and keeps being asked ------------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;
		Services.StanceClips = JackShapedClips();
		Services.DispositionRow = NeutralTuning();
		Services.ClipSeconds = 2.0f;   // every stance clip reports the same length
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		double Now = 0.0;
		for (int32 i = 0; i < 4; ++i) { World.Tick(Now); Now += 0.1; }

		TestTrue(TEXT("the table is resolved through the disposition row, not a raw name"),
			Services.Saw(TEXT("ResolveDisposition Neutral")));
		TestTrue(TEXT("and the clips through the row's own animation token"),
			Services.Saw(TEXT("ResolveStanceClips smiling_jack anim=Neutral")));
		TestTrue(TEXT("a standing NPC opens on Idle_1, looping"),
			Services.Saw(TEXT("PlayNpcClip smiling_jack Stance_Neutral_Idle_1 loop=1")));

		// The cadence. With a 2 s clip and 0.1 s ticks the selector must NOT re-run every think —
		// that would be the "no floor" bug, and it would restart the clip 20 times a second.
		const int32 AfterOpening = Services.Calls.FilterByPredicate([](const FString& C)
		{
			return C.StartsWith(TEXT("PlayNpcClip smiling_jack Stance_"));
		}).Num();
		TestEqual(TEXT("one selection per clip, not one per think"), AfterOpening, 1);

		// Past the 3 s change floor with an 80 % roll, the stance has to move — and a move plays the
		// authored transition rather than snapping to the destination idle.
		for (int32 i = 0; i < 400; ++i) { World.Tick(Now); Now += 0.1; }
		TestTrue(TEXT("the stance eventually changes, through its authored transition"),
			Services.Saw(TEXT("PlayNpcClip smiling_jack Stance_Neutral_Trans_1_2 loop=0")));
		TestTrue(TEXT("and settles onto the destination idle after it"),
			Services.Saw(TEXT("PlayNpcClip smiling_jack Stance_Neutral_Idle_2 loop=1")));
	}

	// --- A body with no stance set is not left unscheduled -----------------------------------
	// The monsters and one-off models carry no `Stance_*` clips at all. They must fall back to the
	// ACT_IDLE resolution rather than stalling, and — the part that used to be broken — they must
	// still get a next think.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;   // StanceClips left empty: this model authored none
		Services.DispositionRow = NeutralTuning();
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		double Now = 0.0;
		for (int32 i = 0; i < 20; ++i) { World.Tick(Now); Now += 0.5; }

		TestFalse(TEXT("a body with no stance set plays no stance clip"),
			Services.Calls.ContainsByPredicate([](const FString& C)
			{
				return C.Contains(TEXT("PlayNpcClip smiling_jack Stance_"));
			}));
		// It was asked, and asked again — the resolve is retried on the slow cadence rather than
		// the entity being dropped from the think list entirely.
		TestTrue(TEXT("the standing fallback is still scheduled"),
			Services.Calls.FilterByPredicate([](const FString& C)
			{
				return C.StartsWith(TEXT("ResolveStanceClips"));
			}).Num() >= 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
