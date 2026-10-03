// The post-feed trance, end to end on a headless world: the surviving victim's install, the
// program's first think, what it makes the NPC refuse, how long it holds, and how it unwinds.
//
// `docs/vtmb/feeding.md` -> "Step 4, decoded" and `docs/vtmb/npc-ai/schedule-kernel.md` ->
// "The incapacitation tasks and the NPC flag word" own every fact asserted here. The kernel-level
// shape of the program (task order, the 30 + 0..120 bound, DELAY_INTERRUPTS) is asserted in
// `ElysiumScheduleTests.cpp`; this file asserts the SYSTEM around it -- the producer's guard, the
// consumers of the bits it sets, the exit through the disposition idle, and the executor
// pre-emption that lets a walking NPC be tranced at all.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumFeedTranceTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;

namespace
{
	double Cm(double SourceUnits) { return SourceUnits * ElysiumMove::U; }

	// A model key, so the leaf builds a body and the recording services build a motor. Its activity
	// resolver deliberately keeps its default miss: `TASK_SET_ACTIVITY` must still advance at step 5.
	const TCHAR* const GModel =
		TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl");

	// One NPC at the origin with the two incapacitation outputs counted, the player, and two
	// patrol points for the executor case.
	struct FTranceFixture
	{
		struct FSetup
		{
			bool bPatrol = false;
			// The victim already hates the feeder -- retail's one refusal of the trance.
			bool bHostile = false;
			// A `worldspawn` with `safearea 1` and its `events_world` leaf, so `CWorld::m_nAreaType`
			// is nonzero -- the map-wide AI gate `FeedBegin` reads.
			bool bSafeArea = false;
		};

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;
		// The world time the fixture's own thinks reached; a case continues from here.
		double SettledAt = 0.0;

		explicit FTranceFixture(const FSetup& Setup)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x54524e43);
			Services.bProvideNpcMotor = true;

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__feed_trance_test__");
			if (Setup.bSafeArea)
			{
				FElysiumEntityDef WorldSpawn;
				WorldSpawn.Classname = TEXT("worldspawn");
				WorldSpawn.Keys.Add(TEXT("safearea"), TEXT("1"));
				Defs.Defs.Add(MoveTemp(WorldSpawn));
				FElysiumEntityDef WorldEvents;
				WorldEvents.Classname = TEXT("events_world");
				WorldEvents.TargetName = TEXT("world");
				Defs.Defs.Add(MoveTemp(WorldEvents));
			}

			FElysiumEntityDef GuardDef;
			GuardDef.Classname = TEXT("npc_VHumanCombatant");
			GuardDef.TargetName = TEXT("guard");
			GuardDef.Origin = FVector::ZeroVector;
			GuardDef.Keys.Add(TEXT("model"), GModel);
			GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			auto Wire = [&GuardDef](const TCHAR* Output, const TCHAR* Target)
			{
				FElysiumOutputDef Row;
				Row.Name = Output;
				Row.Target = Target;
				Row.Input = TEXT("Add");
				Row.Param = TEXT("1");
				GuardDef.Outputs.Add(MoveTemp(Row));
			};
			Wire(TEXT("OnIncapacitatedStart"), TEXT("incap_start"));
			Wire(TEXT("OnIncapacitatedEnd"), TEXT("incap_end"));
			Defs.Defs.Add(MoveTemp(GuardDef));

			for (const TCHAR* Name : { TEXT("incap_start"), TEXT("incap_end") })
			{
				FElysiumEntityDef Counter;
				Counter.Classname = TEXT("math_counter");
				Counter.TargetName = Name;
				Defs.Defs.Add(MoveTemp(Counter));
			}

			for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
			{
				FElysiumEntityDef Point;
				Point.Classname = TEXT("info_node_patrol_point");
				Point.TargetName = FString::Printf(TEXT("route_%d"), PointIndex);
				// A patrol point is found by its exact-case `Group` on a type-10000 hint (`0x102d2840`).
				Point.Keys.Add(TEXT("hinttype"), TEXT("10000"));
				Point.Keys.Add(TEXT("Group"), Point.TargetName);
				Point.Origin = FVector(0.0, static_cast<double>(PointIndex) * -200.0, 0.0);
				Defs.Defs.Add(MoveTemp(Point));
			}

