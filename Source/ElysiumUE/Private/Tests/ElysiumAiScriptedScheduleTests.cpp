// Content-free Substrate automation: `aiscripted_schedule` — the authored AI director.
//
// Every number and every ordering asserted here is a fact from
// `docs/vtmb/npc-ai/authored-control.md` -> "`aiscripted_schedule`" and "Direct schedule
// changes", or a key spelling read off the exported `.ents` of the five maps that carry the
// entity (`sm_apartment_1`, `sm_diner_1`, `sm_hub_1`, `sm_medical_1`, `sm_warehouse_1`). Nothing
// here reads the export: the fixture states the authored record in code, which is what makes these
// the runtime's statement of the contract rather than a reading of one map.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumAiScriptedScheduleTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ECond = EElysiumNpcCond;
using EId = int32;

namespace
{
	double Cm(double SourceUnits) { return SourceUnits * ElysiumMove::U; }

	// The one model key every fixture NPC needs: without it the leaf builds no body, and without a
	// body the recording services build no motor.
	const TCHAR* const GModel =
		TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl");

	// Where `goal_a` stands. Far enough that no acquisition or attack condition is in play.
	const FVector GGoalOrigin(500.0, 0.0, 0.0);

	// The world.
	// One directed NPC at the origin, one other NPC it can be pointed at, one `point_target` goal
	// (which is what four of the six named corpus goals are), the `aiscripted_schedule` itself, and
	// two patrol points for the executor hand-over cases.
	struct FAiScheduleFixture
	{
		struct FSetup
		{
			int32 Mode = 2;
			int32 ForceState = 0;
			const TCHAR* GoalName = TEXT("goal_a");
			int32 SpawnFlags = 0;
			bool bPatrol = false;
			// Leave the mind un-admitted, so a case can fire the director at an NPC that has never
			// thought — which is what `npc_maker.OnSpawnNPC` does in `sm_medical_1`.
			bool bSkipAdmission = false;
		};

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Victim = nullptr;
		FElysiumEntity* Director = nullptr;
		FElysiumEntity* Goal = nullptr;
		FElysiumPlayer* Player = nullptr;
		FElysiumEntityHandle DirectorHandle;

		// The row set every case shares: the directed guard, the victim it can be pointed at, the
		// `point_target` goal, the `aiscripted_schedule` director itself, and two patrol points.
		// `World.Load`/`SpawnPlayer`/`Activate` still happen in the constructor body below rather
		// than through `FElysiumNpcWorldFixture`, because the first `Tick` — the admission barrier —
		// is conditional here (`bSkipAdmission`), unlike every other fixture's fixed sequence.
		static FElysiumNpcWorldBuilder BuildWorld(const FSetup& Setup)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__aischedule_test__"), 0x41534348);

			FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
			GuardDef.Keys.Add(TEXT("model"), GModel);
			GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));

			FElysiumEntityDef& VictimDef = Builder.AddNpc(TEXT("victim"), FVector(100.0, 0.0, 0.0));
			VictimDef.Keys.Add(TEXT("model"), GModel);

			// `point_target` is the corpus's own goal classname on four of the six named rows. It has
			// no registered leaf, which is exactly the point: a goal is a POSITION, not a behaviour.
			Builder.AddEntity(TEXT("point_target"), TEXT("goal_a"), GGoalOrigin);

			FElysiumEntityDef& DirectorDef = Builder.AddEntity(TEXT("aiscripted_schedule"),
				TEXT("director"), FVector(0.0, 400.0, 0.0));
			DirectorDef.Keys.Add(TEXT("m_iszEntity"), TEXT("guard"));
			DirectorDef.Keys.Add(TEXT("goalent"), Setup.GoalName);
			DirectorDef.Keys.Add(TEXT("schedule"), FString::FromInt(Setup.Mode));
			DirectorDef.Keys.Add(TEXT("forcestate"), FString::FromInt(Setup.ForceState));
			DirectorDef.Keys.Add(TEXT("m_flRadius"), TEXT("1024"));
			if (Setup.SpawnFlags != 0)
			{
				DirectorDef.Keys.Add(TEXT("spawnflags"), FString::FromInt(Setup.SpawnFlags));
			}

			for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
			{
				FElysiumEntityDef& Point = Builder.AddEntity(TEXT("info_node_patrol_point"),
					*FString::Printf(TEXT("route_%d"), PointIndex),
					FVector(0.0, static_cast<double>(PointIndex) * -200.0, 0.0));
				// A patrol point is found by its exact-case `Group` on a type-10000 hint (`0x102d2840`).
				Point.Keys.Add(TEXT("hinttype"), TEXT("10000"));
				Point.Keys.Add(TEXT("Group"), Point.TargetName);
			}
			return Builder;
		}

		explicit FAiScheduleFixture(const FSetup& Setup)
			: World(nullptr, nullptr, Services.Bundle())
		{
			Services.bProvideNpcMotor = true;

			FElysiumNpcWorldBuilder Builder = BuildWorld(Setup);
			ElysiumAdoptPlacesAt(World, { FVector(0.0, -200.0, 0.0), FVector(0.0, -400.0, 0.0) });   // route_1, route_2
			World.Load(MoveTemp(Builder.Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			if (!Setup.bSkipAdmission)
			{
				// `Activate` arms admission and schedules the first think at t=0, so THIS tick is
				// already the admission barrier. A case that wants an un-admitted mind must skip it.
				World.Tick(FElysiumNpcWorldFixture::FirstThinkSeconds);
			}

			FElysiumEntity* GuardEnt = World.FindByName(TEXT("guard"));
			FElysiumEntity* VictimEnt = World.FindByName(TEXT("victim"));
			Guard = GuardEnt ? GuardEnt->AsNpc() : nullptr;
			Victim = VictimEnt ? VictimEnt->AsNpc() : nullptr;
			Director = World.FindByName(TEXT("director"));
			Goal = World.FindByName(TEXT("goal_a"));
			Player = World.FindPlayer();
			if (Director != nullptr)
			{
				DirectorHandle = Director->Handle;
			}
			if (Player != nullptr)
			{
				// Out of every reach and cone unless a case deliberately puts it back in play.
				Player->Origin = FVector(0.0, Cm(9000.0), 0.0);
			}
			for (FElysiumNpc* Npc : { Guard, Victim })
			{
				if (Npc != nullptr)
				{
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
					Npc->RecomputeSheet();
					Npc->MaxHealth = 100;
				}
			}

			// Two ordinary thinks cross the admission barrier and settle the loadout, exactly as the
			// production path does.
			if (!Setup.bSkipAdmission)
			{
				for (int32 i = 0; i < 2; ++i)
				{
					Wake();
					World.Tick(FElysiumNpcWorldFixture::FirstThinkSeconds);
				}
			}
			if (Setup.bPatrol)
			{
				// Story 8 wave 2: a patrol is the path object's own program. `SetupPatrolType`
				// (`0x1029eb30`) names it and `FollowPatrolPath` (`0x1029ed90`) lays the nodes, in the
				// level scripts' order; each build installs the program at once (`0x1029f56b`).
				World.AcceptInput(TEXT("!self"), FName(TEXT("SetupPatrolType")),
					FElysiumVariant::String(TEXT("255 0 FOLLOW_PATROL_PATH_WALK")),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid(),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid());
				World.AcceptInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
					FElysiumVariant::String(TEXT("route_1 route_2")),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid(),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid());
				Step(0.0);   // the patrol program's first tasks issue the first leg
			}
			Quiet();
		}

		FAiScheduleFixture(const FAiScheduleFixture&) = delete;
		FAiScheduleFixture& operator=(const FAiScheduleFixture&) = delete;

		void Wake(double Now = 0.0)
		{
			FElysiumNpcWorldFixture::Wake({ Guard, Victim }, Now);
		}

		void Quiet()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, Victim });
		}


		void Step(double Now)
		{
			// Hand the AI back and put all four clocks on the caller's `Now`, so the tick below
			// runs exactly one full think (`ResetThinkTimers` `0x102c23f0`).
			FElysiumNpcWorldFixture::Wake({ Guard }, Now);
			World.Tick(Now);
			Quiet();
		}

		// `InputStartSchedule` (`0x101a9b30`) only arms `CineThink` at curtime; the search (within
		// `m_flRadius`) and the push run at the director's next think, which this runs at once.
		void FireStartSchedule()
		{
			World.AcceptInput(TEXT("!self"), FName(TEXT("StartSchedule")), FElysiumVariant::Void(),
				DirectorHandle, DirectorHandle);
			if (FElysiumEntity* Live = World.Resolve(DirectorHandle))
			{
				Live->ThinkAt(World.NowSeconds());
			}
		}

		FElysiumRecordingNpcMotor* MotorFor(const FElysiumNpc* Npc) const
		{
			for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
			{
				if (Npc != nullptr && Motor && Motor->Owner == Npc->Handle)
				{
					return Motor.Get();
				}
			}
			return nullptr;
		}

		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
		{
			return FElysiumNpcWorldFixture::Debug(Entity, Key);
		}
	};
}