			ElysiumAdoptPlacesAt(World, { FVector(0.0, -200.0, 0.0), FVector(0.0, -400.0, 0.0) });   // route_1, route_2
			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(FElysiumNpcWorldFixture::FirstThinkSeconds);   // the admission barrier

			FElysiumEntity* GuardEnt = World.FindByName(TEXT("guard"));
			Guard = GuardEnt ? GuardEnt->AsNpc() : nullptr;
			Player = World.FindPlayer();
			if (Guard == nullptr || Player == nullptr)
			{
				return;
			}
			// Out of every sense cone: nothing here is about acquisition. `AttemptFeed` takes the
			// victim directly, exactly as the feeding tests drive it.
			Player->Origin = FVector(0.0, Cm(9000.0), 0.0);

			using EC = EElysiumTraitContainer;
			Guard->Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
			Guard->Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
			// Enough blood that a short feed leaves a SURVIVOR: the trance is step 4's branch, the
			// death outcome is step 3's, and the feeding tests already own the latter.
			Guard->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 10);
			Guard->RecomputeSheet();
			Guard->MaxHealth = 100;
			Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 0);
			Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
			Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
			Player->RecomputeSheet();

			// Two ordinary thinks settle admission and the loadout.
			for (int32 i = 0; i < 2; ++i)
			{
				Step(0.0);
			}
			if (Setup.bHostile)
			{
				Guard->Relationships.SetEntity(Player->Handle, EElysiumRelationship::Hate, 5);
			}
			if (Setup.bPatrol)
			{
				// Story 8 wave 2: the patrol is the path object's own program (`SetupPatrolType`
				// `0x1029eb30` then `FollowPatrolPath` `0x1029ed90`, each install at `0x1029f56b`).
				World.AcceptInput(TEXT("!self"), FName(TEXT("SetupPatrolType")),
					FElysiumVariant::String(TEXT("255 0 FOLLOW_PATROL_PATH_WALK")), Guard->Handle, Guard->Handle);
				World.AcceptInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
					FElysiumVariant::String(TEXT("route_1 route_2")), Guard->Handle, Guard->Handle);
				for (SettledAt = 0.0; SettledAt <= 5.0 && !Services.Saw(TEXT("NpcMotor MoveTo"));
					SettledAt += 0.5)
				{
					Step(SettledAt);
				}
			}
			Quiet();
		}

		FTranceFixture(const FTranceFixture&) = delete;
		FTranceFixture& operator=(const FTranceFixture&) = delete;

		void Quiet()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard });
		}

		// One forced think of the guard at `Now`.
		void Step(double Now)
		{
			FElysiumNpcWorldFixture::Wake({ Guard }, Now);
			World.Tick(Now);
			Quiet();
		}

		// The ordinary retail feed, cut short while the victim still has blood. `FeedInterrupt` is the
		// authoritative teardown either way, and it is the caller of the branch under test.
		void FeedAndInterrupt(double& Now)
		{
			// The four automatic-acceptance states accept without a roll; the trance's own refusal
			// is decided at teardown, not at acceptance.
			Guard->Disposition = TEXT("cower");
			Player->AttemptFeed(*Guard);
			for (int32 i = 0; i < 10; ++i)
			{
				Now += 0.1;
				World.RunPlayerThink(Now);
				World.Tick(Now);
			}
			Player->FeedInterrupt();
		}

		// Run the guard once a second until the trance program is no longer the one running. Returns
		// the seconds it held, or -1 when it never ended inside the budget.
		double RunOutTrance(double& Now)
		{
			const double Started = Now;
			for (int32 Second = 0; Second < 200; ++Second)
			{
				Now += 1.0;
				Step(Now);
				if (Guard->Schedule.Current != ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MESMERIZED))
				{
					return Now - Started;
				}
			}
			return -1.0;
		}

		float Counter(const TCHAR* Name)
		{
			return SaveTestCounterValue(World.FindByName(Name));
		}

		FString Debug(const TCHAR* Key) const
		{
			if (Guard == nullptr)
			{
				return FString();
			}
			TArray<TPair<FString, FString>> Rows;
			Guard->GetDebugState(Rows);
			for (const TPair<FString, FString>& Row : Rows)
			{
				if (Row.Key == Key)
				{
					return Row.Value;
				}
			}
			return FString();
		}

		FString Snapshot() const
		{
			if (Guard == nullptr)
			{
				return FString();
			}
			TArray<TPair<FString, FString>> Rows;
			Guard->GetDebugState(Rows);
			TArray<FString> Parts;
			for (const TPair<FString, FString>& Row : Rows)
			{
				Parts.Add(FString::Printf(TEXT("%s=[%s]"), *Row.Key, *Row.Value));
			}
			const TArray<FString>& Trace = Guard->GetMind().Trace();
			for (int32 i = FMath::Max(0, Trace.Num() - 20); i < Trace.Num(); ++i)
			{
				Parts.Add(FString::Printf(TEXT("TRACE %d: %s"), i, *Trace[i]));
			}
			return FString::Join(Parts, TEXT("\n    "));
		}
	};
}

// ============================================================================================
// A standing bystander: fed on, released with blood left, stands entranced, and comes back whole.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTranceStandingTest,
	"Elysium.Arm.FeedTrance.Standing", GElysiumTestFlags)
bool FElysiumFeedTranceStandingTest::RunTest(const FString&)
{
	FTranceFixture F(FTranceFixture::FSetup{});
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	double Now = 0.0;
	F.FeedAndInterrupt(Now);

	// The install is synchronous with the teardown -- `FeedInterrupt` -> `SetSchedule(0xfb)` --
	// and the program has not begun: retail's victim is still grappled when its schedule is set.
	TestFalse(TEXT("the pair is torn down"), F.Player->IsFeedPaired());
	TestTrue(TEXT("the victim survived with blood"), F.Guard->BloodPoolValue() >= 1);
	TestEqual(TEXT("SCHED_TROIKA_MESMERIZED is installed"),
		F.Guard->Schedule.Current,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MESMERIZED));
	TestFalse(TEXT("...and has not run a task yet"), F.Guard->IsOblivious());

	// The first think runs the four flag writes and starts `TASK_SET_ACTIVITY`. Its resolver keeps the
	// default negative answer, so retail's one-second RunTask watchdog owns the advance from here.
	Now += 0.1;
	F.Step(Now);
	if (!F.Guard->IsOblivious())
	{
		AddInfo(FString::Printf(TEXT("guard state after the first think:\n    %s"), *F.Snapshot()));
	}
	TestTrue(TEXT("TASK_MAKE_OBLIVIOUS: the body senses nothing"), F.Guard->IsOblivious());
	TestTrue(TEXT("D_IS_BUSY: IsBusyWithDiscipline answers true"),
		F.Guard->IsBusyWithDiscipline());
	TestTrue(TEXT("NO_DIALOG: the dialogue gate refuses"), F.Guard->HasDialogSuppressFlag());
	TestTrue(TEXT("DONT_INVESTIGATE is set"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::DONT_INVESTIGATE));
	TestFalse(TEXT("the unresolved activity does not fake a body presentation"),
		F.Services.Saw(TEXT("PlayNpcClip")));
	TestEqual(TEXT("OnIncapacitatedStart fired once"), F.Counter(TEXT("incap_start")), 1.0f);
	// Corrected to retail (story 8 wave 2): `TASK_SET_ACTIVITY`'s start arm (`0x102a1c0f`) is
	// `SetIdealActivity(act)`; a body with no sequence for ACT_DISPOSITION_MESMERIZED resolves its
	// ideal through the ladder's ACT_DISPOSITION retry to slot 611's `m_nSequence` fallback, so the
	// run arm's `m_nSequence == m_nIdealSequence` (`0x102aad2d`) completes it in the same maintain
	// pass — not a failure, and not the watchdog. (The port's retired arm wrote an "unresolved" trace
	// row and waited out the second.)
	TestTrue(TEXT("the unresolved activity completes into the authored wait, not a fail schedule"),
		F.Guard->Schedule.IsRunning() && F.Guard->Schedule.TaskIndex == 5);

	Now += 1.0;
	F.Step(Now);
	TestTrue(TEXT("the one-second watchdog advances to the authored wait"),
		F.Guard->Schedule.IsRunning() && F.Guard->Schedule.TaskIndex >= 5);
	// The trance is one of the four automatic feed-acceptance states.
	TestTrue(TEXT("a tranced victim accepts a second feed with no roll"),
		F.Guard->IsFeedAutoAcceptState());

	// It holds 30 + 0..120 seconds, then ends -- and the NEXT install is what releases the bits:
	// `SelectSchedule` case 1 sends a still-busy NPC through the disposition idle, whose
	// schedule-change virtual clears them.
	const double Held = F.RunOutTrance(Now);
	TestTrue(TEXT("the trance ended inside the recovered bound"), Held >= 30.0 && Held <= 151.0);
	TestFalse(TEXT("D_IS_BUSY released by the next install"), F.Guard->IsBusyWithDiscipline());
	TestFalse(TEXT("NO_DIALOG released"), F.Guard->HasDialogSuppressFlag());
	TestFalse(TEXT("obliviousness released with them"), F.Guard->IsOblivious());
	// Retail fires `OnIncapacitatedEnd` only from the task's dead FALSE arm and the cine/grapple
	// exits -- never from the schedule-change release. A map wired to it must not see one here.
	TestEqual(TEXT("OnIncapacitatedEnd does NOT fire on the schedule-change release"),
		F.Counter(TEXT("incap_end")), 0.0f);
	return true;
}