// The two recovered tables: `forcestate`'s asymmetry and the mode set.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleTablesTest,
	"Elysium.Arm.AiScriptedSchedule.Tables", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleTablesTest::RunTest(const FString&)
{
	// The load-bearing asymmetry. "Treating the keyvalue as the native enum would swap combat and
	// alert", which is why authored 2 and 3 are asserted in BOTH directions here.
	EElysiumNpcState State = EElysiumNpcState::Prone;
	TestFalse(TEXT("authored 0 forces no state at all"),
		ElysiumAiScriptedSchedule::ForcedState(0, State));

	TestTrue(TEXT("authored 1 forces a state"), ElysiumAiScriptedSchedule::ForcedState(1, State));
	TestEqual(TEXT("...and it is idle"), State, EElysiumNpcState::Idle);

	TestTrue(TEXT("authored 2 forces a state"), ElysiumAiScriptedSchedule::ForcedState(2, State));
	TestEqual(TEXT("...and it is ALERT, not combat"), State, EElysiumNpcState::Alert);

	TestTrue(TEXT("authored 3 forces a state"), ElysiumAiScriptedSchedule::ForcedState(3, State));
	TestEqual(TEXT("...and it is COMBAT, not alert"), State, EElysiumNpcState::Combat);

	TestFalse(TEXT("a value outside the recovered table forces nothing"),
		ElysiumAiScriptedSchedule::ForcedState(4, State));
	TestFalse(TEXT("...and is not a known force state"),
		ElysiumAiScriptedSchedule::IsKnownForceState(4));
	TestTrue(TEXT("0..3 are the known force states"),
		ElysiumAiScriptedSchedule::IsKnownForceState(0)
		&& ElysiumAiScriptedSchedule::IsKnownForceState(3));

	// The mode table: 1..5 and nothing else.
	TestFalse(TEXT("mode 0 is not a movement mode"), ElysiumAiScriptedSchedule::IsKnownMode(0));
	for (int32 Mode = 1; Mode <= 5; ++Mode)
	{
		TestTrue(FString::Printf(TEXT("mode %d is recovered"), Mode),
			ElysiumAiScriptedSchedule::IsKnownMode(Mode));
	}
	TestFalse(TEXT("mode 6 is outside the recovered table"),
		ElysiumAiScriptedSchedule::IsKnownMode(6));

	TestTrue(TEXT("modes 1 and 2 are the move-to-goal variants"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(1) && ElysiumAiScriptedSchedule::IsMoveToGoal(2));
	TestTrue(TEXT("modes 4 and 5 are the follow-path variants"),
		ElysiumAiScriptedSchedule::IsFollowPath(4) && ElysiumAiScriptedSchedule::IsFollowPath(5));
	TestFalse(TEXT("mode 3 runs no program at all"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(3) || ElysiumAiScriptedSchedule::IsFollowPath(3));

	TestEqual(TEXT("the move variants share one registered program"),
		ElysiumAiScriptedSchedule::ProgramFor(1), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("...both of them"),
		ElysiumAiScriptedSchedule::ProgramFor(2), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("the follow variants share the other"),
		ElysiumAiScriptedSchedule::ProgramFor(5), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("mode 3 names no program"),
		ElysiumAiScriptedSchedule::ProgramFor(3), ElysiumScheduleId::None);

	// CHOSEN, NOT RECOVERED — asserted so the choice is visible in one place and a decoded
	// discriminator changes exactly one function and this block.
	TestFalse(TEXT("CHOSEN: mode 1 walks"), ElysiumAiScriptedSchedule::IsRunVariant(1));
	TestTrue(TEXT("CHOSEN: mode 2 runs"), ElysiumAiScriptedSchedule::IsRunVariant(2));
	TestFalse(TEXT("CHOSEN: mode 4 walks"), ElysiumAiScriptedSchedule::IsRunVariant(4));
	TestTrue(TEXT("CHOSEN: mode 5 runs"), ElysiumAiScriptedSchedule::IsRunVariant(5));

	return true;
}

// Mode 1/2 — the move to the goal, over the recording motor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleMoveTest,
	"Elysium.Arm.AiScriptedSchedule.MoveToGoal", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleMoveTest::RunTest(const FString&)
{
	// Mode 2 with `forcestate 2`: the three warehouse thug rows, in miniature.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 2;
		FAiScheduleFixture F(Setup);
		if (!TestNotNull(TEXT("the directed NPC constructs"), F.Guard)
			|| !TestNotNull(TEXT("the director constructs"), F.Director))
		{
			return false;
		}
		TestEqual(TEXT("the authored mode parses under the corpus's own key"),
			F.Debug(F.Director, TEXT("Mode")), FString(TEXT("2 (move-to-goal B) run")));
		TestTrue(TEXT("the authored force state parses through the recovered mapping"),
			F.Debug(F.Director, TEXT("Force state")).Contains(TEXT("2 -> Alert")));
		TestTrue(TEXT("nothing is pushed before the wire fires"),
			!F.Guard->ScriptedScheduleOrder.IsSet());

		F.FireStartSchedule();

		TestTrue(TEXT("the push takes the body under the ScriptedSchedule owner"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);
		TestTrue(TEXT("the pushed order is visible in one row"),
			F.Guard->ScriptedScheduleOrder.IsSet() && F.Guard->ScriptedScheduleOrder.Mode == 2);
		// The state push is a STATE, not a hold: the arbiter must not overwrite it with `Scripted`.
		TestTrue(TEXT("forcestate 2 put the mind in ALERT, not in a scripted hold"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);

		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard);
		if (!TestNotNull(TEXT("the directed NPC owns a motor"), Motor))
		{
			return false;
		}
		TestTrue(TEXT("the program issued the move"), Motor->bMoving);
		TestTrue(TEXT("...to the goal entity's own origin"),
			Motor->RequestedFeet.Equals(GGoalOrigin));
		TestEqual(TEXT("...at the running gait mode 2 chose"),
			Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::RunSpeed);
		F.Step(0.1);
		// Corrected (L13 wave-2 fixes): mode 2 is retail's `ScheduledMoveToGoalEntity` (`0x101a998f`),
		// which installs program 2 FIRST and then `SetGoal`s; the route build completes the current
		// task unless it is a continuous move (`0x102f1ece` slot 529, `0x102f1ede` navigator slot 2),
		// so the translated program's `TASK_PATROL_PATH` is finished before it ever starts and its
		// patrol gait never lands. The run activity mode 2 chose (`0x101a9960`) stands.
		TestEqual(TEXT("0x102f1ede: the patrol task is completed by SetGoal, the run gait stands"),
			Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::RunSpeed);

		// Arrival ends the program, which is what releases the claim.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		F.Step(0.2);
		F.Step(0.3);
		TestTrue(TEXT("the order is dropped once its program ends"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and the body goes back"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
		// `forcestate` was a state push and not a temporary hold, so it OUTLIVES the program.
		TestTrue(TEXT("the pushed state survives the program that came with it"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);
	}

	// Mode 1 walks, which is the CHOSEN half of the pair.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 1;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		F.Step(0.1);
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			TestEqual(TEXT("mode 1 takes the walking gait"),
				Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::WalkSpeed);
		}
		TestTrue(TEXT("mode 1 with forcestate 0 pushes no state"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
	}

	// Mode 4/5: the follow-path family runs its own program.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 4;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestEqual(TEXT("a follow-path mode starts the follow-path program"),
			F.Guard->Schedule.Current, F.Guard->ResolveScheduleId(F.Guard->TranslateSchedule(ElysiumSched::IDLE_WALK)));
		F.Step(0.1);
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			TestTrue(TEXT("a one-node route walks straight to the goal"),
				Motor->RequestedFeet.Equals(GGoalOrigin));
			// The exhausted-route leg ends the program rather than looping forever.
			Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		}
		F.Step(0.2);
		F.Step(0.3);
		// Sample the order, not the later autonomous program's TASK_FAILED bit: the latter may
		// already have attempted a stance on this deliberately headless fixture.
		TestTrue(TEXT("an exhausted route ends the corpus program"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}
	return true;
}

// Mode 3 — the goal becomes the enemy, through the ordinary acquisition transaction.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleAssignEnemyTest,
	"Elysium.Arm.AiScriptedSchedule.AssignEnemy", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleAssignEnemyTest::RunTest(const FString&)
{
	// The four diner assassins: mode 3, forcestate 3, goal `!player`.
	FAiScheduleFixture::FSetup Setup;
	Setup.Mode = 3;
	Setup.ForceState = 3;
	Setup.GoalName = TEXT("!player");
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr || F.Victim == nullptr || F.Player == nullptr)
	{
		return false;
	}

	// A previous acquisition episode, so the last-enemy transfer (and the absence of any latch reset)
	// is observable rather than vacuous.
	F.Guard->BaseMemory.Enemy = F.Victim->Handle;
	// Slot 481's episode words (`0x10270b20`): the found latch `m_afMemory & 0x20000` and the
	// `+0x5b98` occlusion count.
	F.Guard->BaseScheduleHost.MemoryBits |= 0x20000u;
	F.Guard->BaseMemory.EnemyOccludedCheck = 7;
	F.Guard->Cognition.Conditions.Reset();

	F.FireStartSchedule();

	TestTrue(TEXT("the goal is committed as the enemy"),
		F.Guard->BaseMemory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...through SetEnemy, so the old handle went down the last-enemy path"),
		F.Guard->BaseMemory.LastEnemy == F.Victim->Handle);
	// `SetEnemy` (`0x10279a50`) resets no LOS episode: it writes `m_hEnemy`, the last enemy
	// (`0x10279b70`), slot 560 and the discipline sweep, and nothing else. The port's old body
	// forgot the latch, the debounce and the occlusion flag here — port-invented; the episode is
	// `GatherEnemyConditions`' (`0x10270b20`). Corrected to retail in the story 8 L11 integration.
	TestTrue(TEXT("...and SetEnemy leaves the previous LOS episode's latch to the gather pass"),
		(F.Guard->BaseScheduleHost.MemoryBits & 0x20000u) != 0);
	TestEqual(TEXT("...along with its occlusion count (+0x5b98)"),
		F.Guard->BaseMemory.EnemyOccludedCheck, 7);

	// "injects native condition 0x54".
	TestTrue(TEXT("NEW_ENEMY (0x54) is injected"),
		F.Guard->Cognition.Conditions.Has(ECond::NewEnemy));
	TestEqual(TEXT("NEW_ENEMY is retail's own 0x54"), static_cast<int32>(ECond::NewEnemy), 0x54);

	const auto* Memory = F.Guard->EnemyMemory.Find(F.Player->Handle);
	TestTrue(TEXT("slot 544 stores the goal in enemy memory"),
		Memory != nullptr && Memory->LastPosition.Equals(F.Player->Origin));

	// `forcestate 3` is COMBAT, and mode 3 supplied the enemy that state needs.
	TestTrue(TEXT("forcestate 3 put the mind in combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);
	// Mode 3 pushes policy only: it claims no body.
	TestTrue(TEXT("mode 3 claims no body — it is a policy push, not a movement order"),
		F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	TestTrue(TEXT("...and leaves no order behind"),
		!F.Guard->ScriptedScheduleOrder.IsSet());

	// Ordinary combat selection follows from the state the director pushed. Which program it lands
	// on is the recovered slot-604/605 body's answer and nothing else now: this used to assert
	// `SCHED_TROIKA_MELEE_IDLE`, which was the CHOSEN fall-through standing in for a number the port
	// registered no program for.
	const EId Selected = F.Guard->SelectSchedule();
	TestFalse(TEXT("combat selection follows the push rather than the idle stance"),
		Selected == ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestTrue(TEXT("...into a loaded combat program"),
		ElysiumScheduleFor(ElysiumScheduleGlobalId(Selected)) != nullptr);
	// Story 8 L06 integration (corrected to retail): the injected NEW_ENEMY is answered by the
	// pre-selector before slot 438 runs — `0x102ae920` COMBAT arm `0x102aedf0` HasCondition(0x54),
	// not frenzied (`0x102aee01`) -> START_COMBAT `0xea` (`0x102aee03`). The unarmed guard's weapon
	// split would take slot 605 (`0x10385008`), never a melee program.
	TestEqual(TEXT("...which is the pre-selector's START_COMBAT 0xea (0x102aee03)"),
		static_cast<int32>(Selected), 0xea);
	return true;
}

// The recovered refusals: a missing goal, a bare row, and the route-failure switch.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleRefusalTest,
	"Elysium.Arm.AiScriptedSchedule.Refusals", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleRefusalTest::RunTest(const FString&)
{
	// A missing goal logs and stops, and pushes NOTHING.
	// This fires in shipped content: `sm_medical_1`'s `guard_to_cs` names `cs_target`, which that
	// map does not contain.
	{
		// `0x101a98c0`'s "Can't find goal entity" line, on every fire: retail keeps no once-latch.
		AddExpectedError(TEXT("Can't find goal entity"), EAutomationExpectedErrorFlags::Contains, 2);
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 3;
		Setup.GoalName = TEXT("cs_target");   // authored, absent — the corpus's own case
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("a missing goal pushes no order"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and claims no body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
		// "logs and stops" — including the state, which the director never got far enough to push.
		TestTrue(TEXT("...and does not push the forced state either"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
		// The second fire looks again and logs again.
		F.FireStartSchedule();
	}

	// The spawn validator (`0x101a9730`): neither a schedule nor a forced state is a `DevMsg(2)`, and
	// the row still stands.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 0;
		Setup.ForceState = 0;
		FAiScheduleFixture F(Setup);
		TestNotNull(TEXT("the bare row still constructs as an entity"), F.Director);
	}

	// A forced state with no movement mode is an ordinary row.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 0;
		Setup.ForceState = 1;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("a state-only row pushes the state"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
		TestTrue(TEXT("...and no order"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}

	// A body that will not take the route reports it once.
	{
		// Corrected (L13 wave-2 fixes): modes 1/2 are retail's `ScheduledMoveToGoalEntity`, whose
		// false answer prints retail's own line once (`0x101a99c5`, string `0x10595230`).
		AddExpectedError(TEXT("ScheduledMoveToGoalEntity to goal entity"),
			EAutomationExpectedErrorFlags::Contains, 1);
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			Motor->bAcceptMoves = false;   // "this goal has no path"
		}
		F.FireStartSchedule();
		F.Step(0.1);
		F.Step(0.2);   // the failing pass, then the routing pass (story 25)
		TestFalse(TEXT("a refused route releases the directed order"), F.Guard->ScriptedScheduleOrder.IsSet());
		F.Step(1.2);
		F.Step(2.2);
		TestTrue(TEXT("a refused route ends the order once FAIL has run out"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and gives the body back"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	}

	// A director that fires before the NPC's first think is DEFERRED, not lost.
	// `sm_medical_1` wires `guard_to_nurse` off an `npc_maker`'s `OnSpawnNPC`, so this is the
	// shipped case rather than a synthetic one.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 2;
		Setup.bSkipAdmission = true;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("an un-admitted mind keeps the push waiting"),
			F.Guard->GetMind().Admission() == FElysiumNpcMind::EAdmission::Armed);
		TestTrue(TEXT("...and claims no body while it waits"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);

		F.Step(0.1);   // the admission think, which establishes Idle and returns
		F.Step(0.2);   // the replay
		TestTrue(TEXT("the deferred order lands once admission has run"),
			F.Guard->ScriptedScheduleOrder.IsSet() && F.Guard->ScriptedScheduleOrder.Mode == 2);
		// The point of deferring the WHOLE push: admission establishes idle, so a forced state
		// applied ahead of it would have been wiped by the barrier it was racing.
		TestTrue(TEXT("...carrying the forced state the admission barrier would have wiped"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);
	}

	// Spawn flag 0x800 suppresses exactly that warning.
	{
		// No AddExpectedError here on purpose: the point of the case is that nothing is emitted.
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.SpawnFlags = ElysiumAiScriptedSchedule::SpawnFlagSuppressRouteWarning;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("the suppression flag is visible on the director"),
			F.Debug(F.Director, TEXT("Spawnflags")).Contains(TEXT("0x800")));
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			Motor->bAcceptMoves = false;
		}
		F.FireStartSchedule();
		F.Step(0.1);
		F.Step(0.2);
		F.Step(1.2);
		F.Step(2.2);
		TestTrue(TEXT("the refusal still ends the order silently"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}
	return true;
}

// `ChangeSchedule` / `StartSchedule` — a native schedule named by a script.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleNamedTest,
	"Elysium.Arm.AiScriptedSchedule.NamedSchedule", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleNamedTest::RunTest(const FString&)
{
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };

	// The `-` case below counts its stub line. Declared before the fixture stands, so it is matched
	// ahead of the fixture's own "Stub fired" noise (`ElysiumFixtureNoise.h`): the framework credits
	// the first-declared matching expectation only.
	AddExpectedError(TEXT("CAI_BaseNPC.ChangeSchedule"), EAutomationExpectedErrorFlags::Contains, 1);

	FAiScheduleFixture::FSetup Setup;
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr)
	{
		return false;
	}

	// A registered name starts through the ordinary kernel.
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_TROIKA_MELEE_IDLE")), F.Guard->Handle, F.Guard->Handle);
	TestEqual(TEXT("a registered name starts its own program"),
		F.Guard->Schedule.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MELEE_IDLE));

	// Case folding is retail's own posture for every named surface in this runtime.
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("StartSchedule")),
		FElysiumVariant::String(TEXT("sched_troika_chase_enemy")), F.Guard->Handle,
		F.Guard->Handle);
	TestEqual(TEXT("the lookup is case-insensitive, and StartSchedule takes the same door"),
		F.Guard->Schedule.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_CHASE_ENEMY));

	// The names the shipped scripts actually use. All five `ChangeSchedule` sites in the corpus ask
	// for `SCHED_VDOG_SNARL`, `SCHED_VDOG_MADEFRIEND` or the literal `-`, and this port resolved
	// NONE of them: it registered 28 programs of its own and the dog's two were not among them. The
	// corpus registers `CNPC_VDog`'s ten, 0x15e..0x167, so the first two resolve now -- and they
	// resolve into a SPECIES space, through a guard that is not a dog, which is the whole point of
	// the schedule NAMESPACE being global while the id spaces are per class.
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_VDOG_SNARL")), F.Guard->Handle, F.Guard->Handle);
	const FElysiumScheduleProgram* Snarl =
		FElysiumScheduleCorpus::Get().Manager().FindByName(TEXT("SCHED_VDOG_SNARL"));
	if (TestNotNull(TEXT("SCHED_VDOG_SNARL is a loaded program"), Snarl))
	{
		TestEqual(TEXT("...and a script's ChangeSchedule now starts it"),
			F.Guard->Schedule.Current, Snarl->GlobalId);
	}
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_VDOG_MADEFRIEND")), F.Guard->Handle, F.Guard->Handle);
	const FElysiumScheduleProgram* MadeFriend =
		FElysiumScheduleCorpus::Get().Manager().FindByName(TEXT("SCHED_VDOG_MADEFRIEND"));
	if (TestNotNull(TEXT("SCHED_VDOG_MADEFRIEND is loaded too"), MadeFriend))
	{
		TestEqual(TEXT("...and starts"), F.Guard->Schedule.Current, MadeFriend->GlobalId);
	}

	// The stub funnel survives, but it fires only for a name the corpus really does not carry. `-` is
	// the third thing the shipped scripts ask for (its one stub line is declared at the top).
	const int32 BeforeDash = F.Guard->Schedule.Current;
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("-")), F.Guard->Handle, F.Guard->Handle);
	TestEqual(TEXT("a name no class registers changes nothing"),
		F.Guard->Schedule.Current, BeforeDash);

	// An empty parameter is refused by name rather than starting something arbitrary.
	AddExpectedError(TEXT("was fired with no schedule name"),
		EAutomationExpectedErrorFlags::Contains, 1);
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")), FElysiumVariant::Void(),
		F.Guard->Handle, F.Guard->Handle);
	return true;
}

// Precedence: the ScriptedSchedule owner's place in the arbiter.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedSchedulePrecedenceTest,
	"Elysium.Arm.AiScriptedSchedule.Precedence", GElysiumTestFlags)
bool FElysiumAiScriptedSchedulePrecedenceTest::RunTest(const FString&)
{
	// A director displaces a patrol route, and the route resumes.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.bPatrol = true;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		// Corrected to retail (story 8 wave 2): the patrol is a PROGRAM, not a parked body owner.
		auto OnPatrolProgram = [&F]()
		{
			return FString(ElysiumScheduleName(F.Guard->Schedule.Current)).Contains(TEXT("FOLLOW_PATROL_PATH_WALK"));
		};
		TestTrue(TEXT("the patrol path's program runs first (0x1029f56b)"), OnPatrolProgram());

		F.FireStartSchedule();
		TestTrue(TEXT("the director displaces it"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);
		TestTrue(TEXT("...and the path object survives the program change"),
			F.Guard->IsPatrolActiveForDebug());

		F.Step(0.1);
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard);
		if (!TestNotNull(TEXT("the directed NPC owns a motor"), Motor))
		{
			return false;
		}
		TestTrue(TEXT("the goal, not the patrol point, is what the body was sent to"),
			Motor->RequestedFeet.Equals(GGoalOrigin));

		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		F.Step(0.2);
		F.Step(0.3);
		Motor->SampleStatus = EElysiumNpcMoveStatus::Moving;
		F.Step(0.4);
		F.Step(0.5);
		// `SelectSchedule` case 1 step 3 (`0x102af6b6`) answers the surviving path's schedule word
		// once the director's program is done.
		TestTrue(TEXT("the patrol program comes back through selection when the program ends"),
			OnPatrolProgram());
		TestTrue(TEXT("...and re-issues a leg toward a patrol point"),
			Motor->RequestedFeet.Equals(FVector(0.0, -200.0, 0.0))
			|| Motor->RequestedFeet.Equals(FVector(0.0, -400.0, 0.0)));
	}

	// A scripted sequence displaces the director.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("the director holds the body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);

		TestTrue(TEXT("a beat takes it outright"),
			F.Guard->ClaimScriptBody(TEXT("test beat")));
		TestTrue(TEXT("...and the arbiter says so"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Sequence);
		// A director's order is DROPPED rather than parked: `aiscripted_schedule` has no resume.
		TestTrue(TEXT("the pushed order leaves with the body"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestEqual(TEXT("...and so does the program it was running"),
			F.Guard->Schedule.Current, ElysiumScheduleId::None);

		F.Guard->ReleaseScriptBody(TEXT("test beat ended"));
		TestTrue(TEXT("the beat's release restores an unowned body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	}

	// (Deleted with the patrol executor, story 8 wave 2: "three deep — a beat over a combat claim over
	// a patrol route" asserted the arbiter's one parked slot holding the route's token. Retail's patrol
	// is a schedule, so there is no route token to park; the case had no retail counterpart.)
	return true;
}

// The pre-emption fix: a committed enemy outranks an autonomous executor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedSchedulePreemptionTest,
	"Elysium.Arm.AiScriptedSchedule.CombatPreemption", GElysiumTestFlags)
bool FElysiumAiScriptedSchedulePreemptionTest::RunTest(const FString&)
{
	// The recovered emission this case runs into on the way back out of combat: `0x1026f660` case 2
	// emits `***Combat state with no enemy!` (`105cc04c`) and falls back to alert. Retail's
	// `DevWarning` is NOT latched and story 29e's port stopped latching it, so the count is the
	// number of no-enemy combat passes rather than one.
	AddExpectedError(TEXT("Combat state with no enemy"), EAutomationExpectedErrorFlags::Contains, 0);

	FAiScheduleFixture::FSetup Setup;
	Setup.bPatrol = true;
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr || F.Victim == nullptr)
	{
		return false;
	}
	// Corrected to retail (story 8 wave 2): the route is the patrol path's program.
	TestTrue(TEXT("the guard starts on its route (the patrol path's program, 0x1029f56b)"),
		FString(ElysiumScheduleName(F.Guard->Schedule.Current)).Contains(TEXT("FOLLOW_PATROL_PATH_WALK")));

	// Commit an enemy the way `MaintainSchedule 0x102817c0` does — `SetState(m_IdealNPCState)`
	// (`0x1026e340`) — exactly as `FCombatFixture` does. A patrol executor installs no schedule
	// mask, so `0x102ad660`'s idle→combat arm (`HasInterruptCondition 0x10269d30`) cannot answer.
	F.Guard->Relationships.SetEntity(F.Victim->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->BaseMemory.Enemy = F.Victim->Handle;
	F.Guard->SetState(2);
	// `1026e35d`/`1026e368`: `SetState` writes BOTH state words, so the ideal matches the current.
	TestEqual(TEXT("1026e340 writes m_NPCState"), F.Guard->NpcStateRetail(), 2);
	TestEqual(TEXT("...and m_IdealNPCState with the same value"), F.Guard->IdealStateRetail(), 2);
	TestTrue(TEXT("the acquisition promotes the mind to combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);

	// A committed enemy must reach combat selection.
	F.Step(0.6);
	// Asked of the PRODUCER directly rather than read off the NPC's condition set after the think,
	// because the condition does not outlive the selection it drove: `ElysiumSchedule::Start` clears
	// every gathered condition, which is retail's own `CAI_BaseNPC::SetSchedule` (`0x10280e50`)
	// zeroing the six dwords at `+0x5c5c` that `SetCondition` (`0x10269a20`) writes. A post-install
	// snapshot of the set is empty in retail too, so the old spelling of this assertion was reading a
	// slot the install had legitimately wiped. The claim under test is unchanged.
	// The patrol program walked the guard to its first point (story 8 wave 2), so the enemy is
	// stood inside its reach and cone for the producer's question.
	F.Victim->Origin = F.Guard->Origin + FVector(Cm(20.0), 0.0, 0.0);
	F.Guard->Angles.Y = 0.0;
	FElysiumNpcConditions Attack;
	ElysiumNpcCond::GatherAttackConditions(*F.Guard, 0.6, Attack);
	TestTrue(TEXT("the committed enemy raises CAN_MELEE_ATTACK1"),
		Attack.Has(EElysiumNpcCond::CanMeleeAttack1));
	TestTrue(TEXT("a patrolling NPC in combat is running a schedule at all"),
		F.Guard->Schedule.IsRunning());

	// (Story 8 wave 2: the arbiter's parked-route assertions are deleted with the patrol executor —
	// retail's patrol is a schedule, which the combat program replaced; what survives the change is
	// the path object, and selection returns to its program.)
	TestTrue(TEXT("the path object survives the combat program"), F.Guard->IsPatrolActiveForDebug());

	// The enemy dies. The transaction clears it, the ideal state falls back, and the route resumes.
	F.Victim->Kill();
	for (int32 i = 0; i < 4; ++i)
	{
		F.Step(0.7 + 0.1 * i);
	}
	// Retail's observable is slot 167 `GetEnemy()`: the removed victim's handle no longer resolves.
	// `ChooseEnemy` (`0x10279dd0`) clears `m_hEnemy` only on its change work, and a script-assigned
	// enemy never set the `m_afMemory` enemy bits (`0x1027a0ff` is their one writer), so no
	// went-null (`0x10279e85`) fires and the stale handle stands, resolving null. The old assertion
	// read the raw handle the port's guessed body used to clear — corrected to retail (story 8 L11).
	TestNull(TEXT("the dead enemy is no longer committed (GetEnemy answers null)"),
		static_cast<const FElysiumNpcBase&>(*F.Guard).GetEnemy());
	TestFalse(TEXT("the mind has left combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);
	// Out of combat with no enemy the ideal state falls to ALERT (`0x1026f660` case 2), whose
	// selection carries no patrol arm; `SelectSchedule` case 1 step 3 (`0x102af6b6`) answers the
	// surviving path's program once the NPC is back in IDLE.
	TestTrue(TEXT("the path object survives the fight"), F.Guard->IsPatrolActiveForDebug());
	for (int32 i = 0; i < 120 && !FString(ElysiumScheduleName(F.Guard->Schedule.Current)).Contains(
		TEXT("FOLLOW_PATROL_PATH_WALK")); ++i)
	{
		F.Step(1.5 + 0.5 * i);
	}
	TestTrue(TEXT("the patrol program comes back through selection"),
		FString(ElysiumScheduleName(F.Guard->Schedule.Current)).Contains(TEXT("FOLLOW_PATROL_PATH_WALK")));
	return true;
}

}   // namespace ElysiumAiScriptedScheduleTests

#endif // WITH_DEV_AUTOMATION_TESTS