// ============================================================================================
// `TASK_SET_ACTIVITY`'s RunTask completion is the current base-channel sequence identity, not the
// ACT_* request. The resolved `ACT_DISPOSITION_MESMERIZED` below becomes the bank clip
// `(move_and_ranged, walk)`; the same authoritative phase with different case reaches ideal before
// the watchdog. This catches an implementation that compared the requested activity or model stem.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTranceActivityIdentityTest,
	"Elysium.Arm.FeedTrance.ActivityIdentity", GElysiumTestFlags)
bool FElysiumFeedTranceActivityIdentityTest::RunTest(const FString&)
{
	FTranceFixture F(FTranceFixture::FSetup{});
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	F.Services.bNpcActivitiesResolve = true;
	// `MaintainSchedule` calls RunTask after StartTask in the same loop. Put the body on the resolved
	// sequence before that think, so the identity probe must advance without spending the watchdog.
	F.Services.bBodyClipPhaseSet = true;
	F.Services.BodyClipPhase.OwnerStem = TEXT("MOVE_AND_RANGED");
	F.Services.BodyClipPhase.Label = TEXT("WALK");

	double Now = 0.0;
	F.FeedAndInterrupt(Now);
	Now += 0.1;
	F.Step(Now);
	TestTrue(TEXT("the current resolved identity completes in StartTask's same maintain pass"),
		F.Guard->Schedule.IsRunning() && F.Guard->Schedule.TaskIndex >= 5);
	TestTrue(TEXT("the body was asked to play the resolved clip"), F.Services.Saw(TEXT("PlayNpcClip")));
	TestFalse(TEXT("the resolving body wrote no miss trace"),
		F.Guard->GetMind().Trace().ContainsByPredicate([](const FString& Row)
		{
			return Row.Contains(TEXT("TASK_SET_ACTIVITY ACT_DISPOSITION_MESMERIZED unresolved"));
		}));
	return true;
}

// ============================================================================================
// A victim that already hates the feeder: `IRelationType(attacker) == D_HT` refuses the trance.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTranceHostileTest,
	"Elysium.Arm.FeedTrance.Hostile", GElysiumTestFlags)
bool FElysiumFeedTranceHostileTest::RunTest(const FString&)
{
	FTranceFixture::FSetup Setup;
	Setup.bHostile = true;
	FTranceFixture F(Setup);
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	double Now = 0.0;
	F.FeedAndInterrupt(Now);
	TestFalse(TEXT("the pair is torn down"), F.Player->IsFeedPaired());
	TestTrue(TEXT("the victim survived with blood"), F.Guard->BloodPoolValue() >= 1);
	TestNotEqual(TEXT("no trance for a D_HT victim"),
		F.Guard->Schedule.Current,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MESMERIZED));
	Now += 0.1;
	F.Step(Now);
	TestFalse(TEXT("...and it is not oblivious"), F.Guard->IsOblivious());
	TestFalse(TEXT("...nor busy"), F.Guard->IsBusyWithDiscipline());
	TestEqual(TEXT("no incapacitation output"), F.Counter(TEXT("incap_start")), 0.0f);
	return true;
}

// ============================================================================================
// A walking patroller. Retail's patrol is a schedule, so a forced install replaces it and the
// schedule-change virtual stops the navigator; this runtime's patrol is an executor, and the
// running program has to pre-empt it the way combat does -- otherwise the guard walks off
// mid-trance with its program parked at task 0.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTrancePatrolTest,
	"Elysium.Arm.FeedTrance.Patrol", GElysiumTestFlags)
bool FElysiumFeedTrancePatrolTest::RunTest(const FString&)
{
	FTranceFixture::FSetup Setup;
	Setup.bPatrol = true;
	FTranceFixture F(Setup);
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	TestTrue(TEXT("the guard is on its route"), F.Services.Saw(TEXT("NpcMotor MoveTo")));
	const int32 MovesBefore = F.Services.Count(TEXT("NpcMotor MoveTo"));

	double Now = F.SettledAt;
	F.FeedAndInterrupt(Now);
	const double InstalledAt = Now;
	TestEqual(TEXT("the trance is installed on a patroller"),
		F.Guard->Schedule.Current,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MESMERIZED));

	// The program pre-empts the executor while the unresolved activity is still running.
	Now += 0.1;
	F.Step(Now);
	TestTrue(TEXT("the mesmerized program still owns the patroller"),
		F.Guard->Schedule.Current == ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MESMERIZED) && F.Guard->Schedule.IsRunning());
	TestTrue(TEXT("the program made the patroller oblivious"), F.Guard->IsOblivious());
	TestTrue(TEXT("...and marked it busy with the discipline"), F.Guard->IsBusyWithDiscipline());
	TestTrue(TEXT("...and suppresses dialogue"), F.Guard->HasDialogSuppressFlag());
	TestTrue(TEXT("...and blocks investigation"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::DONT_INVESTIGATE));
	// (`0x102aad2d`: the unresolved activity's fallback commit completes TASK_SET_ACTIVITY in its own
	// maintain pass, as `FeedTrance.Standing` asserts.)
	TestTrue(TEXT("the unresolved activity completed into the authored wait, without a fail schedule"),
		F.Guard->Schedule.TaskIndex == 5);
	TestEqual(TEXT("no patrol leg is issued while the activity waits"),
		F.Services.Count(TEXT("NpcMotor MoveTo")), MovesBefore);

	// `TASK_SET_ACTIVITY` advances only when its actual one-second watchdog expires.
	Now += 1.0;
	F.Step(Now);
	TestTrue(TEXT("the watchdog advances to the authored wait"),
		F.Guard->Schedule.IsRunning() && F.Guard->Schedule.TaskIndex >= 5);
	Now += 5.0;
	F.Step(Now);
	TestEqual(TEXT("no new patrol leg is issued while tranced"),
		F.Services.Count(TEXT("NpcMotor MoveTo")), MovesBefore);

	// Then the route comes back.
	// Measured from the install: the 6.1 s this case has already stepped through are part of the
	// trance. (The draw that sets its length shares the NPC
	// stream with the patrol's interest roll `0x1029f650`, which rolls now that the route's node
	// holds its hint -- so the length moves with the network, the bound does not.)
	const bool bEnded = F.RunOutTrance(Now) >= 0.0;
	const double Held = Now - InstalledAt;
	TestTrue(*FString::Printf(TEXT("the trance ended inside the recovered bound (held %.1f s)"), Held),
		bEnded && Held >= 30.0 && Held <= 151.0);
	TestFalse(TEXT("the bits are released"), F.Guard->IsBusyWithDiscipline());
	// The next install releases the bits through the disposition idle (`0x6b`, case 1 while still
	// busy); after it, `SelectSchedule` case 1 step 3 (`0x102af6b6`) answers the surviving path's
	// program, whose first leg is a new move.
	for (int32 i = 0; i < 20 && F.Services.Count(TEXT("NpcMotor MoveTo")) <= MovesBefore; ++i)
	{
		Now += 1.0;
		F.Step(Now);
	}
	TestTrue(TEXT("the patrol route resumes after the trance"),
		F.Services.Count(TEXT("NpcMotor MoveTo")) > MovesBefore);
	return true;
}

// The map-wide AI gate around a player's feed. `FeedBegin` `0x10339d90`: a player feeder, in a
// map whose `CWorld::m_nAreaType` is nonzero, whom no NPC has assessed for 3 s, calls
// `SetAIEnabled(false)` `0x10265680`; `FeedInterrupt` `0x1033a9e0` calls `SetAIEnabled(true)`
// on the way out, which re-bases every NPC's whole clock (slot 614 plus the `Last` stamps).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTranceAiGateTest,
	"Elysium.Arm.FeedTrance.AiGate", GElysiumTestFlags)
bool FElysiumFeedTranceAiGateTest::RunTest(const FString&)
{
	FTranceFixture::FSetup Setup;
	Setup.bSafeArea = true;
	FTranceFixture F(Setup);
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	TestTrue(TEXT("the map's area type is nonzero"),
		ElysiumLaw::WorldAreaType(F.World) != 0);
	TestTrue(TEXT("the AI starts enabled"), F.World.IsAiEnabled());
	TestTrue(TEXT("nobody has assessed the player: the stamps are the spawn's zeros"),
		F.Player->LatestSeenByNpcTime() == 0.0);

	// Past the map's first three seconds -- the zeroed stamps read as "assessed at t=0", so a feed
	// before t=3 never closes the gate (retail's own opening window).
	double Now = 0.0;
	for (int32 i = 0; i < 50; ++i)
	{
		Now += 0.1;
		F.World.RunPlayerThink(Now);
		F.World.Tick(Now);
	}
	// The ordinary feed, watched from the outside: the anim event's `FeedBegin` is what closes
	// the gate, not `AttemptFeed`.
	F.Guard->Disposition = TEXT("cower");
	F.Player->AttemptFeed(*F.Guard);
	for (int32 i = 0; i < 10; ++i)
	{
		Now += 0.1;
		F.World.RunPlayerThink(Now);
		F.World.Tick(Now);
	}
	TestTrue(TEXT("the feed began"), F.Player->FeedState.IsTransacting());
	TestFalse(TEXT("an unobserved feed in a typed area disables the map's AI"),
		F.World.IsAiEnabled());

	// Push the victim's stamps off `Now`, so the re-base is observable as a write and not as a
	// coincidence.
	F.Guard->ScheduleHost.NextNormal = Now + 50.0;
	F.Guard->ScheduleHost.LastNormal = 1.0;
	F.Player->FeedInterrupt();
	TestTrue(TEXT("the interrupt re-enables the AI"), F.World.IsAiEnabled());
	const double At = F.World.NowSeconds();
	TestTrue(TEXT("...and re-bases every NPC's Next stamps"),
		FMath::IsNearlyEqual(F.Guard->ScheduleHost.NextNormal, At, 1e-6));
	TestTrue(TEXT("...and its Last stamps too (slot 584's shape)"),
		FMath::IsNearlyEqual(F.Guard->ScheduleHost.LastNormal, At, 1e-6));
	return true;
}

// The gate's two refusals: an observed feeder, and a combat-typed area.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTranceAiGateRefusalsTest,
	"Elysium.Arm.FeedTrance.AiGateRefusals", GElysiumTestFlags)
bool FElysiumFeedTranceAiGateRefusalsTest::RunTest(const FString&)
{
	// An untyped map (no `worldspawn` policy at all reads as area 0, retail's combat type).
	{
		FTranceFixture::FSetup Plain;
		FTranceFixture F(Plain);
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
		{
			return false;
		}
		double Now = 0.0;
		F.FeedAndInterrupt(Now);
		TestTrue(TEXT("a feed in an area-0 map never touches the AI"), F.World.IsAiEnabled());
	}
	// A typed map, but an NPC assessed the player less than 3 s ago.
	{
		FTranceFixture::FSetup Setup;
		Setup.bSafeArea = true;
		FTranceFixture F(Setup);
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
		{
			return false;
		}
		double Now = 0.0;
		for (int32 i = 0; i < 50; ++i)
		{
			Now += 0.1;
			F.World.RunPlayerThink(Now);
			F.World.Tick(Now);
		}
		F.Player->LastSeenByNpcTime[4] = Now;   // D_NU, just now
		F.Guard->Disposition = TEXT("cower");
		F.Player->AttemptFeed(*F.Guard);
		for (int32 i = 0; i < 10; ++i)
		{
			Now += 0.1;
			F.World.RunPlayerThink(Now);
			F.World.Tick(Now);
		}
		TestTrue(TEXT("the feed began"), F.Player->FeedState.IsTransacting());
		TestTrue(TEXT("a feed seen inside the last 3 s leaves the AI running"),
			F.World.IsAiEnabled());
		F.Player->FeedInterrupt();
		TestTrue(TEXT("...and the interrupt has nothing to re-enable"), F.World.IsAiEnabled());
	}
	return true;
}

}   // namespace ElysiumFeedTranceTests

#endif // WITH_DEV_AUTOMATION_TESTS
